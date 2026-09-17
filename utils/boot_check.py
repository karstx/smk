#!/usr/bin/env python3
"""Check a linked bootloader's placement against the addresses it promises.

Two of them are an ABI with the application, which is linked separately and
cannot see whether they held: reset lands at the base of the bootloader region,
and `isp_jump()` lands on the entry stub. SDCC will happily put a helper
function ahead of the stub inside its area, so the stub's address is asserted
rather than assumed.

Usage: boot_check.py --map FILE --base ADDR --entry ADDR --stamp FILE
"""

import argparse
import re
import sys

SYMBOL = re.compile(r"^[A-Z]:\s+([0-9A-Fa-f]+)\s+(\S+)")


def symbols(path):
    found = {}
    with open(path) as f:
        for line in f:
            m = SYMBOL.match(line)
            if m:
                found.setdefault(m.group(2), int(m.group(1), 16))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", required=True)
    parser.add_argument("--base", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--entry", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--stamp")
    args = parser.parse_args()

    syms = symbols(args.map)
    problems = []

    def want(name, expected, what):
        actual = syms.get(name)
        if actual is None:
            problems.append(f"{name} is missing from the map")
        elif actual != expected:
            problems.append(f"{what} is at {actual:#06x}, not {expected:#06x}")

    want("s_HOME", args.base, "the reset entry")
    want("s_BOOTISP", args.entry, "the entry stub's area")
    want("_boot_isp_entry", args.entry, "the entry stub")

    for area in ("CSEG", "CONST", "GSINIT", "GSFINAL", "XINIT"):
        start, length = syms.get("s_" + area), syms.get("l_" + area)
        if start is None or length is None or start < args.base:
            continue
        if start + length > args.entry:
            problems.append(f"{area} runs to {start + length:#06x}, past the entry stub at {args.entry:#06x}")

    if problems:
        for problem in problems:
            print(f"boot_check: {problem}", file=sys.stderr)
        return 1

    print(f"boot_check: OK — reset at {args.base:#06x}, entry stub at {args.entry:#06x}.")

    if args.stamp:
        open(args.stamp, "w").close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
