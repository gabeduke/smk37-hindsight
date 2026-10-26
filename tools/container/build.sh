#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Build the firmware on a Mac or on Linux, in the container (Dockerfile here):
#
#   tools/container/build.sh [elite|pro]
#
# JieLi's installer is fetched once into $SMK37_TOOLCHAIN_CACHE (default
# ~/.cache/smk37-hindsight), checked against its SHA-256, and unpacked with
# innoextract: it is never run. Build.ps1 then checks the 22 toolchain files
# it uses against sdk/manifest.json.
set -eu
model=${1:-elite}
case "$model" in pro|elite) ;; *) echo "usage: $0 [elite|pro]" >&2; exit 2 ;; esac

root=$(cd "$(dirname "$0")/../.." && pwd)
here="$root/tools/container"
cache=${SMK37_TOOLCHAIN_CACHE:-$HOME/.cache/smk37-hindsight}
url=https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe
sha256=399bd518a28e47a7a0394506cebd5bf658748a58cd6b7c02512e24b4b88c5a5e

sum() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

mkdir -p "$cache"
if [ ! -f "$cache/2.5.3.exe" ]; then
    echo "Fetching JieLi's 2.5.3 toolchain installer"
    curl -fL --retry 3 -o "$cache/2.5.3.exe.part" "$url"
    mv "$cache/2.5.3.exe.part" "$cache/2.5.3.exe"
fi
if [ "$(sum "$cache/2.5.3.exe")" != "$sha256" ]; then
    echo "$cache/2.5.3.exe is not the expected installer; delete it to fetch it again." >&2
    exit 1
fi

# The image is named by its Dockerfile, so a change to it builds a new one.
image="smk37-hindsight-build:$(sum "$here/Dockerfile" | cut -c1-12)"
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$image" "$here"
fi

pi32="$cache/unpacked/C\$/JL/pi32"
if [ ! -f "$pi32/bin/clang.exe" ]; then
    rm -rf "$cache/unpacked"
    docker run --rm --platform linux/amd64 -v "$cache":/cache "$image" \
        innoextract --silent --output-dir /cache/unpacked /cache/2.5.3.exe
fi

docker run --rm --platform linux/amd64 \
    -v "$root":/work -v "$pi32":/opt/pi32:ro -w /work "$image" \
    pwsh -NoProfile -File sdk/Build.ps1 -Model "$model" -FirmwareOnly -Toolchain /opt/pi32
