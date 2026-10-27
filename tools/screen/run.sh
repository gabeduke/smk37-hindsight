#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Render the keyboard's screen on the host, no keyboard and no toolchain:
#
#   tools/screen/run.sh [SCRIPT]      (default tools/screen/pass1.txt)
#
# Builds this firmware's app/ code on Open's simulator (web/engine) with the
# host's C compiler, plays the script, and writes build/screen/<script>/<shot>.png.
# A failed `expect` fails the run.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
script=${1:-$root/tools/screen/pass1.txt}
out="$root/build/screen/$(basename "$script" .txt)"
mkdir -p "$out"
# The web build's sources and flags (tools/Build-Web.ps1), minus WebAssembly.
sources=$(cd "$root" && find app web third_party/synth -name '*.c' ! -path 'app/board/*' | sort)
(cd "$root" && ${CC:-cc} -std=c11 -O1 -fwrapv -fno-fast-math -ffp-contract=off \
    -DFM1_HOST_TEST -DFM1_OPEN_WEB -w \
    -Iweb/engine -Iweb/synth -Ithird_party/synth -Isdk/include -Iapp/synth -Iapp \
    tools/screen/screen.c $sources -lm -o "$root/build/screen/screen")
rm -f "$out"/*.rgb565 "$out"/*.png
"$root/build/screen/screen" "$script" "$out"
for dump in "$out"/*.rgb565; do
    python3 -I "$root/tools/screen/png.py" "$dump" "${dump%.rgb565}.png"
    rm "$dump"
    echo "$(basename "${dump%.rgb565}").png"
done
