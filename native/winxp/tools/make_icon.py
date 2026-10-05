#!/usr/bin/env python3
"""Writes pulse.ico (16, 32 and 48 px, 32-bit with alpha): five vital bars
(CPU, energy, memory, thermal, GPU) on a baseline, on a white rounded tile.
The shapes match brand/pulse-icon.svg. 16 px drops the tile so the bars
stay sharp."""
import struct, sys

INK = (0x0E, 0x1E, 0x19)
BARS = ((0x10, 0xB9, 0x81), (0xF5, 0x9E, 0x0B), (0x0E, 0xA5, 0xE9), (0xF4, 0x3F, 0x5E), (0x14, 0xB8, 0xA6))
HEIGHTS = (78, 118, 96, 150, 110)  # in 256 units, bottoms at y = 192


def rrect(x, y, x0, y0, w, h, r):
    """True if (x, y) lies in the rounded rectangle."""
    if x < x0 or y < y0 or x > x0 + w or y > y0 + h:
        return False
    dx = max(x0 + r - x, 0, x - (x0 + w - r))
    dy = max(y0 + r - y, 0, y - (y0 + h - r))
    return dx * dx + dy * dy <= r * r


def shade(x, y):
    """RGBA at a point in 256 units (no anti-aliasing)."""
    for i, (c, h) in enumerate(zip(BARS, HEIGHTS)):
        if rrect(x, y, 41 + i * 37, 192 - h, 26, h, 13):
            return c + (255,)
    if rrect(x, y, 36, 204, 184, 8, 4):
        return INK + (255,)
    if rrect(x, y, 8, 8, 240, 240, 56):
        if not rrect(x, y, 10, 10, 236, 236, 54):
            return (0xDB, 0xE0, 0xDE, 255)
        t = (y - 8) / 240.0  # white to #E8F5EF
        return (int(255 - 23 * t), int(255 - 10 * t), int(255 - 16 * t), 255)
    return (0, 0, 0, 0)


def render(size):
    """Sample 4x4 points per pixel and average them."""
    n, k = 4, 256.0 / size
    px = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(n):
                for sx in range(n):
                    r, g, b, a = shade((x + (sx + 0.5) / n) * k, (y + (sy + 0.5) / n) * k)
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
            a = acc[3]
            row.append((acc[0] // a, acc[1] // a, acc[2] // a, a // (n * n)) if a else (0, 0, 0, 0))
        px.append(row)
    return px


def render16():
    """Bars only, on whole pixels: 2 px wide, 1 px apart, baseline on row 14."""
    px = [[(0, 0, 0, 0)] * 16 for _ in range(16)]
    for i, (c, h) in enumerate(zip(BARS, HEIGHTS)):
        top = 13 - round(h / 150 * 11) + 1
        for y in range(top, 14):
            for x in (1 + i * 3, 2 + i * 3):
                px[y][x] = c + (255,)
    for x in range(1, 15):
        px[14][x] = INK + (255,)
    return px


def bmp_entry(size):
    px = render16() if size == 16 else render(size)
    hdr = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    data = b''.join(struct.pack('BBBB', b, g, r, a) for row in reversed(px) for (r, g, b, a) in row)
    mask_row = ((size + 31) // 32) * 4
    mask = b''
    for row in reversed(px):
        bits = bytearray(mask_row)
        for x, p in enumerate(row):
            if p[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bytes(bits)
    return hdr + data + mask

sizes = [16, 32, 48]
blobs = [bmp_entry(s) for s in sizes]
out = struct.pack('<HHH', 0, 1, len(sizes))
off = 6 + 16 * len(sizes)
for s, b in zip(sizes, blobs):
    out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(b), off)
    off += len(b)
open(sys.argv[1], 'wb').write(out + b''.join(blobs))
