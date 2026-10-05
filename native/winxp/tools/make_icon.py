#!/usr/bin/env python3
"""Writes pulse.ico (16, 32 and 48 px, 32-bit with alpha): two overlapping
rings, green #10B981 and sky #0EA5E9, on a white rounded tile."""
import struct, sys, math

def render(size):
    px = []
    s = size / 32.0
    for y in range(size):
        row = []
        for x in range(size):
            cx, cy = (x + 0.5) / s, (y + 0.5) / s
            # tile with 5 px corner radius and a grey border
            def inside(r):
                dx = max(r - cx + 1, 0, cx - (31 - r)); dy = max(r - cy + 1, 0, cy - (31 - r))
                return dx * dx + dy * dy <= r * r
            col = (0, 0, 0, 0)
            if inside(5.5):
                col = (0xAC, 0xA8, 0x99, 255)
                if cx > 1.5 and cx < 30.5 and cy > 1.5 and cy < 30.5:
                    col = (255, 255, 255, 255)
            for (rx, ry, c) in ((12.5, 16, (0x10, 0xB9, 0x81)), (19.5, 16, (0x0E, 0xA5, 0xE9))):
                d = abs(math.hypot(cx - rx, cy - ry) - 7.2)
                a = max(0.0, min(1.0, (1.7 - d) * s))
                if a > 0 and col[3]:
                    col = tuple(int(col[i] * (1 - a) + c[i] * a) for i in range(3)) + (255,)
            row.append(col)
        px.append(row)
    return px

def bmp_entry(size):
    px = render(size)
    hdr = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    data = b''.join(struct.pack('BBBB', b, g, r, a) for row in reversed(px) for (r, g, b, a) in row)
    mask_row = ((size + 31) // 32) * 4
    mask = b'\x00' * (mask_row * size)
    return hdr + data + mask

sizes = [16, 32, 48]
blobs = [bmp_entry(s) for s in sizes]
out = struct.pack('<HHH', 0, 1, len(sizes))
off = 6 + 16 * len(sizes)
for s, b in zip(sizes, blobs):
    out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(b), off)
    off += len(b)
open(sys.argv[1], 'wb').write(out + b''.join(blobs))
