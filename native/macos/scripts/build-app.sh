#!/bin/bash
# Builds Pulse.app as a universal (arm64 + x86_64) binary for macOS 13+.
#
#   scripts/build-app.sh            -> dist/Pulse.app and dist/Pulse-macos-universal.zip
#   PULSE_PSEUDO=1 scripts/build-app.sh   also bundles the en-XA pseudo-locale
#   PULSE_ARCHS="arm64" scripts/build-app.sh   one architecture only
#
# Steps: regenerate Localizable.xcstrings from ../../locales/*.json, build each
# architecture with SwiftPM, merge them with lipo, write Info.plist
# (LSUIElement), copy the catalog into Contents/Resources, sign ad hoc.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
LOCALES="$ROOT/../../locales"
BUILD="${PULSE_BUILD_DIR:-$ROOT/.build}"
OUT="${PULSE_OUT:-$ROOT/dist}"
ARCHS="${PULSE_ARCHS:-arm64 x86_64}"
VERSION="${PULSE_VERSION:-1.0.0}"
BUILD_NUMBER="${PULSE_BUILD_NUMBER:-1}"
BUNDLE_ID="${PULSE_BUNDLE_ID:-app.pulse.monitor}"
MIN_MACOS="13.0"
CATALOG="$ROOT/Sources/PulseApp/Resources/Localizable.xcstrings"

echo "==> Locale catalog"
swift build -c release --scratch-path "$BUILD" --product pulse-catalog
GEN="$(swift build -c release --scratch-path "$BUILD" --show-bin-path)/pulse-catalog"
"$GEN" --locales "$LOCALES" --out "$CATALOG"
BUNDLED_CATALOG="$CATALOG"
CODE_ARGS=(--locales "$LOCALES")
if [ "${PULSE_PSEUDO:-0}" = "1" ]; then
  BUNDLED_CATALOG="$BUILD/Localizable.pseudo.xcstrings"
  CODE_ARGS+=(--extra "$ROOT/../../i18n/test/fixtures/en-XA.json")
  "$GEN" "${CODE_ARGS[@]}" --out "$BUNDLED_CATALOG"
fi
CODES="$("$GEN" "${CODE_ARGS[@]}" --codes)"

echo "==> Compile ($ARCHS)"
BINS=()
for arch in $ARCHS; do
  triple="$arch-apple-macosx$MIN_MACOS"
  swift build -c release --scratch-path "$BUILD" --triple "$triple" --product Pulse
  BINS+=("$(swift build -c release --scratch-path "$BUILD" --triple "$triple" --show-bin-path)/Pulse")
done

echo "==> Bundle"
APP="$OUT/Pulse.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
lipo -create -output "$APP/Contents/MacOS/Pulse" "${BINS[@]}"
cp "$BUNDLED_CATALOG" "$APP/Contents/Resources/Localizable.xcstrings"

LOCS=""
for c in $CODES; do
  LOCS="$LOCS        <string>$c</string>
"
  mkdir -p "$APP/Contents/Resources/$c.lproj"
done

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>Pulse</string>
    <key>CFBundleExecutable</key>
    <string>Pulse</string>
    <key>CFBundleIdentifier</key>
    <string>$BUNDLE_ID</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleLocalizations</key>
    <array>
$LOCS    </array>
    <key>CFBundleName</key>
    <string>Pulse</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>CFBundleVersion</key>
    <string>$BUILD_NUMBER</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.utilities</string>
    <key>LSMinimumSystemVersion</key>
    <string>$MIN_MACOS</string>
    <key>LSUIElement</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
</dict>
</plist>
PLIST
plutil -lint "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

echo "==> Sign (ad hoc)"
codesign --force --sign - --timestamp=none "$APP"
codesign --verify --verbose=2 "$APP"

echo "==> Check"
lipo -info "$APP/Contents/MacOS/Pulse"
(cd "$OUT" && rm -f Pulse-macos-universal.zip && ditto -c -k --keepParent Pulse.app Pulse-macos-universal.zip)
echo "Built $APP"
