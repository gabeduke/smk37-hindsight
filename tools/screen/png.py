# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Turn a 240x240 RGB565 screen dump into a PNG, twice the size:
python3 -I tools/screen/png.py IN.rgb565 OUT.png"""
import struct
import sys
import zlib

W = H = 240
SCALE = 2
data = open(sys.argv[1], "rb").read()
assert len(data) == W * H * 2, "not a 240x240 RGB565 dump"
rows = []
for y in range(H):
    row = bytearray()
    for x in range(W):
        p, = struct.unpack_from("<H", data, (y * W + x) * 2)
        rgb = bytes(((p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
        row += rgb * SCALE
    rows += [b"\0" + bytes(row)] * SCALE


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W * SCALE, H * SCALE, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
