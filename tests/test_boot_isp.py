#!/usr/bin/env python3
"""ISP bootloader tests.

The layout checks read the built `.hex`; the rest drive it in the patched uCsim
simulator, where the descriptors and the page-read path can be exercised the way
a host would.

Run from the repo root (inside `nix develop`, after building):

    meson compile -C build nuphy-air60_boot.hex
    python3 -m unittest discover -s tests

Override targets with env vars SMK_UCSIM (simulator) and SMK_BOOTLOADER (.hex).
"""

import unittest

from bootsim import (
    BootSim, get_descriptor,
    DESC_DEVICE, DESC_CONFIGURATION, DESC_STRING, DESC_HID, DESC_REPORT,
)

SIM = BootSim()

FLASH_SIZE = 0x10000
BOOTLOADER_SIZE = 0x1000
PAGE_SIZE = 2048

BOOT_BASE = FLASH_SIZE - BOOTLOADER_SIZE
ISP_ENTRY = FLASH_SIZE - 0x100


def setUpModule():
    reason = SIM.available()
    if reason:
        raise unittest.SkipTest(reason)


class TestLayout(unittest.TestCase):
    """Where the image sits. Everything below BOOT_BASE belongs to the
    application, and the entry stub has to land on the address the application's
    `isp_jump()` hard-codes."""

    def test_image_stays_in_the_bootloader_region(self):
        addrs = SIM.rom.keys()
        self.assertGreaterEqual(min(addrs), BOOT_BASE)
        self.assertLess(max(addrs), FLASH_SIZE)

    def test_reset_entry_is_an_ljmp(self):
        self.assertEqual(SIM.rom[BOOT_BASE], 0x02)

    def test_isp_entry_stub_checks_the_key_and_jumps_back(self):
        """`xrl a,#0x5a` gates the stub, and it ends by restarting at BOOT_BASE
        so a request goes through the same startup as a cold boot."""
        stub = bytes(SIM.rom.get(ISP_ENTRY + i, 0) for i in range(0x40))
        self.assertEqual(stub[2:4], bytes([0x64, 0x5A]), f"stub starts {stub[:8].hex(' ')}")
        self.assertIn(bytes([0x02, BOOT_BASE >> 8, BOOT_BASE & 0xFF]), stub)

    def test_nothing_between_the_bootloader_and_its_entry_stub(self):
        """--code-size has to keep the bootloader proper below ISP_ENTRY."""
        body = [a for a in SIM.rom if a < ISP_ENTRY]
        self.assertLess(max(body), ISP_ENTRY)


class TestDeviceDescriptor(unittest.TestCase):
    def setUp(self):
        self.desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_DEVICE, length=18), iterations=6))

    def test_length_and_type(self):
        self.assertEqual(len(self.desc), 18)
        self.assertEqual(self.desc[0], 18)
        self.assertEqual(self.desc[1], DESC_DEVICE)

    def test_identity_is_the_one_flashing_tools_look_for(self):
        vid = self.desc[8] | (self.desc[9] << 8)
        pid = self.desc[10] | (self.desc[11] << 8)
        self.assertEqual((vid, pid), (0x0603, 0x1020))

    def test_every_string_index_is_populated(self):
        """The stock bootloaders left these zero, so a device in ISP mode had no
        name and nothing to tell one board from another."""
        self.assertEqual(self.desc[14:17], [1, 2, 3])


