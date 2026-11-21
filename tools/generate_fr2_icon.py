#!/usr/bin/env python3
"""generate_fr2_icon.py - Fox Ranger II app icons.

An original pixel-art side view of a green and yellow fighter (drawn for this
project; no game graphics), pixel-sharp on a dark rounded tile with a green
glow and a yellow rim.  The 1024 px master is box-filtered down so small
sizes stay legible.  The same green/yellow palette as the launcher.

Writes packaging/FoxRanger2.{icns,png,ico} (stdlib + macOS iconutil for .icns)."""
import os, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.join(HERE, '..', 'packaging')

# G/g green, Y/y yellow, C cyan canopy, W white highlight, . transparent
SHIP = [
    "..yY..............................................",
    "..yYg.............................................",
    "..yYGg............................................",
    "..yYGGg...........................................",
    "..yYGGGg..........................................",
    "..yYGGGGg.......gggg..............................",
    "..gGGGGGGgggggggGGGGgggg..........................",
    ".ggGGGGGGGGGGGGGGGGGGCCCggg.......................",
    "yYYgGGGGGGGGGGGGGGGGCCWWCCGGggg...................",
    "yYYgGGYYYYGGGGGGGGGGGCCCCGGGGGGGGgggg.............",
    "yYYgGYyggyYGGGGGGGGGGGGGGGGGGGGGGGGGGGgggg........",
    "yYYgGYg..gYGGYYYYYYYYYYYYYYYYYYYYYGGGGGGGGGGggW...",
    "yYYgGYyggyYGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGWW.",
    ".ggGGGYYYYGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGgggggg...",
    "..ggGGGGGGGgggggGGGGGGGGGGGGGGGGGGGGgggggg........",
    "...gggggggg...ggGGGGGGGGGGGGGGGGGggg..............",
    ".............gGGGGGGGGGGGGGGGggg..................",
    "............gGGGGYYYYYYGGgg.......................",
    "...........gGGGGGGGGGgg...........................",
    "..........yYYYYYYYyy..............................",
]
COL = {'G': (0x3F, 0xCF, 0x3F), 'g': (0x1E, 0x7A, 0x2A), 'Y': (0xFF, 0xD2, 0x1E), 'y': (0xC0, 0x8A, 0x10),
       'C': (0x6F, 0xE8, 0xFF), 'W': (0xFF, 0xFF, 0xFF)}


def master(n=1024):
    sw, sh = len(SHIP[0]), len(SHIP)
    s = int(n * 0.84) // sw                       # integer scale: crisp pixels
    ox, oy = (n - sw * s) // 2, (n - sh * s) // 2
    r, rim = n * 0.18, max(2, n // 36)
    px = bytearray(n * n * 4)
    for y in range(n):
        for x in range(n):
            dx = max(r - x, 0, x - (n - 1 - r)); dy = max(r - y, 0, y - (n - 1 - r))
            dist = (dx * dx + dy * dy) ** 0.5
            if dist > r:
                continue
            if dist > r - rim or min(x, y, n - 1 - x, n - 1 - y) < rim:
                c = (0xFF, 0xD2, 0x1E, 255)                                   # yellow rim
            else:
                # space black with a green glow behind the hull
                gx, gy = (x - n * 0.50) / (n * 0.46), (y - n * 0.50) / (n * 0.30)
                glow = max(0.0, 1.0 - (gx * gx + gy * gy))
                c = (int(0x02 + 0x30 * glow * glow), int(0x05 + 0x9A * glow * glow), int(0x0A + 0x18 * glow * glow), 255)
                sx, sy = (x - ox) // s, (y - oy) // s
                if 0 <= sx < sw and 0 <= sy < sh and x >= ox and y >= oy and SHIP[sy][sx] in COL:
                    c = COL[SHIP[sy][sx]] + (255,)
                elif (x // (n // 256), y // (n // 256)) in STAR_CELLS:
                    c = (0x9F, 0xD8, 0x8A, 255)
            px[(y * n + x) * 4:(y * n + x) * 4 + 4] = bytes(c)
    return n, px


# a few faint stars, on a 256-grid so they survive downscaling
STAR_CELLS = {(23, 31), (61, 200), (88, 22), (140, 226), (201, 40), (222, 180), (40, 120), (180, 18), (238, 98)}


def downscale(n, px, m):
    f = n // m
    out = bytearray(m * m * 4)
    for y in range(m):
        for x in range(m):
            acc = [0, 0, 0, 0]
            for yy in range(y * f, y * f + f):
                o = (yy * n + x * f) * 4
                for xx in range(f):
                    a = px[o + 3]
                    acc[0] += px[o] * a; acc[1] += px[o + 1] * a; acc[2] += px[o + 2] * a; acc[3] += a
                    o += 4
            a = acc[3]
            out[(y * m + x) * 4:(y * m + x) * 4 + 4] = bytes(
                [acc[0] // a if a else 0, acc[1] // a if a else 0, acc[2] // a if a else 0, a // (f * f)])
    return out


def png(n, px):
    raw = b''.join(b'\0' + bytes(px[y * n * 4:(y + 1) * n * 4]) for y in range(n))
    c = lambda t, x: struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x))
    return (b'\x89PNG\r\n\x1a\n' + c(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 6, 0, 0, 0))
            + c(b'IDAT', zlib.compress(raw, 9)) + c(b'IEND', b''))


def main():
    n, big = master()
    sizes = {m: (big if m == n else downscale(n, big, m)) for m in (1024, 512, 256, 128, 64, 32, 16)}
    open(os.path.join(PKG, 'FoxRanger2.png'), 'wb').write(png(256, sizes[256]))
    # Windows .ico: PNG-compressed entries (Vista+)
    ico_sizes = [16, 32, 64, 128, 256]
    blobs = [png(m, sizes[m]) for m in ico_sizes]
    hdr = struct.pack('<HHH', 0, 1, len(blobs))
    off = 6 + 16 * len(blobs)
    ents = b''
    for m, b in zip(ico_sizes, blobs):
        ents += struct.pack('<BBBBHHII', m % 256, m % 256, 0, 0, 1, 32, len(b), off)
        off += len(b)
    open(os.path.join(PKG, 'FoxRanger2.ico'), 'wb').write(hdr + ents + b''.join(blobs))
    if sys.platform == 'darwin':
        with tempfile.TemporaryDirectory() as t:
            iset = os.path.join(t, 'FoxRanger2.iconset'); os.mkdir(iset)
            for m in (16, 32, 128, 256, 512):
                open(os.path.join(iset, f'icon_{m}x{m}.png'), 'wb').write(png(m, sizes[m]))
                open(os.path.join(iset, f'icon_{m}x{m}@2x.png'), 'wb').write(png(2 * m, sizes[2 * m]))
            subprocess.run(['iconutil', '-c', 'icns', iset, '-o', os.path.join(PKG, 'FoxRanger2.icns')], check=True)


if __name__ == '__main__':
    main()
