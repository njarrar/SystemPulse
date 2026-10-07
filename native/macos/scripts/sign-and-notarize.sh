#!/bin/bash
# Signs dist/Pulse.app with a Developer ID certificate, sends it to Apple for
# notarization, staples the ticket and rebuilds dist/Pulse-macos-universal.zip.
# A Mac then opens the app with no "couldn't be verified" warning.
#
# Needs these environment variables (CI passes them from repository secrets):
#   MACOS_CERT_P12       base64 of the "Developer ID Application" .p12 export
#   MACOS_CERT_PASSWORD  password set when exporting the .p12
#   APPLE_ID             Apple Account email of the developer account
#   APPLE_APP_PASSWORD   app-specific password from account.apple.com
#   APPLE_TEAM_ID        10-character Team ID from developer.apple.com/account
#
# Run after scripts/build-app.sh.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="${PULSE_OUT:-$(pwd)/dist}"
APP="$OUT/Pulse.app"
ZIP="$OUT/Pulse-macos-universal.zip"

for v in MACOS_CERT_P12 MACOS_CERT_PASSWORD APPLE_ID APPLE_APP_PASSWORD APPLE_TEAM_ID; do
  if [ -z "${!v:-}" ]; then echo "Missing $v" >&2; exit 1; fi
done

WORK="$(mktemp -d)"
KEYCHAIN="$WORK/pulse-signing.keychain-db"
KEYCHAIN_PASSWORD="$(uuidgen)"
OLD_KEYCHAINS="$(security list-keychains -d user | tr -d '"')"
cleanup() {
  # shellcheck disable=SC2086
  security list-keychains -d user -s $OLD_KEYCHAINS || true
  security delete-keychain "$KEYCHAIN" 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

echo "==> Import certificate"
echo "$MACOS_CERT_P12" | base64 --decode > "$WORK/cert.p12"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 3600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security import "$WORK/cert.p12" -k "$KEYCHAIN" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null
# shellcheck disable=SC2086
security list-keychains -d user -s "$KEYCHAIN" $OLD_KEYCHAINS
IDENTITY="$(security find-identity -v -p codesigning "$KEYCHAIN" | awk -F'"' '/Developer ID Application/ {print $2; exit}')"
if [ -z "$IDENTITY" ]; then
  echo "No Developer ID Application identity in the certificate" >&2
  exit 1
fi
echo "Signing as: $IDENTITY"

echo "==> Sign (hardened runtime, secure timestamp)"
codesign --force --options runtime --timestamp --sign "$IDENTITY" --keychain "$KEYCHAIN" "$APP"
codesign --verify --strict --verbose=2 "$APP"

echo "==> Notarize"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$WORK/notarize.zip"
xcrun notarytool submit "$WORK/notarize.zip" \
  --apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID" \
  --wait --timeout 30m --output-format json | tee "$WORK/result.json"
if ! grep -q '"status" *: *"Accepted"' "$WORK/result.json"; then
  ID="$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' "$WORK/result.json" | head -1)"
  [ -n "$ID" ] && xcrun notarytool log "$ID" \
    --apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID" || true
  echo "Apple did not accept the app" >&2
  exit 1
fi

echo "==> Staple"
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
spctl --assess --type execute --verbose=2 "$APP"

(cd "$OUT" && ditto -c -k --keepParent Pulse.app Pulse-macos-universal.zip)
echo "Signed and notarized $APP"
