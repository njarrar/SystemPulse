#!/usr/bin/env python3
"""Writes the program icon (16x16 and 32x32, 16 colours) as a .ico file.

Usage: gen_icon.py <out.ico>

16-colour icons load on every Windows version from 95 on. The picture is
the tray icon: green bars on black.
"""
import struct
import sys

PALETTE = [(0, 0, 0), (0, 255, 0), (0, 128, 0), (128, 128, 128)] + [(0, 0, 0)] * 12
BARS = [0.35, 0.55, 0.4, 0.8, 0.6]


def pixels(size):
    px = [[0] * size for _ in range(size)]
    unit = size / 16.0
    for k, h in enumerate(BARS):
        x0 = int(round((1 + k * 3) * unit))
        x1 = int(round((3 + k * 3) * unit))
        top = int(round((15 - h * 13) * unit))
        bottom = int(round(15 * unit))
        for y in range(top, bottom):
            for x in range(x0, x1):
                px[y][x] = 1 if x < x1 - max(1, int(unit) // 2) or size == 16 else 2
    return px


def image(size):
    px = pixels(size)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 4, 0, 0, 0, 0, 16, 0)
    pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in PALETTE)
    xor = b""
    row_bytes = ((size * 4 + 31) // 32) * 4
    for y in range(size - 1, -1, -1):
        row = bytearray()
        for x in range(0, size, 2):
            row.append((px[y][x] << 4) | px[y][x + 1])
        row += b"\0" * (row_bytes - len(row))
        xor += bytes(row)
    and_row = ((size + 31) // 32) * 4
    mask = b"\0" * (and_row * size)
    return header + pal + xor + mask


def main():
    out = sys.argv[1]
    imgs = [image(16), image(32)]
    data = struct.pack("<HHH", 0, 1, len(imgs))
    offset = 6 + 16 * len(imgs)
    for size, img in zip((16, 32), imgs):
        data += struct.pack("<BBBBHHII", size, size, 16, 0, 1, 4, len(img), offset)
        offset += len(img)
    data += b"".join(imgs)
    with open(out, "wb") as fh:
        fh.write(data)


if __name__ == "__main__":
    main()
