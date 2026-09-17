#!/usr/bin/env python3
"""USB firmware tests, driven through the patched uCsim simulator.

Run from the repo root (inside `nix develop`, after building firmware):

    meson compile -C build nuphy-air60_default_smk.hex
    python3 -m unittest discover -s tests        # or: python3 tests/test_usb.py

Override targets with env vars SMK_UCSIM (simulator) and SMK_FIRMWARE (.hex).
"""

import unittest

from sim import (
    Sim, get_descriptor,
    DESC_DEVICE, DESC_CONFIGURATION, DESC_STRING,
    set_address, set_configuration, get_configuration, get_status_device,
    hid_set_idle, hid_get_idle, hid_set_protocol, hid_get_protocol,
)
from devices import Air60Sim

SIM = Sim()

# The device-descriptor header is identical across SMK keyboards (only VID/PID,
# which live further in, differ), so these assertions hold for any built target.
DEVICE_DESC_HEADER = [0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08]


def setUpModule():
    reason = SIM.available()
    if reason:
        raise unittest.SkipTest(reason)


class TestUsbInterrupt(unittest.TestCase):
    def test_setupif_vectors_to_usb_handler(self):
        """After real init (EA=1, IEN1.EUSB=1 set by usb_init), USBIF1.SETUPIF
        must vector the CPU to the USB vector."""
        out = SIM.fire_vector()
        self.assertEqual(SIM.stopped_at(out), 0x3B,
                         f"expected vector to 0x3b; sim output:\n{out}")

    def test_vectoring_pushes_return_address(self):
        """Accepting the interrupt must push the 2-byte return address (SP +2).
        Driven over the interactive socket so each SP read is cleanly framed."""
        kb = Air60Sim()
        try:
            kb.boot(usb=True)
            pre, post, stopped = kb.fire_usb_vector()
        finally:
            kb.close()
        self.assertEqual(stopped, 0x3b, "USBIF1.SETUPIF should vector to 0x3b")
        self.assertEqual(post - pre, 2,
                         f"USB vector should push 2 bytes; pre=0x{pre:02x} post=0x{post:02x}")


class TestGetDescriptor(unittest.TestCase):
    def test_first_in_packet_is_device_descriptor_header(self):
        out = SIM.control_in(get_descriptor(DESC_DEVICE))
        pkts = SIM.in_packets(out)
        self.assertTrue(pkts, f"no EP0 IN packet captured; sim output:\n{out}")
        self.assertEqual(pkts[0], DEVICE_DESC_HEADER)

    def test_descriptor_reports_length_and_type(self):
        pkts = SIM.in_packets(SIM.control_in(get_descriptor(DESC_DEVICE)))
        self.assertTrue(pkts)
        self.assertEqual(pkts[0][0], 18, "bLength should be 18")
        self.assertEqual(pkts[0][1], DESC_DEVICE, "bDescriptorType should be DEVICE")

    def test_max_packet_size_0_is_8(self):
        pkts = SIM.in_packets(SIM.control_in(get_descriptor(DESC_DEVICE)))
        self.assertTrue(pkts)
        self.assertEqual(pkts[0][7], 8, "bMaxPacketSize0 should be 8")

    def test_full_device_descriptor_multipacket(self):
        """The SIE model pulls every EP0 IN chunk (8+8+2 bytes), reassembling the
        whole 18-byte device descriptor."""
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_DEVICE)))
        self.assertEqual(len(desc), 18, f"expected 18-byte descriptor, got {desc}")
        self.assertEqual(desc[:8], DEVICE_DESC_HEADER)
        self.assertEqual(desc[17], 1, "bNumConfigurations should be 1")

    def test_device_descriptor_vid_pid(self):
        """idVendor/idProduct live in the 2nd IN packet, so this only passes once
        multi-packet reassembly works. Asserted for the nuphy-air60 reference."""
        if "nuphy" not in (SIM.firmware or ""):
            self.skipTest("VID/PID assertion is specific to nuphy-air60 firmware")
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_DEVICE)))
        self.assertGreaterEqual(len(desc), 12)
        vid = desc[8] | (desc[9] << 8)
        pid = desc[10] | (desc[11] << 8)
        self.assertEqual((vid, pid), (0x05AC, 0x024F))


