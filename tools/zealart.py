"""Game-owned asset helpers: an 8-bit tileset built from text pixel art, the 5x7 font,
ZGDK RLE packing and ZMT (Zeal Music Tracker) writing.

Games import it from their tools/ scripts:
    from zealart import Tileset, rle, zmt, voice

Tileset rules (Video Board, 8-bit mode): 256 tiles of 16x16, one shared palette, palette
index 0 is transparent on layer 1 and on sprites. A 16x32 sprite uses tile N (top) and N+1 (bottom).
"""
import os
import struct
from PIL import Image


class Tileset:
    def __init__(self, palette):
        """palette: list of (char, (r, g, b)); index 0 is the transparent color"""
        assert len(palette) <= 256
        self.pal = palette
        self.ci = {ch: i for i, (ch, _) in enumerate(palette)}
        assert len(self.ci) == len(palette), 'duplicate palette char'
        self.tiles = [None] * 256
        self.names = {}
        self.flags = {}
        self.next = None

    # ------------------------------------------------------------ pixels
    def grid(self, rows, h=16):
        """text rows (up to 16 chars, '.' = index 0) -> h rows of 16 indexes"""
        out = []
        for r in rows:
            r = r.replace(' ', '')
            assert len(r) <= 16, (r, len(r))
            out.append([self.ci[ch] if ch != '.' else 0 for ch in r.ljust(16, '.')])
        while len(out) < h:
            out.append([0] * 16)
        assert len(out) == h, len(out)
        return out

    def blank(self):
        return [[0] * 16 for _ in range(16)]

    def fill(self, c):
        return [[self.ci[c]] * 16 for _ in range(16)]

    def recolor(self, px, mapping):
        m = {self.ci[a]: self.ci[b] for a, b in mapping.items()}
        return [[m.get(v, v) for v in r] for r in px]

    @staticmethod
    def over(base, top):
        """top drawn over base, index 0 in top is see-through"""
        return [[t if t else b for b, t in zip(rb, rt)] for rb, rt in zip(base, top)]

    @staticmethod
    def shift(px, dx=0, dy=0):
        h = len(px)
        out = [[0] * 16 for _ in range(h)]
        for y in range(h):
            for x in range(16):
                sx, sy = x - dx, y - dy
                if 0 <= sx < 16 and 0 <= sy < h:
                    out[y][x] = px[sy][sx]
        return out

    def outline(self, px, c='k'):
        """add a 1 px outline of color c around every non-zero pixel"""
        k = self.ci[c]
        h = len(px)
        out = [r[:] for r in px]
        for y in range(h):
            for x in range(16):
                if px[y][x]:
                    continue
                for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < h and 0 <= xx < 16 and px[yy][xx] and px[yy][xx] != k:
                        out[y][x] = k
                        break
        return out

    # ------------------------------------------------------------ placement
    def put(self, index, name, px, fl=None):
        assert 0 <= index < 256, (index, name)
        assert self.tiles[index] is None, (index, name)
        assert len(px) == 16 and all(len(r) == 16 for r in px), name
        self.tiles[index] = px
        if name:
            assert name not in self.names, name
            self.names[name] = index
        if fl:
            self.flags[index] = fl
        return index

    def start_sprites(self, index):
        self.next = index

    def spr(self, name, px, fl=None):
        """next free sprite slot"""
        i = self.put(self.next, name, px, fl)
        self.next += 1
        return i

    def tall(self, name, px32):
        """16x32 sprite: top half at N, bottom half at N+1 (SPRITE_OPTION_32PX)"""
        assert len(px32) == 32, name
        i = self.spr(name, px32[:16])
        self.spr(None, px32[16:])
        return i

    # ------------------------------------------------------------ font
    FONT = {
        ' ': [""] * 7,
        '!': ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
        '-': [".....", ".....", ".....", "#####", ".....", ".....", "....."],
        '.': [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
        ':': [".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."],
        '?': [".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."],
        "'": ["..#..", "..#..", ".#...", ".....", ".....", ".....", "....."],
        '+': [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
        '/': [".....", "....#", "...#.", "..#..", ".#...", "#....", "....."],
        'x': [".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"],
        '0': [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
        '1': ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
        '2': [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
        '3': ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
        '4': ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
        '5': ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
        '6': ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
        '7': ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
        '8': [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
        '9': [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
        'A': [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
        'B': ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
        'C': [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
        'D': ["###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."],
        'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
        'F': ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
        'G': [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
        'H': ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
        'I': [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
        'J': ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
        'K': ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
        'L': ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
        'M': ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
        'N': ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
        'O': [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
        'P': ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
        'Q': [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
        'R': ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
        'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
        'T': ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
        'U': ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
        'V': ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
        'W': ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
        'X': ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
        'Y': ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
        'Z': ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    }

    def font(self, fg='w', edge='k'):
        """the 5x7 font, doubled, at the ASCII codes 32..95 (ZGDK's default ASCII map prints as-is).
        'x' is drawn at the code of '\\\\' (92) as a times sign."""
        for ch, rows in self.FONT.items():
            px = self.blank()
            for y, r in enumerate(rows):
                for x, c in enumerate(r):
                    if c != '#':
                        continue
                    for dy in range(2):
                        for dx in range(2):
                            px[1 + y * 2 + dy][3 + x * 2 + dx] = self.ci[fg]
            px = self.outline(px, edge)
            code = 92 if ch == 'x' else ord(ch)
            self.put(code, None, px)
        for i in range(32, 96):
            if self.tiles[i] is None:
                self.tiles[i] = self.blank()

    # ------------------------------------------------------------ output
    def save(self, root, header='src/gfx.h', title='tileset'):
        used = max(i for i in range(256) if self.tiles[i] is not None) + 1
        for i in range(used):
            if self.tiles[i] is None:
                self.tiles[i] = self.blank()
        cols = 16
        rows_used = (used + cols - 1) // cols
        img = Image.new('P', (16 * cols, 16 * rows_used))
        flat = []
        for (_, rgb) in self.pal:
            flat += list(rgb)
        flat += [0] * (768 - len(flat))
        img.putpalette(flat)
        for i in range(used):
            tx, ty = (i % cols) * 16, (i // cols) * 16
            for y in range(16):
                for x in range(16):
                    img.putpixel((tx + x, ty + y), self.tiles[i][y][x])
        os.makedirs(os.path.join(root, 'assets'), exist_ok=True)
        img.save(os.path.join(root, 'assets', title + '.gif'))
        os.makedirs(os.path.join(root, 'screenshots', 'assets'), exist_ok=True)
        img.convert('RGB').resize((img.width * 3, img.height * 3), Image.NEAREST).save(
            os.path.join(root, 'screenshots', 'assets', title + '_preview.png'))
        with open(os.path.join(root, header), 'w') as f:
            f.write('/* Generated by tools/make_assets.py, do not edit */\n#ifndef GFX_H\n#define GFX_H\n\n')
            for name, idx in sorted(self.names.items(), key=lambda kv: kv[1]):
                f.write('#define %-24s %3d\n' % (name, idx))
            f.write('\n#define TILE_COUNT %d\n\n' % used)
            f.write('/* tile flags, for a 256-entry table: TILE_FLAGS_INIT inside an array initializer */\n')
            f.write('#define TILE_FLAGS_INIT \\\n')
            f.write(' \\\n'.join('    [%d] = %s,' % (i, ' | '.join(fl)) for i, fl in sorted(self.flags.items())))
            f.write('\n\n#endif\n')
        return used

    def rgb565(self, ch):
        r, g, b = self.pal[self.ci[ch]][1]
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ---------------------------------------------------------------- RLE (ZGDK decompress_rle)
def rle(data):
    """0x80+n-1 = run of n (<=128) of the next byte, 0..127 = n-1 literal bytes follow"""
    out = bytearray()
    i = 0
    lit = bytearray()

    def flush():
        nonlocal lit
        while lit:
            chunk = lit[:128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            lit = lit[128:]

    while i < len(data):
        j = i
        while j < len(data) and data[j] == data[i] and j - i < 128:
            j += 1
        if j - i >= 3:
            flush()
            out.append(0x80 + (j - i) - 1)
            out.append(data[i])
            i = j
        else:
            lit.append(data[i])
            i += 1
    flush()
    return bytes(out)


def unrle(data):
    out = bytearray()
    i = 0
    while i < len(data):
        h = data[i]
        if h & 0x80:
            out += bytes([data[i + 1]]) * ((h & 0x7F) + 1)
            i += 2
        else:
            out += data[i + 1:i + 2 + h]
            i += 2 + h
    return bytes(out)


def c_bytes(data, indent='    '):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ', '.join('0x%02X' % b for b in data[i:i + 16]) + ',')
    return '\n'.join(lines)


# ---------------------------------------------------------------- ZMT music
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
NOTE_OFF = 108
WAV = {"square": 0, "triangle": 1, "saw": 2, "noise": 3}
VOL = {0: 0xF0, 25: 0xF2, 50: 0xF4, 75: 0xF6, 100: 0xF8}
DRUMS = {"k": 24, "s": 64, "h": 96}   # noise "pitch" only changes the colour of the hiss


def note(tok):
    name = tok[0]
    sharp = 1 if tok[1] == "#" else (-1 if tok[1] == "b" else 0)
    octave = int(tok[-1])
    return octave * 12 + SEMI[name] + sharp


def steps(line, drums=False):
    """32 step tokens: '.' hold, '-' note off, 'C5'/'F#4' notes, 'k s h' drum hits on noise"""
    toks = line.split()
    assert len(toks) == 32, (len(toks), line)
    out = []
    for i, t in enumerate(toks):
        if t == ".":
            if drums and i > 0 and toks[i - 1] in DRUMS:
                out.append(NOTE_OFF)
            else:
                out.append(None)
        elif t == "-":
            out.append(NOTE_OFF)
        elif drums:
            out.append(DRUMS[t])
        else:
            out.append(note(t))
    return out


def voice(wave, vol, line):
    return (WAV[wave], VOL[vol], steps(line, drums=(wave == "noise")))


def pattern_bytes(voices):
    out = bytearray()
    bitmap = 0
    for i, v in enumerate(voices):
        if v is not None:
            bitmap |= 1 << i
    out.append(bitmap)
    for v in voices:
        if v is None:
            continue
        wave, vol, notes = v
        cells = []
        step_bits = 0
        for i, n in enumerate(notes):
            hdr = 0
            data = []
            if n is not None:
                hdr |= 0x01
                data.append(n)
            if i == 0:
                hdr |= 0x02 | 0x04
                data += [wave, vol]
            if hdr:
                step_bits |= 1 << i
                cells.append(bytes([hdr] + data))
        out += struct.pack("<I", step_bits)
        for c in cells:
            out += c
    return bytes(out)


def zmt(title, tempo, patterns, arrangement):
    """tempo: frames per quarter note (a step is tempo/4 frames). Voice 4 stays free for effects."""
    assert tempo % 4 == 0
    assert len(patterns) <= 16 and len(arrangement) <= 64
    for p in patterns:
        assert len(p) == 4 and p[3] is None, 'voice 4 belongs to the sound effects'
    out = bytearray(b"ZMT\x00")
    out += title.ljust(12)[:12].encode("ascii")
    out.append(tempo)
    out.append(len(patterns))
    bits = (1 << len(arrangement)) - 1
    out += struct.pack("<II", bits & 0xFFFFFFFF, bits >> 32)
    for p in arrangement:
        out += bytes([p, 0xFF])
    for p in patterns:
        out += pattern_bytes(p)
    return bytes(out)
