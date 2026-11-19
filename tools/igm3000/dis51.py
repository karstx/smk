#!/usr/bin/env python3
"""Small recursive-descent 8051 disassembler with SH68F881 SFR names.

usage: dis51.py image.bin out.lst limit_hex entry_hex [entry_hex ...]
  e.g. dis51.py stock.bin stock.lst 7000 26c0 03 1b 43 4b 53 5b

Register names come from src/sino51lib/sh68f881/sh68f881.h; names marked * are
SFR page-1 registers (INSCON.BKS0 = 1), shown next to their page-0 alias.
"""
import re
import sys
from pathlib import Path

HEADER = Path(__file__).resolve().parents[2] / "src/sino51lib/sh68f881/sh68f881.h"


def load_sfrs(header=HEADER):
    sfr = {}
    page = None
    for line in open(header):
        m = re.match(r"// (.*)", line)
        if m:
            c = m.group(1)
            page = "p1" if "page 1" in c else ("p0" if "page 0" in c else None)
        m = re.match(r"SFR\((\w+), (0x[0-9a-f]+)\)", line)
        if m:
            sfr.setdefault(int(m.group(2), 16), []).append(m.group(1) + ("*" if page == "p1" else ""))
    return sfr


SFR = load_sfrs()


def sfr_name(a):
    if a < 0x80:
        return f"0x{a:02x}"
    if a in SFR:
        return "|".join(SFR[a])
    return f"SFR_{a:02x}"


def bit_name(b):
    if b < 0x80:
        return f"0x{0x20 + b // 8:02x}.{b % 8}"
    return f"{sfr_name(b & 0xF8)}.{b & 7}"


# opcode -> (mnemonic, [operand kinds])
T = {}
ri = ["@R0", "@R1"]
rn = [f"R{i}" for i in range(8)]


def grp(base, mn, pre=(), post=()):
    for i, r in enumerate(ri):
        T[base + 6 + i] = (mn, list(pre) + [r] + list(post))
    for i, r in enumerate(rn):
        T[base + 8 + i] = (mn, list(pre) + [r] + list(post))


T[0x00] = ("nop", [])
for hi in range(8):
    T[hi * 0x20 + 0x01] = ("ajmp", ["a11"])
    T[hi * 0x20 + 0x11] = ("acall", ["a11"])
T[0x02] = ("ljmp", ["a16"]); T[0x03] = ("rr", ["A"]); T[0x04] = ("inc", ["A"]); T[0x05] = ("inc", ["dir"]); grp(0x00, "inc")
T[0x10] = ("jbc", ["bit", "rel"]); T[0x12] = ("lcall", ["a16"]); T[0x13] = ("rrc", ["A"]); T[0x14] = ("dec", ["A"]); T[0x15] = ("dec", ["dir"]); grp(0x10, "dec")
T[0x20] = ("jb", ["bit", "rel"]); T[0x22] = ("ret", []); T[0x23] = ("rl", ["A"]); T[0x24] = ("add", ["A", "#"]); T[0x25] = ("add", ["A", "dir"]); grp(0x20, "add", ["A"])
T[0x30] = ("jnb", ["bit", "rel"]); T[0x32] = ("reti", []); T[0x33] = ("rlc", ["A"]); T[0x34] = ("addc", ["A", "#"]); T[0x35] = ("addc", ["A", "dir"]); grp(0x30, "addc", ["A"])
for base, mn in ((0x40, "orl"), (0x50, "anl"), (0x60, "xrl")):
    T[base + 2] = (mn, ["dir", "A"]); T[base + 3] = (mn, ["dir", "#"]); T[base + 4] = (mn, ["A", "#"]); T[base + 5] = (mn, ["A", "dir"]); grp(base, mn, ["A"])