class TestIspJump(unittest.TestCase):
    """The ISP jump is the firmware's path back into the bootloader for
    reflashing -- the anti-brick recovery path. A SET_REPORT(Feature, ISP)
    control transfer with the right confirm bytes must land in the bootloader at
    0xff00 with the B=0xa5/ACC=0x5a handshake the bootloader checks for."""

    def test_set_report_isp_jumps_to_bootloader(self):
        out = SIM.trigger_isp_jump()
        self.assertEqual(SIM.stopped_at(out), SIM.ISP_BOOTLOADER,
                         f"expected jump to bootloader 0x{SIM.ISP_BOOTLOADER:x}; output:\n{out}")

    def test_isp_jump_sets_bootloader_handshake(self):
        """ACC=0x5a / B=0xa5 also proves arrival via isp_jump() rather than the
        CPU running off the end of program memory into 0xff00."""
        out = SIM.trigger_isp_jump()
        self.assertEqual(SIM.acc(out), SIM.ISP_MAGIC_ACC, "ACC handshake byte")
        self.assertEqual(SIM.reg_b(out), SIM.ISP_MAGIC_B, "B handshake byte")

    def test_wrong_confirm_bytes_do_not_jump(self):
        """isp_jump() is gated on the OUT payload being 0x05 0x75; a wrong payload
        must NOT jump -- the ISR returns to the NOP sled instead."""
        out = SIM.trigger_isp_jump(confirm=(0x00, 0x00), run_task=False)
        self.assertEqual(SIM.stopped_at(out), SIM.SLED_END,
                         "with wrong confirm bytes the CPU should return to the sled, not jump")
        self.assertNotEqual(SIM.acc(out), SIM.ISP_MAGIC_ACC,
                            "isp_jump()'s ACC=0x5a must not have run")


class TestStandardRequests(unittest.TestCase):
    def test_set_address_updates_usbaddr(self):
        self.assertEqual(SIM.set_address(0x2A), 0x2A)

    def test_set_then_get_configuration(self):
        # SET_CONFIGURATION requires a non-zero address first
        out = SIM.controls(set_address(0x2A), set_configuration(1), get_configuration())
        self.assertEqual(SIM.reassemble(out)[-1:], [1], f"GET_CONFIGURATION; output:\n{out}")

    def test_get_status_device(self):
        status = SIM.reassemble(SIM.control_in(get_status_device()))
        self.assertEqual(len(status), 2, "device GET_STATUS returns 2 bytes")
        self.assertEqual(status[1], 0, "status high byte is reserved 0")

    def test_configuration_descriptor(self):
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_CONFIGURATION)))
        self.assertGreaterEqual(len(desc), 9)
        self.assertEqual(desc[0], 9, "config descriptor bLength")
        self.assertEqual(desc[1], DESC_CONFIGURATION, "bDescriptorType")
        self.assertEqual(desc[4], 2, "bNumInterfaces (keyboard + extra)")

    def test_string_descriptor_langid(self):
        desc = SIM.reassemble(SIM.control_in(get_descriptor(DESC_STRING, index=0)))
        self.assertEqual(desc[:4], [0x04, DESC_STRING, 0x09, 0x04], "LANGID = en-US (0x0409)")


