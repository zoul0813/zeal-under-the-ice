#!/usr/bin/env bash
# Record gameplay video (no audio) from Zeal-NativeEmulator on a virtual X display.
# Usage: tools/record.sh <game.bin> <out.mp4> <seconds> [start_delay_seconds]
# Build an autopilot binary first so there is someone playing:
# Configure a bot build with zde exec cmake, then build with zde cmake build-ap.
# The autopilot build leaves the title by itself after 2 s.
set -euo pipefail
if [ "${1:-}" = --help ] || [ "$#" -lt 3 ]; then
  echo 'Usage: tools/record.sh <game.bin> <out.mp4> <seconds> [title_wait_seconds]'
  echo 'Linux/X11: requires Xvfb, ffmpeg, Python Xlib; set ZEAL_NATIVE and ZOS_ROM.'
  [ "${1:-}" = --help ] && exit 0
  exit 2
fi
HERE="$(cd "$(dirname "$0")" && pwd)"
EMU="${ZEAL_NATIVE:-zeal-native}"
ROM="${ZOS_ROM:-${ZOS_PATH:-${ZDE_PATH:+$ZDE_PATH/home/Zeal-8-bit-OS}}/build/os_with_romdisk.img}"
command -v "$EMU" >/dev/null 2>&1 || { echo 'Set ZEAL_NATIVE to the zeal-native executable' >&2; exit 1; }
[ -f "$ROM" ] || { echo 'Set ZOS_ROM to a ZOS ROM image' >&2; exit 1; }
BIN="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$1")"; OUTF="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$2")"; SECS="$3"; TITLE="${4:-0}"
export DISPLAY="${ZPLAY_DISPLAY:-:99}"
XT="$HERE/xtype.py"
pgrep -f "Xvfb $DISPLAY" >/dev/null || { (Xvfb "$DISPLAY" -screen 0 1280x800x24 >/dev/null 2>&1 &); sleep 2; }
mkdir -p "$(dirname "$OUTF")"
LOG="$(mktemp -d)"
"$EMU" -r "$ROM" -H "$(dirname "$BIN")" >"$LOG/emu.log" 2>&1 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; rm -rf "$LOG"' EXIT
sleep 4
python3 "$XT" "cd H:/\nexec $(basename "$BIN")\n"
sleep 0.5
# the emulator window is 640x480 (2x) at +320+160 on the 1280x800 display
sleep "$TITLE"
ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -framerate 30 -video_size 640x480 -i "$DISPLAY+320,160" -t "$SECS" \
       -an -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart "$OUTF" &
FF=$!
wait $FF
echo "$OUTF"
