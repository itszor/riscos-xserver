#!/usr/bin/env python3
"""Make Xriscos's icon bar sprite: the X Window System logo.

The logo is drawn with the polygons of libXmu's XmuDrawLogo (as xlogo
draws it), at 8x the size, and each 8x8 block becomes one pixel shaded with
the Wimp's greys (colour 0 white to 7 black) by how much of it the logo
covers, so the edges are smoothed. The sprite is mode 20 (16 colours, 2x2
OS units per pixel, the "2x2" icon size), 34x34 pixels, with a mask.

Usage: mklogo.py HEADER.h [SPRITEFILE [PREVIEW.png]]
       mklogo.py --app NAME SPRITES SPRITES22
  HEADER.h    C source for the sprite area, for hw/kdrive/riscos
  SPRITEFILE  the same as a RISC OS sprite file (an !Sprites22)
  PREVIEW.png a magnified preview, to look at on the host

--app writes an application's icons, NAME being its directory name without
the "!" (sprite names are lower case): SPRITES holds "!name" (34x17) and
"sm!name" (18x9) in mode 12, for rectangular-pixel modes, and SPRITES22 the
same at 34x34 and 18x18 in mode 20, for square ones.
"""
import struct
import sys
import zlib

SIZE = 34           # sprite, pixels square
LOGO = 32           # the logo inside it
SCALE = 8           # supersampling
NAME = b"xlogo"
MODE = 20


def xmu_logo_polys(size):
    """XmuDrawLogo's polygons for a size x size logo at 0,0: a list of
    (points, fill) in drawing order, fill True for foreground."""
    size &= ~1
    thin = max(size // 11, 1)
    gap = (thin + 3) // 4
    d31 = thin + thin + gap
    h = size // 2
    return [
        ([(size, 0), (size - d31, 0), (0, size), (d31, size)], True),
        ([(d31 // 2, size), (h, h), (h + (d31 - d31 // 2), h), (d31, size)],
         False),
        ([(size - d31 // 2, 0), (h, h), (h - (d31 - d31 // 2), h),
          (size - d31, 0)], False),
        ([(0, 0), (size // 4, 0), (size, size), (size - size // 4, size)],
         True),
        ([(size - thin, 0), (size - (thin + gap), 0), (thin, size),
          (thin + gap, size)], False),
    ]


def inside(poly, x, y):
    """Point in a convex polygon (either winding), by crossing count."""
    n, c = len(poly), False
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            if x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
                c = not c
    return c


def render(SIZE=SIZE, LOGO=LOGO):
    """Coverage (0 to 1) of each pixel of a SIZE x SIZE sprite by a LOGO x
    LOGO logo centred in it."""
    big = LOGO * SCALE
    polys = xmu_logo_polys(big)
    grid = [[False] * big for _ in range(big)]
    for y in range(big):
        for x in range(big):
            px, py = x + 0.5, y + 0.5
            for poly, fill in polys:
                if inside(poly, px, py):
                    grid[y][x] = fill
    margin = (SIZE - LOGO) // 2
    cover = [[0.0] * SIZE for _ in range(SIZE)]
    for y in range(LOGO):
        for x in range(LOGO):
            n = sum(grid[y * SCALE + j][x * SCALE + i]
                    for j in range(SCALE) for i in range(SCALE))
            cover[y + margin][x + margin] = n / (SCALE * SCALE)
    return cover


def halve_rows(cover):
    """Average pairs of rows, for a mode with pixels twice as tall."""
    return [[(a + b) / 2 for a, b in zip(cover[y], cover[y + 1])]
            for y in range(0, len(cover) - 1, 2)]


def sprite(cover, NAME=NAME, MODE=MODE):
    """One old-format 4bpp sprite with a mask: header and data."""
    SIZE = len(cover[0])            # width
    HEIGHT = len(cover)
    words = (SIZE * 4 + 31) // 32
    row = words * 4
    image, mask = bytearray(row * HEIGHT), bytearray(row * HEIGHT)
    for y in range(HEIGHT):
        for x in range(SIZE):
            c = cover[y][x]
            if c < 1 / 16:
                continue
            colour = min(7, round(c * 7))       # Wimp greys: 0 white, 7 black
            shift = (x & 1) * 4
            image[y * row + x // 2] |= colour << shift
            mask[y * row + x // 2] |= 0xF << shift
    header = struct.pack("<I12sIIIIIII", 44 + 2 * len(image), NAME,
                         words - 1, HEIGHT - 1, 0, (SIZE * 4 - 1) & 31,
                         44, 44 + len(image), MODE)
    return header + bytes(image) + bytes(mask)


def write_header(path, spr):
    area = struct.pack("<IIII", 16 + len(spr), 1, 16, 16 + len(spr)) + spr
    words = struct.unpack("<%dI" % (len(area) // 4), area)
    with open(path, "w") as f:
        f.write("/* Made by tools/mklogo.py: the X logo as a sprite area, "
                "for the icon bar. */\n")
        f.write("static const unsigned riscos_logo_area[%d] = {\n" % len(words))
        for i in range(0, len(words), 6):
            f.write("    " + ", ".join("0x%08x" % w for w in words[i:i + 6])
                    + ",\n")
        f.write("};\n#define RISCOS_LOGO_NAME \"%s\"\n" % NAME.decode())
        f.write("#define RISCOS_LOGO_SIZE %d\n" % SIZE)


def write_spritefile(path, *sprites):
    # A sprite file is a sprite area without its first word.
    data = b"".join(sprites)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", len(sprites), 16, 16 + len(data)) + data)


def write_app_sprites(name, path, path22):
    """!Sprites (mode 12) and !Sprites22 (mode 20) for an application."""
    big, small = render(), render(18, 16)
    big_name = ("!" + name.lower()).encode()
    small_name = ("sm!" + name.lower()).encode()
    write_spritefile(path, sprite(halve_rows(big), big_name, 12),
                     sprite(halve_rows(small), small_name, 12))
    write_spritefile(path22, sprite(big, big_name, 20),
                     sprite(small, small_name, 20))


def write_png(path, cover, mag=8):
    rows = []
    for y in range(SIZE * mag):
        line = bytearray([0])
        for x in range(SIZE * mag):
            c = cover[y // mag][x // mag]
            if c < 1 / 16:
                line += bytes((0xdd, 0xdd, 0xdd))
            else:
                g = 255 - round(min(7, round(c * 7)) * 255 / 7)
                line += bytes((g, g, g))
        rows.append(bytes(line))

    def chunk(t, d):
        return (struct.pack(">I", len(d)) + t + d +
                struct.pack(">I", zlib.crc32(t + d) & 0xffffffff))
    w = h = SIZE * mag
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" +
                chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(b"".join(rows))) +
                chunk(b"IEND", b""))


def main():
    if sys.argv[1:2] == ["--app"]:
        write_app_sprites(*sys.argv[2:5])
        return
    cover = render()
    spr = sprite(cover)
    write_header(sys.argv[1], spr)
    if len(sys.argv) > 2:
        write_spritefile(sys.argv[2], spr)
    if len(sys.argv) > 3:
        write_png(sys.argv[3], cover)


if __name__ == "__main__":
    main()
