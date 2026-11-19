#!/usr/bin/env python3
"""Pre-flash verifier for the ISY IGM3000 firmware.

usage: python3 verify.py build/isy-igm3000_smk.ihx build/isy-igm3000_smk.map [--quick]

Static checks on the image, then emulated scenarios (see emu51.py). Exit code 0
only if every check passes. This is a sanity net, not a guarantee: the emulator
models the USB block and pins from the register names, not from silicon.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dis51  # noqa: E402
from emu51 import CPU, Halt, Host, load_ihex, FREQ  # noqa: E402

APP_LIMIT = 0x6FFB  # sinowisp/bootloader use 0x6FFB..0x6FFF for the LJMP to the app
ISP_JUMP_BYTES = bytes.fromhex("c2af75f0a5745a027f00")
FLASH_SFRS = {0xF2: "IB_CON1", 0xF3: "IB_CON2", 0xF4: "IB_CON3", 0xF5: "IB_CON4", 0xF6: "IB_CON5",
              0xFB: "IB_OFFSET", 0xFC: "IB_DATA"}

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{(' - ' + detail) if detail else ''}")
    return ok


def load_map(path):
    syms = {}
    for line in open(path):
        m = re.match(r"\s*C:\s+([0-9A-F]{8})\s+(_\w+)", line)
        if m:
            syms[m.group(2)] = int(m.group(1), 16)
    return syms


# ---------------------------------------------------------------- HID parsing

def parse_hid(desc):
    """Return ({(type, id): bits}, error or None). Types: input/output/feature."""
    reports = {}
    rid, rsize, rcount = 0, 0, 0
    depth = 0
    i = 0
    while i < len(desc):
        p = desc[i]
        if p == 0xFE:
            return reports, f"long item at {i}"
        size = (0, 1, 2, 4)[p & 3]
        if i + 1 + size > len(desc):
            return reports, f"truncated item at {i}"
        data = int.from_bytes(desc[i + 1:i + 1 + size], "little")
        tag = p & 0xFC
        if tag == 0x84:
            rid = data
        elif tag == 0x74:
            rsize = data
        elif tag == 0x94:
            rcount = data
        elif tag in (0x80, 0x90, 0xB0):
            kind = {0x80: "input", 0x90: "output", 0xB0: "feature"}[tag]
            reports[(kind, rid)] = reports.get((kind, rid), 0) + rsize * rcount
        elif tag == 0xA0:
            depth += 1
        elif tag == 0xC0:
            depth -= 1
            if depth < 0:
                return reports, f"END_COLLECTION underflow at {i}"
        i += 1 + size
    if depth != 0:
        return reports, f"unbalanced collections ({depth})"
    return reports, None


# ---------------------------------------------------------------- static

def static_checks(img, used, syms):
    hi = max(b for a, b in used)
    check("image fits below 0x6FFB (no overlap with app LJMP slot / bootloader)", hi <= APP_LIMIT,
          f"highest byte at 0x{hi - 1:04x}")
    check("reset vector is LJMP (bootloader redirects bytes 1..2)", img[0] == 0x02,
          f"0x0000 = {img[0]:02x} -> 0x{img[1] << 8 | img[2]:04x}")
    a = syms.get("_isp_jump")
    check("isp_jump = clr EA; mov B,#A5; mov A,#5A; ljmp 7F00 (same as stock)",
          a is not None and img[a:a + 10] == ISP_JUMP_BYTES,
          f"at 0x{a:04x}: {img[a:a + 10].hex()}" if a is not None else "symbol missing")

    def ljmp_target(v):
        return img[v + 1] << 8 | img[v + 2] if img[v] == 0x02 else None

    check("Timer2 vector 0x2B -> timer2_isr", ljmp_target(0x2B) == syms.get("_timer2_isr"))
    check("USB vector 0x5B -> usb_interrupt_handler", ljmp_target(0x5B) == syms.get("_usb_interrupt_handler"))

    # flash programming registers must never be written
    entries = [s for n, s in syms.items() if s < 0x7000 and not n.startswith(("_s_", "_l_"))]
    entries += [0x0000] + [3 + 8 * n for n in range(15)]
    code, _, _ = dis51.analyze(img + b"\x00" * 4, entries, APP_LIMIT)
    bad = []
    for pc in code:
        op = img[pc]
        dst = None
        if op in (0x75, 0xF5, 0x05, 0x15, 0xD0, 0xD5, 0xC5, 0x42, 0x43, 0x52, 0x53, 0x62, 0x63) or 0x86 <= op <= 0x8F:
            dst = img[pc + 1]
        elif op == 0x85:
            dst = img[pc + 2]
        if dst in FLASH_SFRS:
            bad.append(f"{pc:04x}:{FLASH_SFRS[dst]}")
    check("no writes to flash-programming SFRs (IB_CON*/IB_OFFSET/IB_DATA)", not bad, ", ".join(bad[:5]))
    check("no illegal 0xA5 opcode reachable", not any(img[pc] == 0xA5 for pc in code))
    print(f"       ({len(code)} instructions reachable, {hi} bytes of image)")


# ---------------------------------------------------------------- dynamic

IDLE_PC = None


def find_idle_pc(rst_dir):
    # main loop: 'jbc _ms_flag,...' followed by the sjmp taken when idle
    lines = open(Path(rst_dir) / "main.rst").read().splitlines()
    for i, l in enumerate(lines):
        if re.search(r"\bjbc\s+_ms_flag", l):
            m = re.match(r"\s*([0-9A-F]{6})\s+((?:[0-9A-F]{2} )+)", l)
            if m:
                return int(m.group(1), 16) + len(m.group(2).split())
    return None


def boot(img, sensor=None, buttons=(), ms=0):
    cpu = CPU(img, sensor, idle_pc=IDLE_PC)
    for b in buttons:
        cpu.buttons_low.add(b)
    if ms:
        cpu.run(int(ms * FREQ / 1000))
    return cpu


def run_until_isp(cpu, ms):
    try:
        cpu.run(int(ms * FREQ / 1000))
    except Halt:
        return True
    return False


def isp_ok(cpu):
    if not cpu.isp_hit:
        return False, "never reached 0x7F00"
    a, b, ea, cyc = cpu.isp_hit
    return (a == 0x5A and b == 0xA5 and ea == 0), f"A={a:02x} B={b:02x} EA={ea} at {cyc * 1000 / FREQ:.0f} ms"


P2, P6 = 0xA0, 0x90


def scenario_boot_chord(img):
    print("\n-- scenario: two buttons held while plugging in")
    cpu = boot(img, buttons=[(P2, 0, 1), (P2, 0, 2)])
    hit = run_until_isp(cpu, 200)
    ok, d = isp_ok(cpu)
    check("2 buttons at power-on -> ISP bootloader within 200 ms", hit and ok, d)
    cpu = boot(img, buttons=[(P2, 0, 1)])
    hit = run_until_isp(cpu, 300)
    check("1 button at power-on -> normal boot (no ISP)", not hit)


def scenario_no_host(img):
    print("\n-- scenario: no USB host ever enumerates (broken-USB safety net)")
    cpu = boot(img)
    hit = run_until_isp(cpu, 12000)
    ok, d = isp_ok(cpu)
    check("never configured -> ISP bootloader after ~10 s", hit and ok, d)
    if cpu.isp_hit:
        t = cpu.isp_hit[3] * 1000 / FREQ
        check("fallback fires between 9.5 and 11 s", 9500 <= t <= 11000, f"{t:.0f} ms")
    gap = cpu.max_kick_gap * 1000 / FREQ
    check("watchdog kicked at least every 20 ms", gap < 20, f"max gap {gap:.2f} ms")


def scenario_warm_start(img):
    print("\n-- scenario: started by the bootloader with a stale interrupt in service")
    # stock enters ISP from its top-priority USB ISR; the bootloader never RETIs
    cpu = boot(img, sensor=2)
    cpu.isr_depth = 1
    cpu.run(int(0.6 * FREQ))
    check("stale in-service latch cleared at startup", cpu.isr_depth == 0, f"isr_depth={cpu.isr_depth}")
    host = Host(cpu)
    try:
        dev, *_ = host.enumerate()
        ok = dev is not None and len(dev) == 18
    except TimeoutError as e:
        ok, dev = False, str(e)
    check("enumerates after a warm start from the bootloader", ok, "" if ok else str(dev))


def scenario_enumerate(img, sensor_kind, quick=False):
    print(f"\n-- scenario: enumeration + ISP over USB (fake sensor type {sensor_kind})")
    cpu = boot(img, sensor=sensor_kind)
    cpu.run(int(0.6 * FREQ))  # past sensor probe
    host = Host(cpu)
    dev, cfg, strings, rep0, rep1 = host.enumerate()
    vid, pid = dev[8] | dev[9] << 8, dev[10] | dev[11] << 8
    check("device descriptor VID:PID 258a:1007 (same as stock)", (vid, pid) == (0x258A, 0x1007),
          f"{vid:04x}:{pid:04x}")
    check("manufacturer string 'ISY'", strings.get(1) == "ISY", repr(strings.get(1)))
    check("product string 'IGM3000 Gaming Mouse'", strings.get(2) == "IGM3000 Gaming Mouse", repr(strings.get(2)))
    check("configuration descriptor 2 interfaces, 59 bytes", cfg and cfg[4] == 2 and len(cfg) == 59,
          f"{len(cfg) if cfg else None} bytes")
    r0, e0 = parse_hid(rep0 or b"")
    r1, e1 = parse_hid(rep1 or b"")
    check("iface 0 report descriptor well-formed, 7-byte mouse input", e0 is None and r0.get(("input", 0)) == 56,
          e0 or str(r0))
    check("iface 1 report descriptor well-formed", e1 is None, e1 or "")
    feat = sorted(rid for (k, rid) in r1 if k == "feature")
    check("sinowisp: iface 1 declares feature report 5 (5 bytes)", r1.get(("feature", 5)) == 40,
          f"feature ids {feat}")
    diag = host.get_feature(1, 7, 18)
    check("diag feature report 7 readable", diag is not None and len(diag) == 18 and diag[0] == 7,
          diag.hex() if diag else "stall")
    want_pid = {1: 0x12, 2: 0x09, 4: 0x3B, 11: 0x43}.get(sensor_kind, 0)
    if diag:
        check(f"sensor detected as type {sensor_kind}", diag[1] == (sensor_kind or 0) and diag[2] == want_pid,
              f"type={diag[1]} pid={diag[2]:02x}")
    if sensor_kind:
        dpi_regs = {1: 0x19, 2: 0x33, 4: 0x0D, 11: 0x1B}[sensor_kind]
        check("CPI register written after init", any(r == dpi_regs for r, _ in cpu.sensor.writes),
              f"writes: {[(hex(r), hex(v)) for r, v in cpu.sensor.writes[-4:]]}")
        cpu.ep1_reports.clear()
        cpu.sensor.set_motion(5, -3)
        cpu.run(int(0.01 * FREQ))
        got = [r for r in cpu.ep1_reports if len(r) == 7 and (r[1] or r[2] or r[3] or r[4])]
        if got:
            r = got[0]
            x = int.from_bytes(r[1:3], "little", signed=True)
            y = int.from_bytes(r[3:5], "little", signed=True)
            exp_y = -3 if sensor_kind == 11 else 3
            check("motion reaches EP1 report (x=5, y oriented like stock)", (x, y) == (5, exp_y), f"x={x} y={y}")
        else:
            check("motion reaches EP1 report", False, f"reports: {cpu.ep1_reports[:3]}")
    # DPI LEDs on P4.0..3, active low, stock patterns (level 2 -> 0x07, level 3 -> 0x0C)
    p4 = cpu.sfr[0xC0] & 0x0F
    check("DPI LEDs show level 2 at boot (P4 low nibble 1000b)", p4 == 0x08 and (cpu.sfr[0xE5] & 0x0F) == 0x0F,
          f"P4={p4:04b} P4CR={cpu.sfr[0xE5]:02x}")
    # DPI+ is button bit 5: P2.1 pulled low only while P3.0 drives the matrix column
    cpu.matrix_low = {5}
    cpu.run(int(0.02 * FREQ))
    cpu.matrix_low = set()
    cpu.run(int(0.02 * FREQ))
    p4 = cpu.sfr[0xC0] & 0x0F
    check("DPI+ (bit5) -> level 3, DPI LEDs 0011b", p4 == 0x03, f"P4={p4:04b}")
    # button press -> report
    cpu.ep1_reports.clear()
    cpu.buttons_low.add((P2, 0, 1))
    cpu.run(int(0.02 * FREQ))
    cpu.buttons_low.clear()
    cpu.run(int(0.02 * FREQ))
    btn = [r[0] for r in cpu.ep1_reports]
    check("button bit0 press/release produces reports", 1 in btn and btn[-1] == 0, f"button bytes {btn}")
    # keep running past the 10 s fallback: must stay in the app once configured
    if not quick:
        stayed = not run_until_isp(cpu, 11000)
        check("configured device does NOT fall back to ISP after 10 s", stayed)
    # sinowisp's ISP command
    cpu.isp_hit = None
    try:
        host.set_feature(1, 5, bytes([0x05, 0x75, 0, 0, 0, 0]))
        hit = run_until_isp(cpu, 50)
    except Halt:
        hit = True  # jumped while the host was still finishing the status stage
    ok, d = isp_ok(cpu)
    check("SET_REPORT feature 5 [05 75 00 00 00 00] -> ISP jump (sinowisp path)", hit and ok, d)


def main():
    ihx, mp = sys.argv[1], sys.argv[2]
    quick = "--quick" in sys.argv
    img, used = load_ihex(ihx)
    syms = load_map(mp)
    global IDLE_PC
    IDLE_PC = find_idle_pc(Path(ihx).with_suffix(".ihx.p"))
    print(f"(main-loop idle fast-forward at {IDLE_PC:#06x})" if IDLE_PC else "(no idle fast-forward)")
    print("== static checks")
    static_checks(img, used, syms)
    scenario_warm_start(img)
    scenario_boot_chord(img)
    scenario_no_host(img)
    for kind in ([2] if quick else [2, 1, 4, 11]):
        scenario_enumerate(img, kind, quick=quick or kind != 2)
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
