#!/usr/bin/env bash
# Frames per emulated second from the game's own frame counter (headless, autopilot build).
# Usage: tools/fps.sh <underice.bin> <seconds>   prints: second, frames, dive, state, depth, live entities' air
set -euo pipefail
D="$(cd "$(dirname "$0")" && pwd)"
python3 "$D/trace.py" "$1" "$2" frame_count:1 game:18 --every 60 | python3 -c '
import sys, re
prev = None
for l in sys.stdin:
    m = re.match(r"\s*([\d.]+)s\s+frame_count=\[(\d+)\]\s+game=\[(.*)\]", l)
    if not m: continue
    t, fc = float(m.group(1)), int(m.group(2)); g = [int(x) for x in m.group(3).split(",")]
    if prev is not None:
        print("%5.0fs fps=%3d dive=%d state=%d depth=%2d air=%4d carried=%d banked=%d/%d lives=%d" % (t, (fc - prev) % 256, g[0]+1, g[15], g[11], g[2]+256*g[3], g[7], g[9], g[10], g[1]))
    prev = fc
'
