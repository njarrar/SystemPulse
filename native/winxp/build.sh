#!/bin/sh
# Builds pulse.exe for Windows XP SP2/SP3 (32-bit x86) with mingw-w64.
#   ./build.sh            build into $BUILD (default: ./build)
#   ./build.sh test       also run the host unit tests for the i18n port
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BUILD=${BUILD:-$HERE/build}
CROSS=${CROSS:-i686-w64-mingw32-}
mkdir -p "$BUILD"

# 1. Native catalog from locales/*.json
python3 "$HERE/tools/gen_catalog.py" "$ROOT/locales" "$BUILD"

if [ "$1" != "test" ] || command -v "${CROSS}gcc" >/dev/null 2>&1; then
  python3 "$HERE/tools/make_icon.py" "$BUILD/pulse.ico"

  # 2. Resources (icon, version, STRINGTABLE)
  ${CROSS}windres -I "$BUILD" -I "$HERE/src" "$HERE/src/pulse.rc" -O coff -o "$BUILD/pulse_res.o"

  # 3. Code. _WIN32_WINNT 0x0501 keeps the headers to XP-era APIs.
  DEFS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D_WIN32_IE=0x0600 -DUNICODE -D_UNICODE -DSELFTEST_IN_APP"
  CFLAGS="-Os -Wall -Wno-unknown-pragmas -march=i686 -I$BUILD -I$HERE/src $DEFS"
  ${CROSS}gcc $CFLAGS -c "$HERE/src/i18n.c" -o "$BUILD/i18n.o"
  ${CROSS}gcc $CFLAGS -c "$HERE/tests/test_i18n.c" -o "$BUILD/selftest.o"
  ${CROSS}g++ $CFLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics -c "$HERE/src/telemetry.cpp" -o "$BUILD/telemetry.o"
  ${CROSS}g++ $CFLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics -c "$HERE/src/app.cpp" -o "$BUILD/app.o"
  ${CROSS}g++ -mwindows -static -static-libgcc -static-libstdc++ -s \
    -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 \
    -Wl,--major-os-version,5,--minor-os-version,1 \
    -o "$BUILD/pulse.exe" "$BUILD/app.o" "$BUILD/telemetry.o" "$BUILD/i18n.o" "$BUILD/selftest.o" "$BUILD/pulse_res.o" \
    -lgdiplus -lpsapi -liphlpapi -lshell32 -luser32 -lgdi32 -ladvapi32
  ls -l "$BUILD/pulse.exe"
  python3 "$HERE/tools/check_xp_imports.py" "$BUILD/pulse.exe" "${CROSS}objdump"
fi

# 4. Host unit tests for the plural port
if [ "$1" = "test" ]; then
  cc -O1 -Wall -I"$BUILD" -o "$BUILD/test_i18n" "$HERE/tests/test_i18n.c" "$HERE/src/i18n.c" -lm
  python3 "$HERE/tests/vectors2tsv.py" "$ROOT/i18n/test/fixtures/vectors.json" "$BUILD/vectors.tsv"
  "$BUILD/test_i18n" "$BUILD/catalog.tsv" "$BUILD/vectors.tsv"
  if command -v "${CROSS}gcc" >/dev/null 2>&1; then
    ${CROSS}gcc -O1 -mwindows -o "$BUILD/spin.exe" "$HERE/tests/spin.c"
  fi
fi
