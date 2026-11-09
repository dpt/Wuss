#!/usr/bin/env python3
# gen_duckling_sprites.py -- draw the Duckling task's sprites as PNGs
#
# Each frame is a 16x16 character grid in a small palette, side views facing
# left.
# The frames go into one sprite sheet, duckling_16x16.png, in the task's icon
# pool directory: the top row faces right, the bottom row faces left, as
# Pinboard draws actors whole with no flipping. The sleep frames' z is laid
# over after mirroring, so it reads the right way round in both rows. The
# column order matches Duckling's frame enum. The pond it swims on is
# pond.png. The icon bar uses the shared pool's hand-drawn app_duckling.png.
#
# Usage: tools/gen_duckling_sprites.py   (from the repo root)

import os
import struct
import zlib

OUT = "resources/wuss-tasks/duckling/iconpool"
SIZE = 16

PALETTE = {
    ".": (0, 0, 0, 0),
    "#": (0, 0, 0, 255),        # outline and eye
    "A": (255, 236, 39, 255),   # body
    "B": (255, 163, 0, 255),    # bill and feet
    "H": (255, 241, 232, 255),  # highlights
    "R": (171, 82, 54, 255),    # under the bill
    "W": (200, 235, 255, 255),  # ripples and glints
    "P": (41, 173, 255, 255),   # pond water
    "D": (16, 104, 200, 255),   # pond rim
}

POND_W, POND_H = 64, 32

BLANK = "." * SIZE

BODY = [
    "....####........",
    "...#AAAA#.......",
    "..#AAHAAA#......",
    ".##BA#HAA#......",
    "#BBBAAAAA#......",
    ".#RRBAAA####..#.",
    "..##AAAAAAAA##A#",
    "..#AAAAAAAAAAAA#",
    ".#AAAA#AAAAAA#A#",
    ".#AAAAA#AAAA#AA#",
    ".#AAAAAA####AAA#",
    "..#AAAAAAAAAAA#.",
    "...##AAAAAAA##..",
    ".....#######....",
]


def replace(grid, rows):
    """grid with the rows in the {index: row} dict swapped in"""
    return [rows.get(i, row) for i, row in enumerate(grid)]


# standing: rows 14-15 are the feet
IDLE = BODY + [
    "......B...B.....",
    ".....BB..BB.....",
]

WALK1 = BODY + [
    ".....B.....B....",
    "....BB....BB....",
]

WALK2 = BODY + [
    "........B.......",
    ".......BB.......",
]

QUACK = replace(IDLE, {
    3: "#BBBA#HAA#......",
    4: ".###AAAAA#......",
    5: "#RRRBAAA####..#.",
    6: ".###AAAAAAAA##A#",
})

# sleeping: sat down on the feet with the eye shut
SAT = [BLANK] * 2 + replace(BODY, {3: ".##BA##AA#......"})
SLEEP1 = SAT
SLEEP2 = SAT

# swimming: sunk to the waterline, bobbing a pixel, with ripples
SWIM1 = [BLANK] * 3 + BODY[:12] + ["..WWW.......WW.."]
SWIM2 = [BLANK] * 4 + BODY[:11] + [".WW.......WWW..."]
SWIMQUACK = [BLANK] * 3 + QUACK[:12] + ["..WWW.......WW.."]

# seen from the front, walking towards the viewer, and from the back
FRONT_BODY = [
    "......####......",
    ".....#AAAA#.....",
    "....#AAAAAA#....",
    "....#H#AA#H#....",
    "....#ABBBBA#....",
    "...##ARRRRA##...",
    "..#AA#AAAA#AA#..",
    ".#AAAAAAAAAAAA#.",
    ".#AA#AAAAAA#AA#.",
    ".#A#AAAAAAAA#A#.",
    ".##AAAAAAAAAA##.",
    "...#AAAAAAAA#...",
    "....##AAAA##....",
    "......####......",
]

BACK_BODY = [
    "......####......",
    ".....#AAAA#.....",
    "....#AAAAAA#....",
    "....#AAAAAA#....",
    "....#AAAAAA#....",
    "...#A#AAAA#A#...",
    "..#AAABBBBAAA#..",
    ".#AAAAAAAAAAAA#.",
    ".#AA#AAAAAA#AA#.",
    ".#A#AAA##AAA#A#.",
    ".##AAA#AA#AAA##.",
    "...#AA#AA#AA#...",
    "....##AAAA##....",
    "......####......",
]

# feet seen end on: both down, then each lifted in turn
FEET = ["......B..B......", ".....BB..BB....."]
FEET1 = ["......B..B......", ".....BB........."]
FEET2 = ["......B..B......", ".........BB....."]

FRONT = FRONT_BODY + FEET
FRONT1 = FRONT_BODY + FEET1
FRONT2 = FRONT_BODY + FEET2
BACK1 = BACK_BODY + FEET1
BACK2 = BACK_BODY + FEET2

FRAMES = [WALK1, WALK2, IDLE, QUACK, SLEEP1, SLEEP2, SWIM1, SWIM2,
          FRONT, FRONT1, FRONT2, BACK1, BACK2, SWIMQUACK]

# the z of each sleep frame: (frame index, column, top row, rows), drawn at
# the top of the cell, behind the head
ZEDS = [
    (4, 11, 2, ["###",
                "..#",
                ".#.",
                "###"]),
    (5, 11, 0, ["####",
                "..#.",
                ".#..",
                "####"]),
]


def pond():
    """an oval of water with a darker rim and a few glints"""
    a, b = POND_W / 2, POND_H / 2
    grid = []
    for y in range(POND_H):
        row = ""
        for x in range(POND_W):
            u, v = (x + 0.5 - a) / a, (y + 0.5 - b) / b
            uu, vv = (x + 0.5 - a) / (a - 2), (y + 0.5 - b) / (b - 2)
            row += ("." if u * u + v * v > 1 else
                    "D" if uu * uu + vv * vv > 1 else "P")
        grid.append(row)
    for x, y, n in [(14, 8, 4), (40, 12, 3), (22, 21, 5), (46, 23, 2)]:
        grid[y] = grid[y][:x] + "W" * n + grid[y][x + n:]
    return grid


def zed(grid, col, top, z):
    """grid with the z drawn over it, its left column at col"""
    grid = list(grid)
    for i, zrow in enumerate(z):
        row = list(grid[top + i])
        for j, c in enumerate(zrow):
            if c != ".":
                row[col + j] = c
        grid[top + i] = "".join(row)
    return grid


def chunk(kind, data):
    body = kind + data
    return (struct.pack(">I", len(data)) + body +
            struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))


def write_png(path, grid):
    raw = b""
    for row in grid:
        assert len(row) == len(grid[0]), (path, row)
        raw += b"\0" + b"".join(bytes(PALETTE[c]) for c in row)
    ihdr = struct.pack(">IIBBBBB", len(grid[0]), len(grid), 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    os.makedirs(OUT, exist_ok=True)
    left = list(FRAMES)
    right = [[row[::-1] for row in grid] for grid in FRAMES]
    for i, col, top, z in ZEDS:
        left[i] = zed(left[i], col, top, z)
        right[i] = zed(right[i], SIZE - col - len(z[0]), top, z)
    for grid in left + right:
        assert len(grid) == SIZE and all(len(row) == SIZE for row in grid)
    write_png(os.path.join(OUT, "duckling_%dx%d.png" % (SIZE, SIZE)),
              ["".join(rows) for rows in zip(*right)] +
              ["".join(rows) for rows in zip(*left)])
    write_png(os.path.join(OUT, "pond.png"), pond())


if __name__ == "__main__":
    main()