T[0x40] = ("jc", ["rel"]); T[0x50] = ("jnc", ["rel"]); T[0x60] = ("jz", ["rel"]); T[0x70] = ("jnz", ["rel"])
T[0x72] = ("orl", ["C", "bit"]); T[0x73] = ("jmp", ["@A+DPTR"]); T[0x74] = ("mov", ["A", "#"]); T[0x75] = ("mov", ["dir", "#"])
T[0x76] = ("mov", ["@R0", "#"]); T[0x77] = ("mov", ["@R1", "#"])
for i in range(8):
    T[0x78 + i] = ("mov", [f"R{i}", "#"])
T[0x80] = ("sjmp", ["rel"]); T[0x82] = ("anl", ["C", "bit"]); T[0x83] = ("movc", ["A", "@A+PC"]); T[0x84] = ("div", ["AB"]); T[0x85] = ("mov", ["dir2", "dir1"])
T[0x86] = ("mov", ["dir", "@R0"]); T[0x87] = ("mov", ["dir", "@R1"])
for i in range(8):
    T[0x88 + i] = ("mov", ["dir", f"R{i}"])
T[0x90] = ("mov", ["DPTR", "#16"]); T[0x92] = ("mov", ["bit", "C"]); T[0x93] = ("movc", ["A", "@A+DPTR"]); T[0x94] = ("subb", ["A", "#"]); T[0x95] = ("subb", ["A", "dir"]); grp(0x90, "subb", ["A"])
T[0xA0] = ("orl", ["C", "/bit"]); T[0xA2] = ("mov", ["C", "bit"]); T[0xA3] = ("inc", ["DPTR"]); T[0xA4] = ("mul", ["AB"]); T[0xA5] = ("db_a5", [])
T[0xA6] = ("mov", ["@R0", "dir"]); T[0xA7] = ("mov", ["@R1", "dir"])
for i in range(8):
    T[0xA8 + i] = ("mov", [f"R{i}", "dir"])
T[0xB0] = ("anl", ["C", "/bit"]); T[0xB2] = ("cpl", ["bit"]); T[0xB3] = ("cpl", ["C"]); T[0xB4] = ("cjne", ["A", "#", "rel"]); T[0xB5] = ("cjne", ["A", "dir", "rel"])
T[0xB6] = ("cjne", ["@R0", "#", "rel"]); T[0xB7] = ("cjne", ["@R1", "#", "rel"])
for i in range(8):
    T[0xB8 + i] = ("cjne", [f"R{i}", "#", "rel"])
T[0xC0] = ("push", ["dir"]); T[0xC2] = ("clr", ["bit"]); T[0xC3] = ("clr", ["C"]); T[0xC4] = ("swap", ["A"]); T[0xC5] = ("xch", ["A", "dir"]); grp(0xC0, "xch", ["A"])
T[0xD0] = ("pop", ["dir"]); T[0xD2] = ("setb", ["bit"]); T[0xD3] = ("setb", ["C"]); T[0xD4] = ("da", ["A"]); T[0xD5] = ("djnz", ["dir", "rel"])
T[0xD6] = ("xchd", ["A", "@R0"]); T[0xD7] = ("xchd", ["A", "@R1"])
for i in range(8):
    T[0xD8 + i] = ("djnz", [f"R{i}", "rel"])
T[0xE0] = ("movx", ["A", "@DPTR"]); T[0xE2] = ("movx", ["A", "@R0"]); T[0xE3] = ("movx", ["A", "@R1"]); T[0xE4] = ("clr", ["A"]); T[0xE5] = ("mov", ["A", "dir"]); grp(0xE0, "mov", ["A"])
T[0xF0] = ("movx", ["@DPTR", "A"]); T[0xF2] = ("movx", ["@R0", "A"]); T[0xF3] = ("movx", ["@R1", "A"]); T[0xF4] = ("cpl", ["A"]); T[0xF5] = ("mov", ["dir", "A"])
T[0xF6] = ("mov", ["@R0", "A"]); T[0xF7] = ("mov", ["@R1", "A"])
for i in range(8):
    T[0xF8 + i] = ("mov", [f"R{i}", "A"])
