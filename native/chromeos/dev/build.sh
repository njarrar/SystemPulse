#!/usr/bin/env bash
# Builds the Pulse dev harness: the chrome://pulse page sources from
# ../chromium/ash/webui/pulse_ui/resources compiled against the mock Mojo
# module in mock/. Type-checks everything and runs the plural test.
#
#   dev/build.sh            build into $OUT (default /tmp/pulse-native-chromeos/build; set WORK to move it)
#   TSC=/path/to/tsc dev/build.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TARGET="$(cd "$HERE/.." && pwd)"
PULSE="$(cd "$TARGET/../.." && pwd)"
RES="$TARGET/chromium/ash/webui/pulse_ui/resources"
WORK="${WORK:-/tmp/pulse-native-chromeos}"
OUT="${OUT:-$WORK/build}"
TOOLS="$WORK/tools"
TSC="${TSC:-$TOOLS/node_modules/.bin/tsc}"

if [ ! -x "$TSC" ]; then
  echo "Installing TypeScript into $TOOLS"
  mkdir -p "$TOOLS"
  (cd "$TOOLS" && npm init -y >/dev/null && npm install --silent typescript@5 @types/node@22)
fi

rm -rf "$OUT"
mkdir -p "$OUT/app" "$OUT/test" "$OUT/www/fonts"

# 1. Locale catalog, the same generator the GN action runs.
python3 "$TARGET/chromium/ash/webui/pulse_ui/tools/gen_pulse_catalog.py" \
  --locales-dir "$PULSE/locales" --out-dir "$OUT/gen"

# A second catalog with the en-XA pseudo-locale (40% longer strings), for
# truncation checks and the three-locale menu. Harness: ?catalog=xa
mkdir -p "$OUT/locales-xa"
cp "$PULSE"/locales/*.json "$PULSE/i18n/test/fixtures/en-XA.json" "$OUT/locales-xa/"
python3 "$TARGET/chromium/ash/webui/pulse_ui/tools/gen_pulse_catalog.py" \
  --locales-dir "$OUT/locales-xa" --out-dir "$OUT/gen-xa"

# 2. Stage page sources next to the mock bindings.
cp "$RES"/*.ts "$OUT/app/"
cp "$HERE"/mock/*.ts "$OUT/app/"
cp "$HERE"/test/*.ts "$OUT/test/"
cp "$HERE/tsconfig.json" "$HERE/tsconfig.test.json" "$OUT/"
ln -s "$TOOLS/node_modules" "$OUT/node_modules"
echo '{"type":"module"}' > "$OUT/package.json"

# 3. Type-check and compile: the page, then the node test.
(cd "$OUT" && "$TSC" -p tsconfig.json)
(cd "$OUT" && "$TSC" -p tsconfig.test.json --outDir out-test)
echo "tsc: page and test compiled with no errors"

# 4. Plural engine against i18n/test/fixtures/vectors.json.
PULSE_ROOT="$PULSE" node "$OUT/out-test/test/plural_test.js" "$OUT/gen/pulse_catalog.json"

# 5. Assemble the static site.
cp "$OUT"/out/app/*.js "$OUT/www/"
cp "$RES/pulse_tokens.css" "$HERE/index.html" "$OUT/gen/pulse_catalog.json" "$OUT/www/"
cp "$OUT/gen-xa/pulse_catalog.json" "$OUT/www/pulse_catalog_xa.json"
python3 "$HERE/fetch_fonts.py" "$WORK/fonts-cache"
cp "$WORK"/fonts-cache/*.woff2 "$OUT/www/fonts/"
cp "$WORK/fonts-cache/fonts.css" "$OUT/www/fonts/fonts.css"
sed -i 's#url(fonts/#url(#g' "$OUT/www/fonts/fonts.css"
echo "harness: $OUT/www/index.html"
