#!/usr/bin/env bash
# Compiles the Pulse telemetry parsers and grouper (the C++ files that have no
# Chromium dependencies beyond base/strings) with their gtest unit tests,
# against the small shims in cpp_shim/, and runs them. Needs g++ and
# googletest (apt-get install libgtest-dev).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../chromium"
OUT="${OUT:-${WORK:-/tmp/pulse-native-chromeos}/cpp}"
mkdir -p "$OUT"
T="$SRC/ash/webui/pulse_ui/telemetry"
GTEST_LIBS=()
if echo '#include <gtest/gtest.h>' | g++ -x c++ -E - >/dev/null 2>&1; then
  GTEST_LIBS=(-lgtest -lgtest_main)
fi
g++ -std=c++20 -Wall -Wextra -Werror -O1 -I"$SRC" -I"$HERE/cpp_shim" \
  "$T/proc_parsers.cc" "$T/process_grouper.cc" \
  "$T/proc_parsers_unittest.cc" "$T/process_grouper_unittest.cc" \
  "${GTEST_LIBS[@]}" -pthread -o "$OUT/pulse_telemetry_unittests"
"$OUT/pulse_telemetry_unittests" --gtest_brief=1