assert len(T) == 256, len(T)

SIZES = {"dir": 1, "dir1": 1, "dir2": 1, "#": 1, "#16": 2, "bit": 1, "/bit": 1, "rel": 1, "a11": 1, "a16": 2}


def decode(mem, pc):
    op = mem[pc]
    mn, ops = T[op]
    length = 1 + sum(SIZES.get(o, 0) for o in ops)
    raw = mem[pc + 1:pc + length]
    out = []
    targets = []
    i = 0
    if op == 0x85:  # mov dst, src  encoded as 85 src dst
        return mn, [sfr_name(raw[1]), sfr_name(raw[0])], length, [], (raw[1], raw[0])
    for o in ops:
        if o in ("dir", "dir1", "dir2"):
            out.append(sfr_name(raw[i])); i += 1
        elif o == "#":
            out.append(f"#0x{raw[i]:02x}"); i += 1
        elif o == "#16":
            out.append(f"#0x{raw[i] << 8 | raw[i + 1]:04x}"); i += 2
        elif o == "bit":
            out.append(bit_name(raw[i])); i += 1
        elif o == "/bit":
            out.append("/" + bit_name(raw[i])); i += 1
        elif o == "rel":
            r = raw[i]; i += 1
            t = (pc + length + (r - 256 if r > 127 else r)) & 0xFFFF
            out.append(f"L_{t:04x}"); targets.append(t)
        elif o == "a11":
            t = ((pc + length) & 0xF800) | ((op >> 5) << 8) | raw[i]; i += 1
            out.append(f"L_{t:04x}"); targets.append(t)
        elif o == "a16":
            t = raw[i] << 8 | raw[i + 1]; i += 2
            out.append(f"L_{t:04x}"); targets.append(t)
        else:
            out.append(o)
    return mn, out, length, targets, None


def analyze(mem, entries, limit):
    code = {}
    labels = {}
    calls = set()
    todo = list(entries)
    while todo:
        pc = todo.pop()
        while pc < limit and pc not in code:
            mn, ops, ln, targets, _ = decode(mem, pc)
            code[pc] = (mn, ops, ln)
            for t in targets:
                labels.setdefault(t, set()).add(pc)
                if t < limit:
                    todo.append(t)
                if mn in ("lcall", "acall"):
                    calls.add(t)
            if mn in ("ret", "reti", "ljmp", "ajmp", "sjmp", "jmp"):
                break
            pc += ln
    return code, labels, calls


def main():
    mem = bytearray(Path(sys.argv[1]).read_bytes())
    mem += b"\x00" * 4
    limit = int(sys.argv[3], 16) if len(sys.argv) > 3 else len(mem) - 4
    entries = [int(x, 16) for x in sys.argv[4:]]
    code, labels, calls = analyze(mem, entries, limit)
    lines = []
    pc = 0
    while pc < limit:
        if pc in code:
            mn, ops, ln = code[pc]
            if pc in labels:
                kind = "FUNC" if pc in calls else "label"
                xr = ",".join(f"{x:04x}" for x in sorted(labels[pc])[:8])
                lines.append(f"\n; ---- {kind} L_{pc:04x}  xref {xr}{'...' if len(labels[pc]) > 8 else ''}")
            raw = " ".join(f"{b:02x}" for b in mem[pc:pc + ln])
            lines.append(f"{pc:04x}: {raw:<9} {mn:<6} {', '.join(ops)}")
            pc += ln
        else:
            start = pc
            while pc < limit and pc not in code and pc - start < 16:
                pc += 1
            chunk = mem[start:pc]
            asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            lines.append(f"{start:04x}: .db {' '.join(f'{b:02x}' for b in chunk):<47} |{asc}|")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
    print(f"decoded {len(code)} instructions, {len(calls)} functions")


if __name__ == "__main__":
    main()
