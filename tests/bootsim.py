"""Drive the SMK ISP bootloader in the patched uCsim simulator.

The application's harness in `sim.py` injects a SETUP packet and lets the USB
interrupt vector to the firmware's ISR. The bootloader claims no vectors -- it
polls, so that the application keeps every one of them -- so this drives it a
main-loop iteration at a time instead, breaking at `boot_usb_poll` and letting
the simulator's SIE model raise the next completion each time round.
"""

import os
import re
import subprocess
from pathlib import Path

from sim import find_sim, load_symbols, read_intel_hex

REPO_ROOT = Path(__file__).resolve().parent.parent

EP0_OUT_BUF = 0x1100
USBIF1, USBIF2 = 0x92, 0x93
SETUPIF, OEP0IF = 0x10, 0x10

# cells the SH68F90 model reads as the external pin level of P5 / P7 (sh68f90.cc)
PIN_STAGE_P5, PIN_STAGE_P7 = 0x1F15, 0x1F17

ACC, REG_B = 0xE0, 0xF0
ISP_KEY_A, ISP_KEY_B = 0x5A, 0xA5

DESC_DEVICE = 0x01
DESC_CONFIGURATION = 0x02
DESC_STRING = 0x03
DESC_HID = 0x21
DESC_REPORT = 0x22

REPORT_TYPE_FEATURE = 0x03


def find_bootloader():
    env = os.environ.get("SMK_BOOTLOADER")
    if env and Path(env).exists():
        return env
    candidates = (sorted(Path.cwd().glob("*_boot.hex"))
                  + sorted(REPO_ROOT.glob("build/*_boot.hex"))
                  + sorted(REPO_ROOT.glob("*_boot.hex")))
    for c in candidates:  # prefer nuphy-air60 (the reference target)
        if "nuphy" in c.name:
            return str(c)
    return str(candidates[0]) if candidates else None


def get_descriptor(desc_type, index=0, length=255, recipient=0x80):
    return [recipient, 0x06, index, desc_type, 0x00, 0x00, length & 0xFF, (length >> 8) & 0xFF]


def get_feature_report(report_id, length):
    return [0xA1, 0x01, report_id, REPORT_TYPE_FEATURE, 0x00, 0x00, length & 0xFF, (length >> 8) & 0xFF]


def set_feature_report(report_id, length):
    return [0x21, 0x09, report_id, REPORT_TYPE_FEATURE, 0x00, 0x00, length & 0xFF, (length >> 8) & 0xFF]


class BootSim:
    def __init__(self, firmware=None, sim=None):
        self.sim = sim or find_sim()
        self.firmware = firmware or find_bootloader()
        if self.firmware and Path(self.firmware).exists():
            syms = load_symbols(Path(self.firmware).with_suffix(".map"))
            self.POLL = syms["boot_usb_poll"]
            self.rom = read_intel_hex(self.firmware)

    def available(self):
        if not self.firmware or not Path(self.firmware).exists():
            return "no bootloader .hex (build it: meson compile -C build nuphy-air60_boot.hex)"
        try:
            subprocess.run([self.sim, "-v"], capture_output=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            return f"patched simulator not runnable ({self.sim}); build it: nix build .#ucsim-sh68f90"
        return None

    def run(self, commands, timeout=120):
        script = "\n".join([f'file "{self.firmware}"'] + commands + ["quit"]) + "\n"
        p = subprocess.run(
            [self.sim, "-t", "sh68f90", "-S", "in=/dev/null"],
            input=script, capture_output=True, text=True, timeout=timeout,
        )
        return p.stdout + p.stderr

    @staticmethod
    def _set_xram(addr, data):
        return f"set mem xram 0x{addr:x} " + " ".join(f"0x{b:02x}" for b in data)

    def _boot_to_loop(self):
        """Reset and run the real startup -- chord scan, USB attach -- stopping on
        the first main-loop poll.

        The rows are staged idle-high, because the model's pin cells default low
        and a low row is a pressed key. What keeps the bootloader here is the
        absent application: a bootloader-only image has no reset vector at
        APP_VECTOR_ADDR to jump to."""
        return [
            "reset",
            f"set mem xram 0x{PIN_STAGE_P5:x} 0xff",
            f"set mem xram 0x{PIN_STAGE_P7:x} 0xff",
            f"break 0x{self.POLL:x}",
            "run",
        ]

    def _pump(self, iterations):
        # each `run` resumes into one poll() and stops at the next loop entry
        return ["run"] * iterations

    def control_in(self, setup, iterations=40):
        """One control-IN transfer; the SIE logs each EP0 IN packet."""
        return self.run(self._boot_to_loop()
                        + [self._set_xram(EP0_OUT_BUF, setup),
                           f"set mem sfr 0x{USBIF1:x} 0x{SETUPIF:02x}"]
                        + self._pump(iterations))

    def read_page(self, addr, page_size, iterations=400):
        """INIT_READ at `addr`, then one page report; returns the reassembled bytes."""
        init_read = [0x05, 0x52, addr & 0xFF, (addr >> 8) & 0xFF, 0x00, 0x00]

        cmds = self._boot_to_loop() + [
            self._set_xram(EP0_OUT_BUF, set_feature_report(0x05, len(init_read))),
            f"set mem sfr 0x{USBIF1:x} 0x{SETUPIF:02x}",
            "run",
            self._set_xram(EP0_OUT_BUF, init_read),
            f"set mem sfr 0x{USBIF2:x} 0x{OEP0IF:02x}",
            "run",
            "run",
            self._set_xram(EP0_OUT_BUF, get_feature_report(0x06, page_size + 2)),
            f"set mem sfr 0x{USBIF1:x} 0x{SETUPIF:02x}",
        ] + self._pump(iterations)
        return self.reassemble(self.run(cmds))

    @staticmethod
    def in_packets(output):
        return [[int(x, 16) for x in m.group(1).split()]
                for m in re.finditer(r"\[SIE\] EP0 IN\[\d+\] \d+ bytes:((?: [0-9a-f]{2})*)", output)]

    @classmethod
    def reassemble(cls, output):
        return [b for pkt in cls.in_packets(output) for b in pkt]
