#!/usr/bin/env python3
"""Writes the program icon (16x16 and 32x32, 16 colours) as a .ico file.

Usage: gen_icon.py <out.ico>

16-colour icons load on every Windows version from 95 on. The picture is
the Pulse icon (brand/pulse-icon.svg): five vital bars (CPU, energy,
memory, thermal, GPU) on a baseline. 32 px puts them on a white tile;
16 px drops the tile so the bars stay sharp.
"""
import struct
import sys

# Index 0 is see-through (the AND mask covers it).
PALETTE = [(0, 0, 0), (255, 255, 255), (0xC0, 0xC0, 0xC0), (0x0E, 0x1E, 0x19),
           (0x10, 0xB9, 0x81), (0xF5, 0x9E, 0x0B), (0x0E, 0xA5, 0xE9), (0xF4, 0x3F, 0x5E),
           (0x14, 0xB8, 0xA6), (0xE8, 0xF5, 0xEF)] + [(0, 0, 0)] * 6
CLEAR, WHITE, EDGE, INK, TINT = 0, 1, 2, 3, 9
BARS = [4, 5, 6, 7, 8]
HEIGHTS = [78, 118, 96, 150, 110]  # in 256 units, as in the SVG


def pixels(size):
    px = [[CLEAR] * size for _ in range(size)]
    if size == 16:
        # bars 2 px wide, 1 px apart, baseline on row 14
        for k, h in enumerate(HEIGHTS):
            n = int(round(h / 150.0 * 11))
            for y in range(14 - n, 14):
                for x in (1 + k * 3, 2 + k * 3):
                    px[y][x] = BARS[k]
        for x in range(1, 15):
            px[14][x] = INK
        return px
    # 32 px: tile with cut corners and a grey edge, light tint on the lower half
    for y in range(32):
        for x in range(32):
            c = min(x, 31 - x) + min(y, 31 - y)
            if c < 3:
                continue
            edge = x in (0, 31) or y in (0, 31) or c == 3
            px[y][x] = EDGE if edge else (TINT if y >= 20 else WHITE)
    # bars 3 px wide, 2 px apart, bottoms on row 23, baseline on rows 25 and 26
    for k, h in enumerate(HEIGHTS):
        n = int(round(h / 150.0 * 17))
        for y in range(24 - n, 24):
            for x in range(4 + k * 5, 7 + k * 5):
                px[y][x] = BARS[k]
    for y in (25, 26):
        for x in range(4, 28):
            px[y][x] = INK
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
    mask = b""
    for y in range(size - 1, -1, -1):
        bits = bytearray(and_row)
        for x in range(size):
            if px[y][x] == CLEAR:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bytes(bits)
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
