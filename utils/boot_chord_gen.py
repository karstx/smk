#!/usr/bin/env python3
"""Emit the bootloader's entry chord as matrix coordinates for one keyboard.

The bootloader stays in ISP mode when a chord is held at power-on, but it has
no keymap of its own: it scans the matrix and compares raw (column, row)
positions. Those positions differ per board, so they are read out of the
board's layout here rather than hand-maintained beside it.

A layout declares `#define LAYOUT_x(K<col>_<row>, ...) { ... }` and then calls
it with one keycode per parameter, so zipping the macro's parameter list
against the first keymap entry's arguments maps a keycode to its position.

Usage: boot_chord_gen.py LAYOUT_C KEY[,KEY...]
"""

import re
import sys


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text.replace("\\\n", " ")


def split_args(text):
    """Split a macro argument list on top-level commas."""
    args, depth, current = [], 0, ""
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
        else:
            current += ch
    args.append(current.strip())
    return [a for a in args if a]


def read_paren(text, open_index):
    """Return the text between `text[open_index]` == '(' and its match."""
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[open_index + 1 : i], i
    raise SystemExit("unbalanced parentheses in layout")


def positions(path):
    text = strip_comments(open(path).read())

    definition = re.search(r"#\s*define\s+(LAYOUT_\w+)\s*\(", text)
    if not definition:
        raise SystemExit(f"{path}: no LAYOUT_* macro definition")
    name = definition.group(1)

    params, end = read_paren(text, definition.end() - 1)
    params = split_args(params)

    call = re.search(re.escape(name) + r"\s*\(", text[end:])
    if not call:
        raise SystemExit(f"{path}: {name} is defined but never used")
    args, _ = read_paren(text[end:], end + call.end() - 1 - end)
    args = split_args(args)

    if len(params) != len(args):
        raise SystemExit(f"{path}: {name} takes {len(params)} keys, first keymap passes {len(args)}")

    found = {}
    for param, keycode in zip(params, args):
        match = re.fullmatch(r"K(\d+)_(\d+)", param)
        if not match:
            raise SystemExit(f"{path}: {name} parameter {param} is not K<col>_<row>")
        found.setdefault(keycode, (int(match.group(1)), int(match.group(2))))
    return found


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    layout, keys = sys.argv[1], [k.strip().upper() for k in sys.argv[2].split(",") if k.strip()]
    if not keys:
        raise SystemExit("the chord must name at least one key")

    found = positions(layout)

    chord = []
    for key in keys:
        position = found.get("KC_" + key)
        if position is None:
            raise SystemExit(f"{layout}: the layout has no KC_{key} to build a bootloader chord from")
        chord.append(position)

    print("#pragma once")
    print()
    print(f"#define BOOT_CHORD_COUNT {len(chord)}u")
    print(f"#define BOOT_CHORD_MAX_COL {max(c for c, _ in chord)}u")
    print(f"#define BOOT_CHORD_MAX_ROW {max(r for _, r in chord)}u")
    rows = [f"        {{{col}u, {row}u}}, /* {key} */" for key, (col, row) in zip(keys, chord)]
    width = max(len(r) for r in rows) + 1

    print("#define BOOT_CHORD_KEYS".ljust(width) + " \\")
    print("    {".ljust(width) + " \\")
    for row in rows:
        print(row.ljust(width) + " \\")
    print("    }")


if __name__ == "__main__":
    main()
