#!/usr/bin/env python3
"""What the bootloader decides at power-on, driven through the simulator.

The key matrix is emulated test-side (see devices.py), so this is the real
scan: the bootloader drives a column, the model pulls the rows a staged key
shorts, and the bootloader reads them back.

Run from the repo root (inside `nix develop`, after building):

    meson compile -C build nuphy-air60_boot.hex
    python3 -m unittest discover -s tests
"""

import re
import unittest
from pathlib import Path

from bootsim import BootSim, ACC, REG_B, ISP_KEY_A, ISP_KEY_B, PIN_STAGE_P5, PIN_STAGE_P7
from devices import UcsimSession, KeyMatrix
from sim import load_symbols

SIM = BootSim()

CHORD_HEADER_SUFFIX = "_boot_chord.h"

APP_ENTRY = 0x9000  # a stand-in application: SJMP $, so reaching it is unmistakable
APP_VECTOR_ADDR = 0xEFFB

# the scan samples the chord several times over ~60 ms, and each column read stops us twice
MAX_STOPS = 1000


def setUpModule():
    reason = SIM.available()
    if reason:
        raise unittest.SkipTest(reason)


def chord_keys():
    """The (column, row) pairs the build resolved the chord to."""
    firmware = Path(SIM.firmware)
    header = firmware.with_name(firmware.name.replace("_boot.hex", CHORD_HEADER_SUFFIX))
    if not header.exists():
        raise unittest.SkipTest(f"no generated chord header {header}")
    text = header.read_text()
    return [(int(c), int(r)) for c, r in re.findall(r"\{(\d+)u,\s*(\d+)u\}", text)]


class BootRun:
    """One power-on, with a set of keys held down."""

    def __init__(self, pressed):
        self.sess = UcsimSession(firmware=SIM.firmware)
        self.matrix = KeyMatrix()
        for col, row in pressed:
            self.matrix.press(col, row)
        self.syms = load_symbols(Path(SIM.firmware).with_suffix(".map"))

    def close(self):
        self.sess.close()

    def outcome(self):
        """Where the boot decision led: 'app' or 'isp'."""
        s = self.sess
        s.cmd("reset")
        s.cmd("set mem rom 0x0000 0x02 0xf0 0x00")                     # reset -> bootloader
        s.cmd(f"set mem rom 0x{APP_VECTOR_ADDR:x} 0x02 0x90 0x00")     # an enabled application
        s.cmd(f"set mem rom 0x{APP_ENTRY:x} 0x80 0xfe")                # ...which just spins

        read_rows = self.syms["user_matrix_read_rows"]
        poll = self.syms["boot_usb_poll"]

        s.brk(read_rows)
        s.brk(APP_ENTRY)
        s.brk(poll)

        for _ in range(MAX_STOPS):
            at = s.stopped_at(s.run())
            if at == read_rows:
                self.matrix.inject(s)
                continue
            if at == APP_ENTRY:
                return "app"
            if at == poll:
                return "isp"
            return f"lost at {at:#06x}" if at is not None else "never stopped"
        return "still scanning"


class TestChord(unittest.TestCase):
    def run_with(self, pressed):
        run = BootRun(pressed)
        try:
            return run.outcome()
        finally:
            run.close()

    def test_no_keys_held_starts_the_application(self):
        self.assertEqual(self.run_with([]), "app")

    def test_the_chord_holds_the_bootloader_in_isp_mode(self):
        self.assertEqual(self.run_with(chord_keys()), "isp")

    def test_part_of_the_chord_is_not_enough(self):
        keys = chord_keys()
        self.assertGreater(len(keys), 1, "a one-key chord cannot be partially held")
        self.assertEqual(self.run_with(keys[:-1]), "app")

    def test_a_key_outside_the_chord_does_nothing(self):
        keys = chord_keys()
        other = next((col, row) for col in range(16) for row in range(5) if (col, row) not in keys)
        self.assertEqual(self.run_with([other]), "app")


class TestIspEntry(unittest.TestCase):
    """The door at ISP_ENTRY, which is how a running application (and so
    sinowisp) gets here: `ljmp ISP_ENTRY` with A = 0x5a and B = 0xa5."""

    ISP_ENTRY = 0xFF00

    def enter(self, acc, reg_b):
        poll = load_symbols(Path(SIM.firmware).with_suffix(".map"))["boot_usb_poll"]
        out = SIM.run([
            "reset",
            f"set mem xram 0x{PIN_STAGE_P5:x} 0xff",
            f"set mem xram 0x{PIN_STAGE_P7:x} 0xff",
            "set mem rom 0x0000 0x02 0xf0 0x00",
            f"set mem rom 0x{APP_VECTOR_ADDR:x} 0x02 0x90 0x00",
            f"set mem rom 0x{APP_ENTRY:x} 0x80 0xfe",
            f"set mem sfr 0x{ACC:x} 0x{acc:02x}",
            f"set mem sfr 0x{REG_B:x} 0x{reg_b:02x}",
            f"pc 0x{self.ISP_ENTRY:x}",
            f"break 0x{APP_ENTRY:x}",
            f"break 0x{poll:x}",
            "run",
        ])
        stops = re.findall(r"Stop at 0x([0-9a-fA-F]+)", out)
        self.assertTrue(stops, f"never stopped; sim output:\n{out[-2000:]}")
        return "isp" if int(stops[-1], 16) == poll else "app"

    def test_the_key_holds_the_bootloader_in_isp_mode(self):
        """No chord, an enabled application, and it still stays: the request is
        what sinowisp relies on to reflash a running board."""
        self.assertEqual(self.enter(ISP_KEY_A, ISP_KEY_B), "isp")

    def test_a_wrong_key_just_reboots(self):
        self.assertEqual(self.enter(ISP_KEY_A, 0x00), "app")


class TestNoApplication(unittest.TestCase):
    def test_a_blank_application_leaves_the_bootloader_in_isp_mode(self):
        """Nothing at the vector means nothing to run, chord or not; this is what
        keeps a half-written board recoverable."""
        run = BootRun([])
        try:
            s = run.sess
            s.cmd("reset")
            s.cmd("set mem rom 0x0000 0x02 0xf0 0x00")
            s.cmd(f"set mem rom 0x{APP_VECTOR_ADDR:x} 0x00 0x00 0x00")

            read_rows = run.syms["user_matrix_read_rows"]
            poll = run.syms["boot_usb_poll"]
            s.brk(read_rows)
            s.brk(poll)

            for _ in range(MAX_STOPS):
                at = s.stopped_at(s.run())
                if at == read_rows:
                    run.matrix.inject(s)
                    continue
                self.assertEqual(at, poll)
                return
            self.fail("bootloader never reached its main loop")
        finally:
            run.close()


if __name__ == "__main__":
    unittest.main()
