#!/usr/bin/env python3
# gen_deskduck_sprites.py -- draw the DeskDuck task's sprites as PNGs
#
# Each frame is a 24x24 character grid in a four-colour palette, facing right.
# Every frame but the icon bar head is also written mirrored, facing left, as
# Pinboard draws actors whole with no flipping. Output goes to the task's
# icon pool directory, one PNG per frame, named <frame>-r / <frame>-l.
#
# Usage: tools/gen_deskduck_sprites.py   (from the repo root)

import os
import struct
import zlib

OUT = "resources/wuss-tasks/deskduck/iconpool"
SIZE = 24

PALETTE = {
    ".": (0, 0, 0, 0),
    "W": (255, 255, 255, 255),  # body
    "O": (255, 153, 0, 255),    # bill and feet
    "K": (0, 0, 0, 255),        # eye
    "G": (128, 128, 128, 255),  # shade and outline
}

BLANK = "." * SIZE

# idle, standing: rows 20-22 are the feet, which the walk frames replace
IDLE = [
    BLANK,
    BLANK,
    BLANK,
    "..............GGGG......",
    ".............GWWWWG.....",
    "............GWWWWWWG....",
    "............GWWWKWWG....",
    "............GWWWWWWOOO..",
    "............GWWWWWWOOOO.",
    ".............GWWWWGOOO..",
    "..............GWWG......",
    "..G...........GWWG......",
    "..GWG........GWWWWG.....",
    "..GWWGGGGGGGGWWWWWWG....",
    "..GWWWWWWWWWWWWWWWWG....",
    "...GWWWWWWWWWWWWWWWG....",
    "...GWWWWWGGWWWWWWWWG....",
    "....GWWWWWWGGWWWWWG.....",
    ".....GWWWWWWWWWWWG......",
    "......GGGGGGGGGGG.......",
    ".........O....O.........",
    ".........O....O.........",
    ".........OO...OO........",
    BLANK,
]

HEAD = [
    BLANK,
    BLANK,
    BLANK,
    "........GGGGGG..........",
    ".......GWWWWWWGG........",
    "......GWWWWWWWWWG.......",
    ".....GWWWWWWWWWWWG......",
    ".....GWWWWWWWKKWWG......",
    ".....GWWWWWWWKKWWG......",
    ".....GWWWWWWWWWWWGOOOO..",
    ".....GWWWWWWWWWWGOOOOOO.",
    ".....GWWWWWWWWWWGOOOOOO.",
    "......GWWWWWWWWWWGOOOO..",
    ".......GWWWWWWWWG.......",
    "........GWWWWWWG........",
    "........GWWWWWWWG.......",
    ".......GWWWWWWWWWG......",
    "......GWWWWWWWWWWWG.....",
    "......GWWWWWWWWWWWG.....",
    "......GGGGGGGGGGGGG.....",
    BLANK,
    BLANK,
    BLANK,
    BLANK,
]


def replace(grid, rows):
    """grid with the rows in the {index: row} dict swapped in"""
    return [rows.get(i, row) for i, row in enumerate(grid)]


WALK1 = replace(IDLE, {
    20: "........O......O........",
    21: ".......O........O.......",
    22: ".......OO.......OO......",
})

WALK2 = replace(IDLE, {
    20: "...........OO...........",
    21: "...........OO...........",
    22: "...........OOO..........",
})

QUACK = replace(IDLE, {
    6: "............GWWWKWWGOO..",
    7: "............GWWWWWWGOOOO",
    8: "............GWWWWWWG....",
    9: ".............GWWWWGOOO..",
})

# sleeping: sat down three rows, feet tucked away, eye shut, then a z
SAT = [BLANK] * 3 + IDLE[:20] + [BLANK]
SAT = replace(SAT, {9: SAT[9].replace("K", "G")})

SLEEP1 = replace(SAT, {
    3: "...................KKK..",
    4: "....................K...",
    5: "...................KKK..",
})

SLEEP2 = replace(SAT, {
    0: "...................KKKK.",
    1: "......................K.",
    2: ".....................K..",
    3: "....................K...",
    4: "...................KKKK.",
})

FRAMES = {
    "walk1": WALK1,
    "walk2": WALK2,
    "idle": IDLE,
    "quack": QUACK,
    "sleep1": SLEEP1,
    "sleep2": SLEEP2,
}


def chunk(kind, data):
    body = kind + data
    return (struct.pack(">I", len(data)) + body +
            struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))


def write_png(path, grid):
    assert len(grid) == SIZE, path
    raw = b""
    for row in grid:
        assert len(row) == SIZE, (path, row)
        raw += b"\0" + b"".join(bytes(PALETTE[c]) for c in row)
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)  # 8-bit RGBA
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, grid in FRAMES.items():
        write_png(os.path.join(OUT, name + "-r.png"), grid)
        write_png(os.path.join(OUT, name + "-l.png"),
                  [row[::-1] for row in grid])
    write_png(os.path.join(OUT, "head.png"), HEAD)


if __name__ == "__main__":
    main()
