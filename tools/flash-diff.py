#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Compare two full flash dumps of one unit, taken before and after a write.

    tools/flash-diff.py BEFORE.bin AFTER.bin [--expect START-END]... [--accept-unit START-END]...

Prints every changed byte range. Fails (exit 1) when a byte changed in the
unit's own regions -- the Bluetooth pairing and factory calibration at
[0xC0000, 0xC2000), the chip key and MAC at [0xFF000, 0x100000) -- unless
that byte is inside an --accept-unit range, and, given any --expect range,
when a byte changed outside all of them. Ranges are half open, in hex or
decimal: --expect 0x4000-0x7B000. Exit 2 is a usage or size error.
"""
import argparse
import sys

UNIT = [(0xC0000, 0xC2000, "Bluetooth pairing and factory calibration"),
        (0xFF000, 0x100000, "chip key and MAC")]


def span(text):
    try:
        start, end = (int(part, 0) for part in text.split("-", 1))
    except ValueError:
        raise argparse.ArgumentTypeError(f"not a START-END range: {text}")
    if not 0 <= start < end:
        raise argparse.ArgumentTypeError(f"empty or negative range: {text}")
    return start, end


def changed_runs(before, after):
    runs, start = [], None
    for i, (a, b) in enumerate(zip(before, after)):
        if a != b and start is None:
            start = i
        elif a == b and start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, len(before)))
    return runs


def inside(position, ranges):
    return any(start <= position < end for start, end in ranges)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--expect", type=span, action="append", default=[])
    parser.add_argument("--accept-unit", type=span, action="append", default=[])
    try:
        args = parser.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    before = open(args.before, "rb").read()
    after = open(args.after, "rb").read()
    if len(before) != len(after) or len(before) != 0x100000:
        print(f"expected two 1 MiB dumps, got {len(before)} and {len(after)} bytes")
        return 2

    runs = changed_runs(before, after)
    if not runs:
        print("identical")
        return 0
    failed = False
    for start, end in runs:
        notes = []
        for unit_start, unit_end, what in UNIT:
            hit = [p for p in range(max(start, unit_start), min(end, unit_end))
                   if not inside(p, args.accept_unit)]
            if hit:
                notes.append(f"UNIT DATA CHANGED: {what} at 0x{hit[0]:05X}")
                failed = True
        if args.expect:
            stray = [p for p in range(start, end) if not inside(p, args.expect)]
            if stray:
                notes.append(f"outside the expected ranges from 0x{stray[0]:05X}")
                failed = True
        print(f"0x{start:05X}-0x{end:05X} ({end - start} bytes)" + "".join("  " + n for n in notes))
    sectors = sorted({p // 4096 for start, end in runs for p in (start, end - 1)})
    print(f"{len(runs)} changed ranges; sectors touched from 0x{sectors[0] * 4096:05X} to 0x{sectors[-1] * 4096 + 4096:05X}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