class TestHidRequests(unittest.TestCase):
    def test_set_idle_get_idle_roundtrip(self):
        out = SIM.controls(hid_set_idle(0x42), hid_get_idle())
        self.assertEqual(SIM.reassemble(out)[-1:], [0x42])

    def test_set_protocol_get_protocol_roundtrip(self):
        out = SIM.controls(hid_set_protocol(1, iface=0), hid_get_protocol(iface=0))
        self.assertEqual(SIM.reassemble(out)[-1:], [1])

    def test_led_output_report(self):
        """SET_REPORT(Output) of an LED byte must land in keyboard_state.led_state."""
        self.assertEqual(SIM.led_report(0x07), 0x07)


class TestKeyboardReport(unittest.TestCase):
    def test_6kro_report_carries_keycodes(self):
        """send_keyboard_report() must emit the pressed keycodes as an 8-byte EP1
        report (bytes 2..7 = keys; byte 0 = modifiers, sourced separately)."""
        kb = Air60Sim()
        try:
            kb.boot(usb=True)
            kb.mark_usb_configured()
            reps = kb.send_report_direct([0x04, 0x05, 0x06])
        finally:
            kb.close()
        self.assertTrue(reps, "send_keyboard_report() should emit an EP1 report")
        rpt = reps[-1]
        self.assertEqual(len(rpt), 8, f"keyboard report is 8 bytes; got {rpt}")
        self.assertEqual(rpt[2:8], [0x04, 0x05, 0x06, 0x00, 0x00, 0x00])


class TestKeyEvent(unittest.TestCase):
    """End to end with NO cold injection: a physical key press propagates through
    the real scan path (Timer2 ISR -> matrix scan -> matrix_task ->
    process_key_state -> HID report on EP1).

    The key matrix is the *board's* hardware, emulated test-side
    (devices.KeyMatrix): the simulator exposes the GPIO ports, and the test pulls
    the pressed key's row low while its column is scanned."""

    def test_keypress_resolves_keycode_and_sends_report(self):
        # col 1, row 2 = KC_A (0x04) in the default keymap (row 2 is CAPS,A,S,D,...)
        kb = Air60Sim()
        try:
            kb.boot(usb=True)
            kb.mark_usb_configured()
            kb.matrix.press(1, 2)
            reps = kb.scan_for_report()
        finally:
            kb.close()
        self.assertTrue(reps, "a keypress should produce an EP1 report")
        self.assertEqual(reps[-1], [0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00],
                         f"EP1 report should carry KC_A; got {reps[-1]}")

    def test_no_key_produces_no_events(self):
        # nothing pressed: the scan path still runs every loop, but with idle rows
        # held high test-side it must report no key (guards against phantom keys).
        kb = Air60Sim()
        try:
            kb.boot(usb=True)
            reps = kb.scan_for_report(max_hits=80)  # no key staged -> expect none
        finally:
            kb.close()
        self.assertEqual(reps, [], f"no pressed key must produce no EP1 report; got {reps}")


class TestEnumerationThroughInit(unittest.TestCase):
    """Boot from RESET through the real init path (crt0 -> main -> init ->
    clock_init[PLL] -> usb_init -> EA=1), then enumerate -- exercising actual
    initialization rather than cold-injecting. Relies on the -t 52 (256-byte
    IRAM) model and the simulator's PLL-lock emulation."""

    def test_usb_device_state_default_after_real_init(self):
        # Reaching the post-init point proves the whole init chain executed, and
        # usb_init() set usb_device_state = DEFAULT.
        self.assertEqual(SIM.boot_post_init_state(), SIM.STATE_DEFAULT)

    def test_enumeration_reaches_configured(self):
        out = SIM.boot_enumerate(set_address(0x2A), set_configuration(1))
        self.assertEqual(SIM.dump_value(out, SIM.USBADDR), 0x2A,
                         "SET_ADDRESS must commit the address to USBADDR")
        self.assertEqual(SIM.dump_value(out, SIM.USB_DEVICE_STATE), SIM.STATE_CONFIGURED,
                         "SET_ADDRESS then SET_CONFIGURATION must reach CONFIGURED")


if __name__ == "__main__":
    unittest.main()
