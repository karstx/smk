"""Drive the patched uCsim 8051 simulator to exercise SMK's USB firmware.

Requires the patched simulator (flake package `ucsim-sh68f90`; `ucsim_51` on PATH
in `nix develop`) and a built firmware `.hex`. The patch registers the SH68F90
USB interrupt (vector 7) plus a minimal SIE model that logs each EP0 IN packet
the firmware produces -- this module injects control SETUP packets and parses
that output. See tools/ucsim/ for the patch and a shell reproducer.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def find_sim():
    # The patched simulator installs as `ucsim-sh68f90` (distinct from sdcc's
    # stock, unpatched `ucsim_51`).
    env = os.environ.get("SMK_UCSIM")
    if env:
        return env
    found = shutil.which("ucsim-sh68f90")
    if found:
        return found
    local = REPO_ROOT / "result" / "bin" / "ucsim-sh68f90"
    return str(local) if local.exists() else "ucsim-sh68f90"


def find_firmware():
    env = os.environ.get("SMK_FIRMWARE")
    if env and Path(env).exists():
        return env
    # cwd covers `meson test` (run from the build dir); REPO_ROOT/build covers
    # manual runs from the repo root.
    candidates = (sorted(Path.cwd().glob("*_smk.hex"))
                  + sorted(REPO_ROOT.glob("build/*_smk.hex"))
                  + sorted(REPO_ROOT.glob("*_smk.hex")))
    for c in candidates:  # prefer nuphy-air60 (the reference target)
        if "nuphy" in c.name:
            return str(c)
    return str(candidates[0]) if candidates else None


def load_symbols(map_path):
    """Parse SDCC's `.map` file. Returns {name_without_leading_underscore: addr}.
    Lines look like: `D:   00000061  _keyboard_state                    keyboard`
    (D = data/xram, C = code, plus other area letters)."""
    pat = re.compile(r"^[A-Z]:\s+([0-9A-Fa-f]+)\s+_(\S+)")
    syms = {}
    with open(map_path) as f:
        for line in f:
            m = pat.match(line)
            if m:
                syms[m.group(2)] = int(m.group(1), 16)
    return syms


def load_lines(cdb_path):
    """Parse SDCC's `.cdb` C-line records into {(filename, lineno): [addr, ...]},
    the source-level breakpoint map. Records look like `L:C$main.c$102$1_0$75:2EC`
    = `L:C$<file>$<line>$<level>_<block>$<block>:<code-address-hex>`.

    A line can map to several addresses (loop headers, multi-statement lines), so
    all are kept sorted; `addr_of_line` takes the lowest by default. The `.cdb` is
    emitted by SDCC's `--debug` (meson `buildtype=debug`) alongside the
    `.hex`/`.map`; `--debug` doesn't change codegen, so addresses match the .hex.
    """
    pat = re.compile(r"^L:C\$([^$]+)\$(\d+)\$[0-9_]+\$[0-9A-Fa-f]+:([0-9A-Fa-f]+)")
    lines = {}
    with open(cdb_path) as f:
        for line in f:
            m = pat.match(line)
            if m:
                key = (m.group(1), int(m.group(2)))
                lines.setdefault(key, []).append(int(m.group(3), 16))
    for key in lines:
        lines[key] = sorted(set(lines[key]))
    return lines


def read_intel_hex(hex_path):
    """Parse Intel HEX into a flat {addr: byte} dict (data records only)."""
    mem = {}
    with open(hex_path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            n, addr, rec = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
            if rec == 0:
                for i in range(n):
                    mem[addr + i] = int(line[9 + 2 * i:11 + 2 * i], 16)
    return mem


def find_post_init(syms, hex_path):
    """The address right after the first `LCALL _init` instruction in `_main`.
    `_main` itself isn't a stable breakpoint for "post-init" because main() may
    call other functions first, so we scan the first ~64 bytes of main() for the
    3-byte opcode `12 hi lo` matching _init."""
    init_addr = syms["init"]
    target = (0x12, (init_addr >> 8) & 0xFF, init_addr & 0xFF)
    mem = read_intel_hex(hex_path)
    base = syms["main"]
    for off in range(64):
        if tuple(mem.get(base + off + i) for i in range(3)) == target:
            return base + off + 3
    raise RuntimeError(
        f"could not locate `LCALL _init` (opcode {target[0]:02x} {target[1]:02x} "
        f"{target[2]:02x}) within 64 bytes of _main=0x{base:x}"
    )


def get_descriptor(desc_type, index=0, length=64):
    """Build an 8-byte GET_DESCRIPTOR control-IN SETUP packet."""
    return [0x80, 0x06, index, desc_type, 0x00, 0x00, length & 0xFF, (length >> 8) & 0xFF]


DESC_DEVICE = 0x01
DESC_CONFIGURATION = 0x02
DESC_STRING = 0x03

# HID class request / report constants (src/smk/usbdef.h)
REPORT_ID_ISP = 0x05
REPORT_TYPE_FEATURE = 0x03
REPORT_TYPE_OUTPUT = 0x02


# --- SETUP packet builders (bmRequestType, bRequest, wValueLo, wValueHi,
#     wIndexLo, wIndexHi, wLengthLo, wLengthHi) -------------------------------
def set_address(addr):
    return [0x00, 0x05, addr & 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00]


def set_configuration(cfg):
    return [0x00, 0x09, cfg & 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00]


def get_configuration():
    return [0x80, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00]


def get_status_device():
    return [0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00]


def hid_set_idle(duration):  # wValue = duration<<8 | report_id(0)
    return [0x21, 0x0A, 0x00, duration & 0xFF, 0x00, 0x00, 0x00, 0x00]


def hid_get_idle():
    return [0xA1, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00]


def hid_set_protocol(proto, iface=0):  # wValue = protocol
    return [0x21, 0x0B, proto & 0xFF, 0x00, iface & 0xFF, 0x00, 0x00, 0x00]


def hid_get_protocol(iface=0):
    return [0xA1, 0x03, 0x00, 0x00, iface & 0xFF, 0x00, 0x01, 0x00]


def set_report_output(report_id=0, length=1):  # SET_REPORT(Output) -- LED report
    return [0x21, 0x09, report_id & 0xFF, REPORT_TYPE_OUTPUT,
            0x00, 0x00, length & 0xFF, (length >> 8) & 0xFF]


def set_report_isp_setup():
    """SET_REPORT(Feature, REPORT_ID_ISP) SETUP packet -- the firmware's path into
    the ISP bootloader."""
    # bmRequestType=0x21 (OUT|Class|Interface), bRequest=0x09 (SET_REPORT),
    # wValue=0x0305 (Feature<<8 | ISP id), wIndex=1 (iface), wLength=5
    return [0x21, 0x09, REPORT_ID_ISP, REPORT_TYPE_FEATURE, 0x01, 0x00, 0x05, 0x00]


class Sim:
    # SH68F90 SFR addresses / bits (see src/sino51lib/sh68f90/sh68f90.h)
    IE, IEN1, USBADDR, USBIF1, USBIF2, EP0CON, OEP0CNT = 0xA8, 0xA9, 0x96, 0x92, 0x93, 0x97, 0x9E
    SP_SFR = 0x81
    EP0_OUT_BUF = 0x1100
    EA, EUSB, SETUPIF, OEP0IF = 0x80, 0x01, 0x10, 0x10
    USB_VECTOR = 0x3B
    ISP_BOOTLOADER = 0xFF00  # isp_jump() does: clr EA; B=0xa5; A=0x5a; ljmp 0xff00
    ISP_MAGIC_ACC, ISP_MAGIC_B = 0x5A, 0xA5
    SLED_END = 0x900E  # break address at the end of the 16-byte NOP sled

    STATE_DEFAULT, STATE_ADDRESSED, STATE_CONFIGURED = 0, 1, 2

    def __init__(self, firmware=None, sim=None):
        self.sim = sim or find_sim()
        self.firmware = firmware or find_firmware()
        # Firmware globals / code addresses are derived from the SDCC .map (and
        # the .hex for POST_INIT); they shift whenever the firmware is rebuilt.
        # Only resolve when the firmware exists -- otherwise the test suite skips
        # via `available()` and these attrs are never read.
        if self.firmware and Path(self.firmware).exists():
            self._resolve_firmware_addresses()

    def _resolve_firmware_addresses(self):
        s = load_symbols(Path(self.firmware).with_suffix(".map"))
        # Source-line -> address map from the SDCC .cdb (for source-level
        # breakpoints). Optional: only debug builds emit it, so tests that want
        # line breakpoints should guard with `has_lines()`.
        cdb = Path(self.firmware).with_suffix(".cdb")
        self.lines = load_lines(cdb) if cdb.exists() else {}
        self.LED_STATE = s["keyboard_state"]      # struct, led_state is field 0
        self.USB_DEVICE_STATE = s["usb_device_state"]
        self.USB_TASK = s["usb_task"]
        # POST_INIT can't be a single symbol -- it's the address after the LCALL
        # _init inside main(). find_post_init() walks main()'s prologue to find it.
        self.POST_INIT = find_post_init(s, self.firmware)

    def available(self):
        """Return None if runnable, else a human reason it is not."""
        if not self.firmware or not Path(self.firmware).exists():
            return "no SMK firmware .hex (build it: meson compile -C build nuphy-air60_default_smk.hex)"
        try:
            subprocess.run([self.sim, "-v"], capture_output=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            return f"patched simulator not runnable ({self.sim}); build it: nix build .#ucsim-sh68f90"
        return None

    # --- source-level addressing (from the .cdb) --------------------------
    def has_lines(self):
        """True if the .cdb source-line map loaded (i.e. a debug build)."""
        return bool(getattr(self, "lines", None))

    def addr_of_line(self, filename, lineno, exact=False):
        """Code address of a source line -- the lowest it maps to. `filename` is
        the basename as it appears in the .cdb ('main.c', ...).

        Not every line emits code (blanks, folded statements). By default, gdb-
        style, the breakpoint moves forward to the next line with code (reported by
        `actual_line`); exact=True requires the requested line. Raises KeyError if
        no code is found."""
        if (filename, lineno) in self.lines:
            return self.lines[(filename, lineno)][0]
        if not exact:
            later = sorted(ln for (f, ln) in self.lines if f == filename and ln >= lineno)
            if later:
                return self.lines[(filename, later[0])][0]
        raise KeyError(
            f"no code at or after {filename}:{lineno} in "
            f"{Path(self.firmware).with_suffix('.cdb').name} "
            f"(unknown file, or past the last statement?)"
        )

    def actual_line(self, filename, lineno):
        """The line a `break filename:lineno` actually lands on (>= lineno), after
        gdb-style forward adjustment to the next line that has code."""
        if (filename, lineno) in self.lines:
            return lineno
        later = sorted(ln for (f, ln) in self.lines if f == filename and ln >= lineno)
        if not later:
            raise KeyError(f"no code at or after {filename}:{lineno}")
        return later[0]

    def break_line(self, filename, lineno):
        """A uCsim `break` command for a source line (use within a command list)."""
        return f"break 0x{self.addr_of_line(filename, lineno):x}"

    def run(self, commands, timeout=30):
        # -t sh68f90: the SH68F90 uCsim CPU variant (8052 core + the chip's on-die
        # peripherals: USB SIE, timers, flash ISP, GPIO, watchdog, interrupts). The
        # plain 8052 (-t 52) lacks those, so booting from reset / USB tests need
        # this model.
        script = "\n".join([f'file "{self.firmware}"'] + commands + ["quit"]) + "\n"
        p = subprocess.run(
            [self.sim, "-t", "sh68f90", "-S", "in=/dev/null"],
            input=script, capture_output=True, text=True, timeout=timeout,
        )
        return p.stdout + p.stderr

    # --- stimulus ---------------------------------------------------------
    @staticmethod
    def _set_xram(addr, data):
        return f"set mem xram 0x{addr:x} " + " ".join(f"0x{b:02x}" for b in data)

    def fire_vector(self):
        """Boot through real init, then raise USBIF1.SETUPIF and break at the USB
        vector. Captures SP before and after the vector accepts the IRQ so the
        push of the return address can be verified (post-boot SP isn't a fixed
        value, so we measure the delta rather than an absolute)."""
        cmds = self._boot_to_post_init() + [
            "echo ===PRE_IRQ===",
            f"dump sfr 0x{self.SP_SFR:x} 0x{self.SP_SFR:x}",
            f"set mem sfr 0x{self.USBIF1:x} 0x{self.SETUPIF:02x}",
            f"break 0x{self.USB_VECTOR:x}",
            "run",
            "echo ===POST_IRQ===",
            f"dump sfr 0x{self.SP_SFR:x} 0x{self.SP_SFR:x}",
        ]
        return self.run(cmds)

    def _ctrl(self, setup):
        """Commands for one control transfer: stage the SETUP, request the
        interrupt, reset PC to the sled top, and run to the sled break."""
        assert len(setup) == 8
        return [
            self._set_xram(self.EP0_OUT_BUF, setup),
            f"set mem sfr 0x{self.USBIF1:x} 0x{self.SETUPIF:02x}",
            "pc 0x9000",
            f"break 0x{self.SLED_END:x}",
            "run",
        ]

    def control_in(self, setup):
        """Boot through real init, then run a single control transfer; the SIE
        logs the EP0 IN packets."""
        return self.run(self._boot_to_post_init() + self._ctrl(setup))

    def controls(self, *setups):
        """Boot through real init, then run several control transfers in one
        session (state persists across them)."""
        cmds = self._boot_to_post_init()
        for s in setups:
            cmds += self._ctrl(s)
        return self.run(cmds)

    def set_address(self, addr):
        """Boot, SET_ADDRESS, then read USBADDR (committed during the status stage)."""
        cmds = self._boot_to_post_init() + self._ctrl(set_address(addr)) + [
            f"dump sfr 0x{self.USBADDR:x} 0x{self.USBADDR:x}",
        ]
        return self.dumped_byte(self.run(cmds))

    def led_report(self, value):
        """Boot, then SET_REPORT(Output) + OUT data stage carrying the LED byte;
        returns the firmware's keyboard_state.led_state afterwards."""
        cmds = self._boot_to_post_init() + self._ctrl(set_report_output(length=1)) + [
            # OUT data stage -> usb_ep0_out_irq (LED branch) -> led_state = buf[0]
            self._set_xram(self.EP0_OUT_BUF, [value]),
            f"set mem sfr 0x{self.OEP0CNT:x} 0x01",
            f"set mem sfr 0x{self.USBIF2:x} 0x{self.OEP0IF:02x}",
            "pc 0x9000",
            f"break 0x{self.SLED_END:x}",
            "run",
            f"dump xram 0x{self.LED_STATE:x} 0x{self.LED_STATE:x}",
        ]
        return self.dumped_byte(self.run(cmds))

    def _boot_to_post_init(self):
        """Boot from reset through the real init path and stop right after init()
        returns (USB fully initialized, EA set), then park on a NOP sled so
        control transfers can be injected against that real state."""
        return [
            "reset",
            f"break 0x{self.POST_INIT:x}",
            "run",
            "delete",  # clear the post-init breakpoint
            "set mem rom 0x9000 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
            "pc 0x9000",
        ]

    def boot_post_init_state(self):
        """Boot through real init; return usb_device_state right after init()."""
        cmds = [
            "reset",
            f"break 0x{self.POST_INIT:x}",
            "run",
            f"dump xram 0x{self.USB_DEVICE_STATE:x} 0x{self.USB_DEVICE_STATE:x}",
        ]
        return self.dumped_byte(self.run(cmds))

    def boot_enumerate(self, *setups):
        """Boot through real init, then run control transfers against the real
        initialized state. Returns sim output (read with dump_value)."""
        cmds = self._boot_to_post_init()
        for s in setups:
            cmds += self._ctrl(s)
        cmds += [
            f"dump xram 0x{self.USB_DEVICE_STATE:x} 0x{self.USB_DEVICE_STATE:x}",
            f"dump sfr 0x{self.USBADDR:x} 0x{self.USBADDR:x}",
        ]
        return self.run(cmds)

    def overflow_stack(self, marker=0xAA):
        """Deliberately overflow the firmware's stack (base 0x85, 122 bytes
        0x86-0xFF) with a PUSH/SJMP loop. The push past 0xFF wraps the 8-bit SP
        to 0x00, silently corrupting register bank 0 -- the 8051 has no hardware
        stack protection. uCsim's own stack tracking flags it ("Stack overflow")
        and halts. Returns sim output."""
        cmds = [
            "reset",
            "set mem rom 0x9000 0xc0 0xe0 0x80 0xfc",  # loop: PUSH ACC; SJMP $-2
            f"set mem sfr 0xe0 0x{marker:02x}",        # ACC = marker byte
            "set mem iram 0x00 0x00",                  # clear register-bank-0 byte R0
            "set mem sfr 0x81 0x85",                   # SP = firmware stack base
            "pc 0x9000",
            "run",                                     # uCsim halts at the overflow
            "dump sfr 0x81 0x81",
            "dump iram 0x00 0x00",
        ]
        return self.run(cmds)

    def trigger_isp_jump(self, confirm=(0x05, 0x75), phase2_break=None, run_task=True):
        """Perform the SET_REPORT(ISP) control transfer that makes the firmware
        jump into the ISP bootloader: the SETUP expects an OUT data stage, which
        carries the confirm bytes that gate the jump. Returns sim output
        including `info registers`.

        The ISR only records the request, once the host has read its status
        stage, so phase 3 enters usb_task() the way the main loop does and that
        is where isp_jump() runs. run_task=False stops after the ISR for the
        negative case, where no jump should happen and the ISR returns to the
        NOP sled."""
        if phase2_break is None:
            phase2_break = self.SLED_END
        out_data = list(confirm) + [0] * (8 - len(confirm))
        cmds = self._boot_to_post_init() + [
            # phase 1: SET_REPORT(Feature, ISP) SETUP -> EP0 waits for the data stage
            self._set_xram(self.EP0_OUT_BUF, set_report_isp_setup()),
            f"set mem sfr 0x{self.USBIF1:x} 0x{self.SETUPIF:02x}",
            f"break 0x{self.SLED_END:x}",
            "run",
            # phase 2: OUT data stage -> usb_ep0_out_irq -> usb_isp_requested.
            # reset PC to the sled top so the ISR return lands back on the sled
            # and reaches the break (rather than sliding past it).
            self._set_xram(self.EP0_OUT_BUF, out_data),
            f"set mem sfr 0x{self.OEP0CNT:x} 0x{set_report_isp_setup()[6]:02x}",
            f"set mem sfr 0x{self.USBIF2:x} 0x{self.OEP0IF:02x}",
            "pc 0x9000",
            f"break 0x{phase2_break:x}",
            "run",
        ]
        if run_task:
            # phase 3: the main loop calls usb_task(), which is what jumps.
            cmds += [
                f"pc 0x{self.USB_TASK:x}",
                f"break 0x{self.ISP_BOOTLOADER:x}",
                "run",
            ]
        cmds += ["info registers"]
        return self.run(cmds)

    # --- parsing ----------------------------------------------------------
    @staticmethod
    def in_packets(output):
        """All EP0 IN packets captured by the SIE model, as lists of byte ints."""
        out = []
        for m in re.finditer(r"\[SIE\] EP0 IN\[\d+\] \d+ bytes:((?: [0-9a-f]{2})*)", output):
            out.append([int(x, 16) for x in m.group(1).split()])
        return out

    @classmethod
    def reassemble(cls, output):
        """Concatenate the EP0 IN packets into one byte list (the full transfer)."""
        return [b for pkt in cls.in_packets(output) for b in pkt]

    @staticmethod
    def ep1_report(output):
        """The last EP1 (keyboard) report the SIE captured, as a byte list."""
        ms = re.findall(r"\[SIE\] EP1 IN \d+ bytes:((?: [0-9a-f]{2})*)", output)
        return [int(x, 16) for x in ms[-1].split()] if ms else []

    @staticmethod
    def dumped_byte(output):
        """Value of the last `dump` (uCsim prints `0x<addr>   <byte> ...`)."""
        ms = re.findall(r"^0x[0-9a-fA-F]+\s+([0-9a-fA-F]{2})", output, re.M)
        return int(ms[-1], 16) if ms else None

    @staticmethod
    def dump_in_section(output, start_marker, addr, end_marker=None):
        """Last dump value of `addr` in the segment after `start_marker` (and
        before `end_marker`, if given). Lets a single sim run capture the same
        register/cell at multiple points (e.g. SP before and after an IRQ).
        Handles both uCsim dump formats: unnamed/xram (`0xADDR  HH HH ...`) and
        named SFR (`0xADDR NAME:    0b<bits> 0xHH 'c' 135 (-121)`)."""
        seg = output.split(start_marker, 1)[-1]
        if end_marker:
            seg = seg.split(end_marker, 1)[0]
        # After `0xADDR` on the line, accept either: (a) whitespace then 2 hex
        # digits (unnamed SFR / xram), or (b) anything-then-`0x` then 2 hex digits
        # (named SFR -- 0xHH appears after the binary representation).
        pat = rf"^0x0*{addr:x}\b(?:[^\n]*?\b0x|[ \t]+)([0-9a-fA-F]{{2}})\b"
        ms = re.findall(pat, seg, re.M | re.I)
        return int(ms[-1], 16) if ms else None

    @staticmethod
    def dump_value(output, addr):
        """Value of the LAST `dump` for a specific address (sfr prints 2 hex
        digits, xram 4). Last match because `set mem` echoes the same address."""
        for pat in (rf"0x0*{addr:x}\b", rf"0x{addr:04x}", rf"0x{addr:02x}"):
            ms = re.findall(rf"^{pat}\s+([0-9a-fA-F]{{2}})", output, re.M | re.I)
            if ms:
                return int(ms[-1], 16)
        return None

    @staticmethod
    def _last(pattern, output):
        # a command sequence can have several runs / register dumps; the final
        # state is the last match
        matches = re.findall(pattern, output)
        return int(matches[-1], 16) if matches else None

    @classmethod
    def stopped_at(cls, output):
        return cls._last(r"Stop at 0x([0-9a-fA-F]+)", output)

    @classmethod
    def sp(cls, output):
        return cls._last(r"\bSP 0x([0-9a-fA-F]+)", output)

    @classmethod
    def acc(cls, output):
        return cls._last(r"\bACC= 0x([0-9a-fA-F]+)", output)

    @classmethod
    def reg_b(cls, output):
        return cls._last(r"\bB= 0x([0-9a-fA-F]+)", output)
