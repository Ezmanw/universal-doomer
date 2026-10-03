#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG with only the standard library.
usage: ppm2png.py in.ppm out.png [scale]"""
import sys, zlib, struct

def read_ppm(path):
    data = open(path, 'rb').read()
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos+1].isspace(): pos += 1
        end = pos
        while not data[end:end+1].isspace(): end += 1
        parts.append(data[pos:end]); pos = end
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos+1:pos+1+w*h*3]

def write_png(path, w, h, rgb, scale=1):
    rows = []
    for y in range(h):
        row = rgb[y*w*3:(y+1)*w*3]
        if scale > 1:
            row = b''.join(row[x*3:x*3+3]*scale for x in range(w))
        rows += [b'\0' + row] * scale
    raw = zlib.compress(b''.join(rows), 9)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w*scale, h*scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', raw) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)

w, h, rgb = read_ppm(sys.argv[1])
write_png(sys.argv[2], w, h, rgb, int(sys.argv[3]) if len(sys.argv) > 3 else 1)
