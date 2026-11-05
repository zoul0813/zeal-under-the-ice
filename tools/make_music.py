#!/usr/bin/env python3
"""Writes the Under the Ice music as ZMT (Zeal Music Tracker) files in assets/.

All tracks are original. Each pattern is 32 sixteenth steps on up to 3 voices; voice 4 is left
to the sound effects. A track holds at most 3 patterns (the game keeps 3 pattern buffers in the
dead tileset blob). Step tokens: '.' hold, '-' note off, 'C5' / 'F#4' notes, 'k s h' drum hits.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
from zealart import zmt, voice  # noqa: E402

OUT = os.path.join(ROOT, 'assets')

# "Cold Water": the dive loop. D minor, a slow pulse like a heartbeat under the ice. A B A C.
PULSE = "k . . . . . h . k . . . . . h . k . . . . . h . k . . . s . h ."
DIVE = [
    [   # A: Dm | Bb
        voice("triangle", 100, "D2 . . . . . . . A2 . . . . . . . Bb1 . . . . . . . F2 . . . . . . ."),
        voice("square", 25, "D4 F4 A4 F4 D4 F4 A4 F4 D4 F4 A4 F4 D4 F4 A4 F4 D4 F4 Bb4 F4 D4 F4 Bb4 F4 D4 F4 Bb4 F4 D4 F4 Bb4 F4"),
        voice("noise", 25, PULSE),
        None,
    ],
    [   # B: melody over Gm | A
        voice("triangle", 100, "G1 . . . . . . . D2 . . . . . . . A1 . . . . . . . E2 . . . C#2 . . ."),
        voice("square", 50, "D5 . . . F5 . E5 . D5 . . . A4 . . . C#5 . . . E5 . . . A5 . . . G5 . F5 E5"),
        voice("noise", 25, PULSE),
        None,
    ],
    [   # C: Bb | C | A, rising back up
        voice("triangle", 100, "Bb1 . . . F2 . . . C2 . . . G2 . . . A1 . . . E2 . . . A1 . C#2 . E2 . A2 ."),
        voice("square", 50, "F5 . . . D5 . . . E5 . . . C5 . . . C#5 . . . E5 . . . A5 . . . . . - ."),
        voice("noise", 25, "k . . . . . h . k . . . . . h . k . . . s . h . k . s . s s s s"),
        None,
    ],
]

# Title: open water, a slow arpeggio. A minor, 8 frames a step.
TITLE = [
    [
        voice("triangle", 100, "A4 . . . . . . . E5 . . . . . . . D5 . . . C5 . . . B4 . . . . . - ."),
        voice("square", 25, "A3 C4 E4 A4 E4 C4 A3 C4 F3 A3 C4 F4 C4 A3 F3 A3 D3 F3 A3 D4 A3 F3 D3 F3 E3 G#3 B3 E4 B3 G#3 E3 G#3"),
        None,
        None,
    ],
    [
        voice("triangle", 100, "C5 . . . . . . . B4 . . . A4 . . . G4 . . . . . E4 . . . . . . . - ."),
        voice("square", 25, "A3 C4 E4 A4 E4 C4 A3 C4 G3 B3 D4 G4 D4 B3 G3 B3 F3 A3 C4 F4 C4 A3 F3 A3 E3 G#3 B3 E4 B3 G#3 E3 E3"),
        None,
        None,
    ],
]

CLEAR = [[   # 5 frames a step
    voice("square", 50, "A4 . C5 . E5 . A5 . - . E5 . A5 . C6 . . . B5 . . . A5 . . . . . - . . ."),
    voice("triangle", 100, "A2 . . . E3 . . . A2 . . . C3 . . . D3 . . . E3 . . . A2 . . . - . . ."),
    voice("noise", 25, "k . h . s . h . k . k . s . s s k . . . s . . . k . . . . . . ."),
    None,
]]
OVER = [[   # 8 frames a step
    voice("square", 50, "E5 . . . D5 . . . C5 . . . B4 . . . A4 . . . . . . . . . . . - . . ."),
    voice("triangle", 100, "A2 . . . G2 . . . F2 . . . E2 . . . A1 . . . . . . . . . . . - . . ."),
    None,
    None,
]]

TRACKS = {
    "dive.zmt": zmt("COLD WATER", 28, DIVE, [0, 1, 0, 2]),
    "title.zmt": zmt("OPEN WATER", 32, TITLE, [0, 1]),
    "clear.zmt": zmt("CAUGHT", 20, CLEAR, [0]),
    "over.zmt": zmt("LAST BREATH", 32, OVER, [0]),
}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, data in TRACKS.items():
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} bytes")
