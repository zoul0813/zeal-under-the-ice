#!/usr/bin/env python3
"""Generate the Under the Ice tileset (assets/tileset.gif) and its tile index header (src/gfx.h).

All art is text pixel art so it stays diffable. One 256-tile, 8-bit tileset is shared by both
layers and all sprites. Palette index 0 is transparent on layer 1 and on sprites; on layer 0 it is
the top band of the sky.

Layout:
    0         empty
    1..31     the ice sheet and the light shafts under its holes (layer 1, drifts sideways)
    32..95    font at the ASCII codes
    96..127   sky, water bands, rock, sand, kelp (layer 0, the streamed sea)
    128..     sprites. The seal's vertical frames (diving, rising, breathing in a hole, scratching)
              are hardware 16x32 sprites: tile N is the top half, N+1 the bottom half. Everything
              else is 16x16. The Video Board has no 32x16 mode, so nothing here is wider than 16.
Run: python3 tools/make_assets.py   (from the game folder, needs Pillow)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
from zealart import Tileset  # noqa: E402

PAL = [
    ('0', (150, 200, 236)),  # 0 sky top / transparent
    ('1', (190, 222, 242)),
    ('2', (228, 240, 248)),  # low sky
    # water, light to dark (8 bands)
    ('3', (40, 116, 166)),
    ('4', (30, 98, 146)),
    ('5', (24, 82, 128)),
    ('6', (18, 66, 110)),
    ('7', (14, 52, 92)),
    ('8', (10, 40, 76)),
    ('9', (8, 30, 60)),
    ('A', (6, 22, 46)),
    ('B', (120, 196, 232)),  # waterline
    ('C', (96, 170, 214)),   # light shaft
    ('D', (70, 140, 190)),   # light shaft, dim
    ('k', (12, 16, 26)),
    ('w', (255, 255, 255)),
    # ice
    ('s', (250, 252, 255)),
    ('i', (220, 238, 248)),
    ('j', (180, 212, 232)),
    ('l', (130, 176, 208)),
    ('L', (90, 140, 180)),
    ('T', (168, 222, 246)),  # thin ice
    ('t', (74, 128, 176)),   # crack
    # rock
    ('r', (86, 92, 104)),
    ('R', (60, 66, 78)),
    ('q', (38, 42, 52)),
    ('g', (70, 120, 90)),    # algae on rock tops
    # sand
    ('y', (150, 138, 110)),
    ('Y', (112, 102, 82)),
    # kelp
    ('G', (56, 132, 86)),
    ('h', (34, 90, 58)),
    # ringed seal
    ('S', (122, 130, 144)),
    ('o', (84, 90, 104)),     # rings
    ('p', (170, 178, 190)),   # belly
    ('P', (206, 212, 220)),
    ('n', (24, 24, 32)),      # nose / eye
    # capelin (silver)
    ('c', (200, 218, 230)),
    ('d', (110, 140, 162)),
    # arctic cod
    ('e', (176, 156, 100)),
    ('f', (122, 106, 66)),
    ('E', (224, 210, 156)),
    # halibut
    ('u', (136, 100, 72)),
    ('U', (98, 72, 52)),
    ('v', (220, 200, 170)),
    # jelly
    ('J', (255, 150, 206)),
    ('x', (196, 88, 160)),
    # leopard seal
    ('m', (78, 82, 94)),
    ('M', (44, 46, 56)),
    ('N', (150, 156, 168)),
    ('a', (200, 60, 70)),     # mouth
    # ui
    ('b', (190, 236, 252)),   # bubble
    ('H', (232, 52, 72)),
    ('z', (255, 226, 120)),
]
ts = Tileset(PAL)
grid, put = ts.grid, ts.put
SEAS = ['3', '4', '5', '6', '7', '8', '9', 'A']


def rows_of(fn, h=16):
    return [''.join(fn(x, y) for x in range(16)) for y in range(h)]


# ---------------------------------------------------------------- layer 1: the ice sheet
put(0, 'TILE_EMPTY', ts.blank())


def ice_px(x, y, body='i', mid='j', dark='l', seed=0):
    v = (x * 3 + y * 5 + seed + (x * y) % 5) % 17
    return dark if v == 0 else (mid if v in (4, 9) or (x + seed) % 11 == 0 else body)


def ice_top(edge=None, thin=False):
    """upper ice row: snow on top. edge 'L' = hole on the right of this tile, 'R' = hole on the left"""
    body, mid, dark = ('T', 'T', 'j') if thin else ('i', 'j', 'l')
    rows = []
    for y in range(16):
        r = ''
        for x in range(16):
            c = ice_px(x, y, body, mid, dark, 3)
            snow = 2 + ((x * 3) % 5 == 1) if thin else 4 + ((x * 5) % 7 > 4) - ((x * 3) % 5 == 1)
            if y == 0:
                c = 'k'
            elif y < snow:
                c = 's'
            elif y == snow:
                c = 'l' if thin else 'j'
            r += c
        rows.append(r)
    rows = [list(r) for r in rows]
    if edge == 'L':
        for y in range(16):
            cut = 15 - max(0, 3 - y)
            for x in range(cut + 1, 16):
                rows[y][x] = '.'
            rows[y][cut] = 'k'
    if edge == 'R':
        for y in range(16):
            cut = max(0, 3 - y)
            for x in range(0, cut):
                rows[y][x] = '.'
            rows[y][cut] = 'k'
    return grid([''.join(r) for r in rows])


def ice_bottom(edge=None, thin=False, cracks=0, pocket=False):
    """lower ice row: the underside, jagged, with short icicles"""
    body, mid, dark = ('T', 'T', 'l') if thin else ('j', 'l', 'L')
    rows = []
    for y in range(16):
        r = ''
        for x in range(16):
            depth = 11 + (x * 7 % 3) - (1 if x % 5 == 2 else 0)
            icicle = (x % 6 == 3 and y < 15) or (x % 9 == 7 and y < 14)
            if y < depth or (icicle and not thin):
                c = ice_px(x, y, body, mid, dark, 1)
                if y >= depth - 1:
                    c = dark
                if icicle and y >= depth:
                    c = 'L' if not thin else 'l'
            elif y == depth or (icicle and y == 15):
                c = 'k'
            else:
                c = '.'
            r += c
        rows.append(list(r))
    if cracks:
        lines = [(2, 0, 1, 1, 9), (9, 0, -1, 1, 8), (12, 2, 0, 1, 8)][:cracks + 1]
        for (x0, y0, dx, dy, n) in lines:
            x, y = x0, y0
            for i in range(n):
                if 0 <= x < 16 and 0 <= y < 16 and rows[y][x] not in '.k':
                    rows[y][x] = 't'
                x += dx if i % 2 else 0
                y += dy
    if pocket:
        bub = ["....kkkkkkk.....",
               "..kkbbbbbbbkk...",
               ".kbwwbbbbbbbbk..",
               ".kbwbbbbbbbbbk..",
               "..kkbbbbbbbkk...",
               "....kkkkkkk....."]
        for j, line in enumerate(bub):
            for x, ch in enumerate(line):
                if ch != '.':
                    rows[8 + j][x] = ch
    if edge == 'L':
        for y in range(16):
            cut = 15 - max(0, y - 9)
            for x in range(cut + 1, 16):
                rows[y][x] = '.'
            if rows[y][cut] != '.':
                rows[y][cut] = 'k'
    if edge == 'R':
        for y in range(16):
            cut = max(0, y - 9)
            for x in range(0, cut):
                rows[y][x] = '.'
            if rows[y][cut] != '.':
                rows[y][cut] = 'k'
    return grid([''.join(r) for r in rows])


put(1, 'TILE_ICE_TOP', ice_top())
put(2, 'TILE_ICE_TOP_L', ice_top('L'))
put(3, 'TILE_ICE_TOP_R', ice_top('R'))
put(4, 'TILE_ICE_BOT', ice_bottom())
put(5, 'TILE_ICE_BOT_L', ice_bottom('L'))
put(6, 'TILE_ICE_BOT_R', ice_bottom('R'))
put(7, 'TILE_THIN_TOP', ice_top(thin=True))
put(8, 'TILE_THIN_BOT', ice_bottom(thin=True))
put(9, 'TILE_THIN_CRACK1', ice_bottom(thin=True, cracks=1))
put(10, 'TILE_THIN_CRACK2', ice_bottom(thin=True, cracks=2))
put(11, 'TILE_POCKET', ice_bottom(pocket=True))
# light falling through a hole: sparse dots that thin out with depth, see-through between them
for i, (name, step, col) in enumerate([('TILE_SHAFT0', 2, 'C'), ('TILE_SHAFT1', 3, 'C'),
                                         ('TILE_SHAFT2', 4, 'D'), ('TILE_SHAFT3', 6, 'D')]):
    put(12 + i, name, grid(rows_of(lambda x, y, s=step, c=col: c if (x + y * 3) % (s * 2) == 0 and (y + x // 4) % s == 0 and 2 <= x <= 13 else '.')))

ts.font()

# ---------------------------------------------------------------- layer 0: sky and sea (opaque)
put(96, 'BG_SKY', grid(rows_of(lambda x, y: '0' if y < 6 else ('1' if y < 12 or (x + y) % 2 else '0'))))
put(97, 'BG_SKY_LOW', grid(rows_of(lambda x, y: '1' if y < 5 else ('2' if y > 9 or (x + y) % 2 else '1'))))
# waterline 7 px into the tile: what shows through a hole
put(98, 'BG_SURFACE', grid(rows_of(lambda x, y: '2' if y < 6 else ('B' if y == 6 or (y == 7 and x % 4 != 1) else '3'))))
for i, c in enumerate(SEAS):
    put(99 + i * 2, 'BG_SEA%d' % i, ts.fill(c))
    if i + 1 < len(SEAS):
        n = SEAS[i + 1]
        put(100 + i * 2, 'BG_SEA%d%d' % (i, i + 1), grid(rows_of(lambda x, y, a=c, b=n: b if (y > 10 or (y > 4 and (x + y) % 2 == 0 and (y > 7 or (x + y) % 4 == 0))) else a)))
# 99..113 used


def rock_px(x, y, seed):
    v = (x * 7 + y * 13 + seed * 5 + (x * y) % 7) % 23
    return 'q' if v == 0 else ('r' if v in (3, 11, 17) else 'R')


put(114, 'TILE_ROCK', grid(rows_of(lambda x, y: rock_px(x, y, 1))), ['TF_SOLID'])
put(115, 'TILE_ROCK2', grid(rows_of(lambda x, y: rock_px(x, y, 4))), ['TF_SOLID'])
put(116, 'TILE_ROCK_TOP', grid(rows_of(lambda x, y: 'k' if y == 0 else ('g' if y < 3 and (x * 5) % 7 > 1 else ('r' if y < 4 else rock_px(x, y, 2))))), ['TF_SOLID'])
put(117, 'TILE_ROCK_BOT', grid(rows_of(lambda x, y: 'k' if y == 15 else ('q' if y > 12 else rock_px(x, y, 3)))), ['TF_SOLID'])
put(118, 'TILE_SAND_TOP', grid(rows_of(lambda x, y: 'k' if y == 0 else ('y' if y < 4 or (x + y * 3) % 7 else 'Y'))), ['TF_SOLID'])
put(119, 'TILE_SAND', grid(rows_of(lambda x, y: 'Y' if (x * 3 + y * 5) % 9 == 0 else 'y')), ['TF_SOLID'])
# kelp for the four deepest bands (it only grows on the sea floor)
KELP = [
    "......hG........",
    ".....hGG........",
    "......hGG.......",
    ".......hG.......",
    ".......hGG......",
    "......hGG.......",
    ".....hGG........",
    ".....hG.........",
    "......hG........",
    "......hGG.......",
    ".......hGG......",
    "......hGG.......",
    ".....hGG........",
    ".....hG.........",
    "......hG........",
    "......hG........",
]
for i, c in enumerate(SEAS[4:]):
    put(120 + i, 'BG_KELP%d' % i, grid([r.replace('.', c) for r in KELP]))
# 124: starfish on sand
STAR = ["k" * 16] + ["y" * 16] * 3 + [
    "yyyyyyyHyyyyyyyy", "yyyyyyHHHyyyyyyy", "yyHHHHHHHHHHHyyy", "yyyHHHHHHHHHyyyy",
    "yyyyHHHHHHHyyyyy", "yyyyHHHyHHHyyyyy", "yyyHHyyyyyHHyyyy"] + ["y" * 16] * 5
put(124, 'TILE_SAND_STAR', grid(STAR), ['TF_SOLID'])

# ---------------------------------------------------------------- sprites
ts.start_sprites(128)
spr, tall = ts.spr, ts.tall

# ringed seal, side view facing right, swimming level (16x16)
SWIM1 = [
    "", "", "", "",
    "...........kkk..",
    ".........kkSSSk.",
    "..kkkkkkkSSSSnSk",
    ".kSSoSSSSoSSSSSn",
    "kSSSSSSoSSSSSSkk",
    "kSoSSSSSSSSSSk..",
    ".kpppppppppppk..",
    "..kkkkkkkkkkk...",
]
SWIM2 = [
    "", "", "", "",
    "...........kkk..",
    ".........kkSSSk.",
    "k.kkkkkkkSSSSnSk",
    "kkSSoSSSSoSSSSSn",
    ".kSSSSSSoSSSSSkk",
    "kSoSSSSSSSSSSk..",
    "kkpppppppppppk..",
    "...kkkkkkkkkk...",
]
spr('SPR_SEAL_SWIM1', grid(SWIM1))
spr('SPR_SEAL_SWIM2', grid(SWIM2))
HURT = [
    "", "", "", "",
    "...........kkk..",
    ".........kkSSSk.",
    "..kkkkkkkSSSSwSk",
    ".kppppppppSSSSSn",
    "kpppppppppSSSSkk",
    "kSoSSSSSSSSSSk..",
    ".kSSSSSSSSSSSk..",
    "..kkkkkkkkkkk...",
]
spr('SPR_SEAL_HURT', grid(HURT))

# vertical: head up, 16x32 (flipped on Y to dive head first)
UP1 = [
    "", "",
    "......kkk.......",
    ".....kSSSk......",
    "....kSnSnSk.....",
    "....kSSSSSk.....",
    "....kSSnSSk.....",
    "...kSSSSSSSk....",
    "...kSSoSSSpk....",
    "..kSSSSSSSppk...",
    ".kSkSSSSoSppkSk.",
    "kSk.kSSSSSppk.kk",
    "kk..kSoSSSppk...",
    "....kSSSSSppk...",
    "....kSSSSoppk...",
    "....kSoSSSppk...",
    "....kSSSSSppk...",
    "....kSSSSSpk....",
    ".....kSoSSpk....",
    ".....kSSSSpk....",
    ".....kSSSpk.....",
    "......kSSpk.....",
    "......kSSk......",
    ".....kSSSSk.....",
    "....kSSkkSSk....",
    "....kkk..kkk....",
]
UP2 = [
    "", "",
    "......kkk.......",
    ".....kSSSk......",
    "....kSnSnSk.....",
    "....kSSSSSk.....",
    "....kSSnSSk.....",
    "...kSSSSSSSk....",
    "...kSSoSSSpk....",
    "..kSSSSSSSppk...",
    "..kSSSSSoSppk...",
    ".kSkSSSSSSppkk..",
    ".kkkSoSSSSppkSk.",
    "....kSSSSSppkkk.",
    "....kSSSSoppk...",
    "....kSoSSSppk...",
    "....kSSSSSppk...",
    "....kSSSSSpk....",
    ".....kSoSSpk....",
    ".....kSSSSpk....",
    ".....kSSSpk.....",
    "......kSSpk.....",
    "......kSSk......",
    "......kSSk......",
    ".....kSkkSk.....",
    ".....kk..kk.....",
]
tall('SPR_SEAL_UP1', grid(UP1, 32))
tall('SPR_SEAL_UP2', grid(UP2, 32))
# breathing in a hole: nose up, whiskers, a puff of breath
BREATHE = ["....w.w.........",
           ".....w..........",
           "......kkk.......",
           ".....kSSSk......",
           "....kSnSnSk.....",
           "...wkSSSSSkw....",
           "....kSSnSSk.....",
           "...wkSSSSSkw....",
           "...kSSoSSSpk....",
           "..kSSSSSSSppk..."] + UP1[10:]
tall('SPR_SEAL_BREATHE', grid(BREATHE, 32))
# scratching at thin ice from below: fore-flippers up
SCR1 = [
    "...kk.....kk....",
    "..kSk.kkk.kSk...",
    "..kSkkSSSkkSk...",
    "...kSSnSnSSk....",
    "....kSSSSSk.....",
    "....kSSnSSk.....",
    "...kSSSSSSSk....",
    "...kSSoSSSpk....",
    "..kSSSSSSSppk...",
    "..kSSSSSoSppk...",
] + UP1[12:]
SCR2 = [
    "................",
    ".kk.......kk....",
    "kSk.kkk...kSk...",
    ".kSkSSSk.kSk....",
    "...kSnSnSSk.....",
    "....kSSSSSk.....",
    "....kSSnSSk.....",
    "...kSSSSSSSk....",
    "...kSSoSSSpk....",
    "..kSSSSSSSppk...",
] + UP1[11:]
tall('SPR_SEAL_SCRATCH1', grid(SCR1, 32))
tall('SPR_SEAL_SCRATCH2', grid(SCR2, 32))

# capelin: small silver fish, swims in schools
CAP1 = ["", "", "", "", "", "",
        "....kkkkk...kk..",
        "..kkccccckk.kck.",
        ".kcncccdccckkck.",
        "..kkddddddkk.kk.",
        "....kkkkkk......"]
CAP2 = ["", "", "", "", "", "",
        "....kkkkk.......",
        "..kkccccckk.kk..",
        ".kcncccdccckcck.",
        "..kkddddddkkkk..",
        "....kkkkkk......"]
spr('SPR_CAPELIN1', grid(CAP1))
spr('SPR_CAPELIN2', grid(CAP2))
# arctic cod: bigger, brown with a pale belly
COD1 = ["", "", "", "",
        ".....kkkkkk.....",
        "...kkeeffeekk.kk",
        "..keeefeeeeeekek",
        ".kenkeeeeefeeeek",
        ".keeeeeeeeeeekek",
        "..kEEEEEEEEEk.kk",
        "...kkkkkkkkk....",
        ]
COD2 = ["", "", "", "",
        ".....kkkkkk.....",
        "...kkeeffeekk...",
        "..keeefeeeeeekkk",
        ".kenkeeeeefeeeek",
        ".keeeeeeeeeeeekk",
        "..kEEEEEEEEEk...",
        "...kkkkkkkkk....",
        ]
spr('SPR_COD1', grid(COD1))
spr('SPR_COD2', grid(COD2))
# halibut: flat, lies on the sea floor, both eyes on top
HAL1 = ["", "", "", "", "", "", "", "", "",
        "....kkkkkkkk....",
        "..kkuUuuuUuukk..",
        ".kunnuuUuuuUuukk",
        "kuuuuuUuuuuuuuUk",
        ".kvvvvvvvvvvvvkk",
        "..kkkkkkkkkkkk..",
        ]
HAL2 = ["", "", "", "", "", "", "", "",
        "....kkkkkkkk....",
        "..kkuUuuuUuukk.k",
        ".kunnuuUuuuUuukk",
        "kuuuuuUuuuuuuuUk",
        ".kvvvvvvvvvvvvk.",
        "..kkkkkkkkkkkk..",
        ]
spr('SPR_HALIBUT1', grid(HAL1))
spr('SPR_HALIBUT2', grid(HAL2))
JELLY1 = ["", "",
          ".....kkkkk......",
          "...kkJJJJJkk....",
          "..kJJwJJJJJJk...",
          "..kJwJJJJJJJk...",
          ".kJJJJJJJJJJJk..",
          ".kxxxxxxxxxxxk..",
          "..k.x.x.x.x.k...",
          "....x.x.x.x.....",
          "...x..x..x..x...",
          "...x.x...x.x....",
          "....x..x...x....",
          "...x...x..x.....",
          ]
JELLY2 = ["", "", "",
          ".....kkkkk......",
          "...kkJJJJJkk....",
          "..kJJwJJJJJJk...",
          ".kJJwJJJJJJJJk..",
          "kxxxxxxxxxxxxxk.",
          ".k.x.x.x.x.x.k..",
          "...x.x..x.x.x...",
          "..x..x..x..x.x..",
          "..x.x....x...x..",
          "...x.x..x...x...",
          ]
spr('SPR_JELLY1', grid(JELLY1))
spr('SPR_JELLY2', grid(JELLY2))
# leopard seal: the hunter. Long head, spotted, a big mouth
LEO1 = ["", "", "", "",
        "..........kkkk..",
        "........kkmmmmk.",
        "..kkkkkkmmmnmmmk",
        ".kmmMmmmmMmmmaak",
        "kmmmmMmmmmmmmmak",
        "kmMmmmmMmmmmmkk.",
        ".kNNNNNNNNNNNk..",
        "..kkkkkkkkkkk...",
        ]
LEO2 = ["", "", "", "",
        "..........kkkk..",
        "........kkmmmmk.",
        "k.kkkkkkmmmnmmmk",
        "kkmmMmmmmMmmmmmk",
        ".kmmmmMmmmmmmmkk",
        "kmMmmmmMmmmmmkk.",
        "kkNNNNNNNNNNNk..",
        "...kkkkkkkkkk...",
        ]
spr('SPR_LEOPARD1', grid(LEO1))
spr('SPR_LEOPARD2', grid(LEO2))
BUBBLE = ["", "", "", "", "",
          "......kk........",
          ".....kbbk.......",
          "....kbwbbk......",
          "....kbbbbk......",
          ".....kbbk.......",
          "......kk........"]
spr('SPR_BUBBLE', grid(BUBBLE))
SPARK = ["", "", "",
         ".......k........",
         "......kzk.......",
         "......kzk.......",
         "..kkkkkzzkkkk...",
         "...kzzzwzzzk....",
         "....kzzzzzk.....",
         "....kzzkzzk.....",
         "...kzzk.kzzk....",
         "...kkk...kkk...."]
spr('SPR_SPARK', grid(SPARK))
CHIPS = ["", "", "", "", "", "",
         "...s.....s......",
         ".....i.......s..",
         "..i....s..i.....",
         "....s.......i...",
         ".s.....i........"]
spr('SPR_ICE_CHIPS', grid(CHIPS))
# HUD
AIR = ["", "", "", "",
       ".....kkkk.......",
       "....kBBBBk......",
       "...kBwwBBBk.....",
       "...kBwBBBBk.....",
       "...kBBBBB3k.....",
       "...kBBB333k.....",
       "....kB333k......",
       ".....kkkk......."]
AIR_OFF = ["", "", "", "", "",
           ".....dddd.......",
           "....d....d......",
           "....d....d......",
           "....d....d......",
           "....d....d......",
           ".....dddd.......",
           ""]
spr('SPR_AIR', grid(AIR))
spr('SPR_AIR_OFF', grid(AIR_OFF))
AIR_LOW = ts.recolor(grid(AIR), {'B': 'H', '3': 'a'})
spr('SPR_AIR_LOW', AIR_LOW)
spr('SPR_FISH_ICON', grid(CAP1))
spr('SPR_SEAL_ICON', grid(SWIM1))
HOLE_ICON = ["", "", "", "",
             "..kkkk....kkkk..",
             ".kssssk..kssssk.",
             "kssssssk.ssssssk",
             "kiiiiiik.kiiiiik",
             ".kjjjjk...kjjjk.",
             "..kkkk.....kkk..",
             ""]
spr('SPR_HOLE_ICON', grid(HOLE_ICON))
# arrow pointing up at the nearest hole when it's off to the side
ARROW = ["", "",
         ".......k........",
         "......kBk.......",
         ".....kBBBk......",
         "....kBBBBBk.....",
         "...kkkBBBkkk....",
         ".....kBBBk......",
         ".....kBBBk......",
         ".....kkkkk......"]
spr('SPR_ARROW', grid(ARROW))

used = ts.save(ROOT)
print('tiles used:', used)

# Shared SNES art, owned locally so the game remains standalone.
from snes_glyphs import install as install_snes_glyphs
install_snes_glyphs(ROOT, used)
