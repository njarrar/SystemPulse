// Minimal stand-in for Chromium's base/strings/string_util.h, enough to build
// the Pulse telemetry parsers and their tests outside a Chromium checkout.
// Only the functions those files call, with the same signatures.
#ifndef PULSE_SHIM_BASE_STRINGS_STRING_UTIL_H_
#define PULSE_SHIM_BASE_STRINGS_STRING_UTIL_H_

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace base {

inline constexpr char kWhitespaceASCII[] = " \t\n\v\f\r";

enum TrimPositions { TRIM_NONE = 0, TRIM_LEADING = 1, TRIM_TRAILING = 2, TRIM_ALL = 3 };

inline std::string_view TrimWhitespaceASCII(std::string_view s, TrimPositions p) {
  if (p & TRIM_LEADING) {
    const size_t b = s.find_first_not_of(kWhitespaceASCII);
    s = b == std::string_view::npos ? std::string_view() : s.substr(b);
  }
  if (p & TRIM_TRAILING) {
    const size_t e = s.find_last_not_of(kWhitespaceASCII);
    s = e == std::string_view::npos ? std::string_view() : s.substr(0, e + 1);
  }
  return s;
}

inline bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.substr(0, prefix.size()) == prefix;
}

inline std::string ToLowerASCII(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

}  // namespace base

#endif  // PULSE_SHIM_BASE_STRINGS_STRING_UTIL_H_
