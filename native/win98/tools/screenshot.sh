#!/bin/sh
# Runs pulse98.exe under Xvfb + Wine and saves a PNG of the flyout above the
# tray, inside a Wine virtual desktop (taskbar and notification area).
#
#   tools/screenshot.sh <exe> <out.png> <seconds> [app flags...]
#
# Needs: wine (32-bit), xvfb-run, ImageMagick. Uses $WINEPREFIX as set by the caller.
# PULSE_PRE: optional exe started first (for example a CPU load helper).
set -e
EXE="$1"; OUT="$2"; WAIT="$3"; shift 3
RAW="$OUT.raw.png"
W=1280; H=1024
xvfb-run -a -s "-screen 0 ${W}x${H}x24" sh -c "
  if [ -n '$PULSE_PRE' ]; then wine '$PULSE_PRE' & sleep 3; fi
  wine explorer /desktop=shell,${W}x${H} '$EXE' --show-flyout --tray $* &
  sleep $WAIT
  import -window root '$RAW'
  wineserver -k || true
"
# Keep the right-hand side: flyout, taskbar end and tray. Trim the empty
# desktop above and to the left, then add a little desktop back.
convert "$RAW" -crop 452x${H}+$((W - 452))+0 +repage -trim +repage \
  -background "#008080" -gravity northwest -splice 12x12 -strip "$OUT"
[ -n "$KEEP_RAW" ] || rm -f "$RAW"
