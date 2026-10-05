#!/bin/sh
# Draws the app icons from app_icon.svg. Run once before building in a Chromium checkout.
# Needs rsvg-convert (librsvg) or ImageMagick.
set -e
dir="$(dirname "$0")/../chromium/ash/webui/pulse_ui/resources"
for s in 48 128 256; do
  if command -v rsvg-convert >/dev/null; then
    rsvg-convert -w $s -h $s "$dir/app_icon.svg" -o "$dir/app_icon_$s.png"
  else
    convert -background none -resize ${s}x${s} "$dir/app_icon.svg" "$dir/app_icon_$s.png"
  fi
done
echo "wrote app_icon_48.png, app_icon_128.png, app_icon_256.png"
