#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Build the firmware on a Mac or on Linux, in the container (Dockerfile here):
#
#   tools/container/build.sh [elite|pro] [firmware|web|package]
#
#   firmware  the package alone, build/<model>/generated/ (the default)
#   web       the workbench with no package: the simulator, this app/ in a
#             browser, at build/preview/index.html. No JieLi toolchain.
#   package   the firmware, and the workbench with it inside: the installer,
#             build/official-web/<model>/SMK37-Open-Source.html
#
# JieLi's installer is fetched once into $SMK37_TOOLCHAIN_CACHE (default
# ~/.cache/smk37-hindsight), checked against its SHA-256, and unpacked with
# innoextract: it is never run. Build.ps1 then checks the 22 toolchain files
# it uses against sdk/manifest.json. The workbench's WASI SDK 27 is fetched
# and checked the same way.
#
# The installer page links the source it was built from (GPL). It is this
# commit's archive on GitHub unless FM1_SOURCE_URL says otherwise, so build
# it from a pushed, clean tree.
set -eu
model=${1:-elite}
what=${2:-firmware}
case "$model" in pro|elite) ;; *) echo "usage: $0 [elite|pro] [firmware|web|package]" >&2; exit 2 ;; esac
case "$what" in firmware|web|package) ;; *) echo "usage: $0 [elite|pro] [firmware|web|package]" >&2; exit 2 ;; esac

root=$(cd "$(dirname "$0")/../.." && pwd)
here="$root/tools/container"
cache=${SMK37_TOOLCHAIN_CACHE:-$HOME/.cache/smk37-hindsight}
url=https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe
sha256=399bd518a28e47a7a0394506cebd5bf658748a58cd6b7c02512e24b4b88c5a5e
wasi=wasi-sdk-27.0-x86_64-linux
wasi_url=https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/$wasi.tar.gz
wasi_sha256=b7d4d944c88503e4f21d84af07ac293e3440b1b6210bfd7fe78e0afd92c23bc2

sum() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

# fetch <file> <url> <sha256>: into the cache once, then checked every time.
fetch() {
    if [ ! -f "$cache/$1" ]; then
        echo "Fetching $1"
        curl -fL --retry 3 -o "$cache/$1.part" "$2"
        mv "$cache/$1.part" "$cache/$1"
    fi
    if [ "$(sum "$cache/$1")" != "$3" ]; then
        echo "$cache/$1 is not the expected file; delete it to fetch it again." >&2
        exit 1
    fi
}

mkdir -p "$cache"
[ "$what" = web ] || fetch 2.5.3.exe "$url" "$sha256"

# The image is named by its Dockerfile, so a change to it builds a new one.
image="smk37-hindsight-build:$(sum "$here/Dockerfile" | cut -c1-12)"
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$image" "$here"
fi

pi32="$cache/unpacked/C\$/JL/pi32"
if [ "$what" != web ] && [ ! -f "$pi32/bin/clang.exe" ]; then
    rm -rf "$cache/unpacked"
    docker run --rm --platform linux/amd64 -v "$cache":/cache "$image" \
        innoextract --silent --output-dir /cache/unpacked /cache/2.5.3.exe
fi

if [ "$what" = firmware ]; then
    exec docker run --rm --platform linux/amd64 \
        -v "$root":/work -v "$pi32":/opt/pi32:ro -w /work "$image" \
        pwsh -NoProfile -File sdk/Build.ps1 -Model "$model" -FirmwareOnly -Toolchain /opt/pi32
fi

fetch "$wasi.tar.gz" "$wasi_url" "$wasi_sha256"
if [ ! -x "$cache/$wasi/bin/clang" ]; then
    rm -rf "${cache:?}/$wasi"
    tar -xzf "$cache/$wasi.tar.gz" -C "$cache"
fi

if [ "$what" = web ]; then
    exec docker run --rm --platform linux/amd64 \
        -v "$root":/work -v "$cache/$wasi":/opt/wasi:ro -w /work -e FM1_WASI_SDK=/opt/wasi "$image" \
        pwsh -NoProfile -File tools/Build-Web.ps1 -LocalPreview
fi

source_url=${FM1_SOURCE_URL:-https://github.com/gabeduke/smk37-hindsight/archive/$(git -C "$root" rev-parse HEAD).tar.gz}
if [ -n "$(git -C "$root" status --porcelain)" ]; then
    echo "Warning: the tree has changes, so $source_url is not quite its source." >&2
fi
exec docker run --rm --platform linux/amd64 \
    -v "$root":/work -v "$pi32":/opt/pi32:ro -v "$cache/$wasi":/opt/wasi:ro -w /work \
    -e FM1_WASI_SDK=/opt/wasi -e FM1_SOURCE_URL="$source_url" "$image" \
    pwsh -NoProfile -File sdk/Build.ps1 -Model "$model" -Package -Toolchain /opt/pi32