class TestConfigurationDescriptor(unittest.TestCase):
    def setUp(self):
        self.desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_CONFIGURATION, length=64), iterations=10))
        self.interface = self.desc[9:18]
        self.endpoint = self.desc[27:34]

    def test_total_length_matches(self):
        self.assertEqual(self.desc[2] | (self.desc[3] << 8), len(self.desc))

    def test_one_hid_interface(self):
        self.assertEqual(self.desc[4], 1, "bNumInterfaces")
        self.assertEqual(self.interface[1], 0x04, "bDescriptorType should be INTERFACE")
        self.assertEqual(self.interface[5], 0x03, "bInterfaceClass should be HID")

    def test_interface_has_an_interrupt_in_endpoint(self):
        """HID 1.11 4.4 requires one. The stock bootloaders declared
        bNumEndpoints = 0 and ran everything over EP0."""
        self.assertEqual(self.interface[4], 1, "bNumEndpoints")
        self.assertEqual(self.endpoint[1], 0x05, "bDescriptorType should be ENDPOINT")
        self.assertEqual(self.endpoint[2], 0x81, "bEndpointAddress should be EP1 IN")
        self.assertEqual(self.endpoint[3], 0x03, "bmAttributes should be interrupt")

    def test_hid_descriptor_declares_the_report_descriptor(self):
        hid = self.desc[18:27]
        self.assertEqual(hid[1], DESC_HID)
        length = hid[7] | (hid[8] << 8)
        report = SIM.reassemble(SIM.control_in(get_descriptor(DESC_REPORT, length=255, recipient=0x81), iterations=16))
        self.assertEqual(length, len(report))


class TestReportDescriptor(unittest.TestCase):
    def setUp(self):
        self.desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_REPORT, length=255, recipient=0x81), iterations=16))

    def test_single_top_level_collection(self):
        """Two of them is what makes Windows split the device into two HID paths,
        so a tool has to find and open both halves."""
        self.assertEqual(self.desc.count(0xA1), 1, "Collection items")
        self.assertEqual(self.desc.count(0xC0), 1, "End Collection items")

    def test_logical_maximum_is_not_negative(self):
        """`25 FF` is Logical Maximum (-1): HID item data is signed, so the stock
        descriptor said max < min. The two-byte form says 255."""
        self.assertNotIn(bytes([0x25, 0xFF]), bytes(self.desc))
        self.assertIn(bytes([0x26, 0xFF, 0x00]), bytes(self.desc))

    def test_page_report_count_matches_the_transfer(self):
        """One page plus its opcode; the report ID is not counted."""
        count = PAGE_SIZE + 1
        self.assertIn(bytes([0x96, count & 0xFF, count >> 8]), bytes(self.desc))

    def test_carries_the_command_the_transfer_and_an_input_report(self):
        report_ids = [self.desc[i + 1] for i in range(len(self.desc) - 1) if self.desc[i] == 0x85]
        self.assertEqual(report_ids, [0x05, 0x06, 0x07])


class TestStringDescriptors(unittest.TestCase):
    @staticmethod
    def _text(desc):
        return bytes(desc[2:]).decode("utf-16-le")

    def test_language_id_is_us_english(self):
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_STRING, index=0, length=64), iterations=4))
        self.assertEqual(desc, [4, 3, 0x09, 0x04])

    def test_product_names_the_board(self):
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_STRING, index=2, length=255), iterations=24))
        self.assertIn("SMK ISP Bootloader", self._text(desc))

    def test_serial_carries_the_application_identity(self):
        """In ISP mode every SinoWealth board answers to 0603:1020; this is what
        says which one it is."""
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_STRING, index=3, length=255), iterations=12))
        self.assertRegex(self._text(desc), r"^0x[0-9a-f]{4}:0x[0-9a-f]{4}$")


class TestPageRead(unittest.TestCase):
    def test_read_returns_flash_from_the_requested_address(self):
        """INIT_READ then one transfer report. The simulator's SIE stops feeding
        EP0 IN completions after 16 packets, so this sees the start of the page
        rather than all 2048 bytes of it."""
        got = SIM.read_page(BOOT_BASE, PAGE_SIZE, iterations=300)

        self.assertGreater(len(got), 2, "no page data came back")
        self.assertEqual(got[0], 0x06, "report ID")
        self.assertEqual(got[1], 0x72, "read-page opcode")

        for offset, byte in enumerate(got[2:]):
            expected = SIM.rom.get(BOOT_BASE + offset)
            if expected is not None:  # unprogrammed cells read differently in the simulator
                self.assertEqual(byte, expected, f"flash byte at {BOOT_BASE + offset:#06x}")


if __name__ == "__main__":
    unittest.main()
