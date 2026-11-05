#!/usr/bin/env python3
"""Sample a running game's memory in zeal-native's headless console (no breakpoints, fast).

Boots ZOS, runs `exec H:/<game>.bin`, then every `--every` frames reads each watched symbol
(from the .map) and prints one line. Good for checking what an autopilot build is doing.
Usage: trace.py <game.bin> <seconds> sym:len[:fmt] [sym:len[:fmt] ...] [--every 30]
  fmt: b (bytes, default), h (int16 little endian list)
"""
import os, re, struct, subprocess, sys, argparse
import shlex
from emulator import add_arguments, resolve, boot_commands

ap = argparse.ArgumentParser()
ap.add_argument('bin'); ap.add_argument('secs', type=float); ap.add_argument('watch', nargs='+')
ap.add_argument('--every', type=int, default=30)
add_arguments(ap)
a = ap.parse_args()
resolve(a, ap)
binp = os.path.realpath(a.bin)
syms = {}
for l in open(binp[:-4] + '.map'):
    m = re.match(r'\s+([0-9A-F]{8})\s+(_\w+)', l)
    if m: syms[m.group(2)] = int(m.group(1), 16)
watch = []
for w in a.watch:
    parts = w.split(':')
    nm = parts[0] if parts[0].startswith('_') else '_' + parts[0]
    watch.append((nm, syms[nm], int(parts[1]), parts[2] if len(parts) > 2 else 'b'))
FRAME = 167680
cmds = boot_commands(os.path.basename(binp))

n = int(a.secs * 60 / a.every)
for i in range(n):
    cmds.append('run %d' % (FRAME * a.every))
    for nm, ad, ln, fmt in watch:
        cmds.append('rb 0x%x %d' % (ad, ln))
cmds.append('quit')
env = dict(os.environ)
p = subprocess.run([a.emu, '-r', a.rom,
                    '-H', os.path.dirname(binp), '-o', '-g'] + shlex.split(a.args), input='\n'.join(cmds) + '\n',
                   capture_output=True, text=True, env=env, timeout=3600)
vals = []
for line in (p.stdout + p.stderr).split('\n'):
    m = re.search(r'\[DBG\] [0-9a-f]{4}: ((?:[0-9a-f]{2} ?)+)$', line.strip())
    if m: vals.append(bytes(int(x, 16) for x in m.group(1).split()))
# rb may wrap long reads over several lines: regroup by expected lengths
flat = b''.join(vals)
per = sum(w[2] for w in watch)
for i in range(len(flat) // per):
    chunk = flat[i * per:(i + 1) * per]
    out, o = [], 0
    for nm, ad, ln, fmt in watch:
        d = chunk[o:o + ln]; o += ln
        if fmt == 'h':
            out.append('%s=%s' % (nm[1:], list(struct.unpack('<%dh' % (ln // 2), d))))
        else:
            out.append('%s=%s' % (nm[1:], list(d)))
    print('%6.1fs  %s' % ((i + 1) * a.every / 60, '  '.join(out)))
