#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Host tests: no toolchain, no unit. Run from anywhere: tests/run.sh
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/build/tests"
mkdir -p "$out"

# Flash is written only where the areas are asserted (unit_regions.h).
direct=$(grep -rlE '\bfm1_flash_(write|erase)[[:space:]]*\(' "$root/app" | sed "s|^$root/||" |
         grep -vxE 'app/(samples|project|fm_library)\.c' || true)
if [ -n "$direct" ]; then
    echo "Flash writes outside the asserted areas (see app/unit_regions.h):"
    echo "$direct"
    exit 1
fi

# A unit's flash dump is exactly 1 MiB and carries its calibration and keys:
# it never goes in a repo.
if git -C "$root" rev-parse --git-dir >/dev/null 2>&1; then
    git -C "$root" ls-files -z --cached --others --exclude-standard | python3 -I -c '
import os, sys
root = sys.argv[1]
dumps = [p for p in sys.stdin.read().split("\0")
         if p and os.path.isfile(os.path.join(root, p)) and os.path.getsize(os.path.join(root, p)) == 1 << 20]
if dumps:
    print("These look like 1 MiB flash dumps; keep them out of the repo:", *dumps, sep="\n  ")
    sys.exit(1)
' "$root"
fi

${CC:-cc} -std=c11 -Wall -Werror -DFM1_HOST_TEST \
    -I"$root/app" -I"$root/app/synth" -I"$root/sdk/include" -I"$root/third_party/synth" \
    "$root/tests/regions_test.c" -o "$out/regions_test"
"$out/regions_test"

python3 -I "$root/tests/flash_diff_test.py"
