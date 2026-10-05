// Minimal stand-in for Chromium's base/strings/string_number_conversions.h
// (see string_util.h in this folder). Same contract: the whole input must be
// a number, no leading or trailing whitespace.
#ifndef PULSE_SHIM_BASE_STRINGS_STRING_NUMBER_CONVERSIONS_H_
#define PULSE_SHIM_BASE_STRINGS_STRING_NUMBER_CONVERSIONS_H_

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace base {

inline bool StringToUint64(std::string_view s, uint64_t* out) {
  if (s.empty() || s[0] == '-' || s[0] == '+' || s[0] == ' ') {
    *out = 0;
    return false;
  }
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), *out);
  return ec == std::errc() && ptr == s.data() + s.size();
}

inline bool StringToDouble(std::string_view s, double* out) {
  if (s.empty() || s[0] == ' ') {
    return false;
  }
  std::string copy(s);
  char* end = nullptr;
  *out = std::strtod(copy.c_str(), &end);
  return end == copy.c_str() + copy.size();
}

}  // namespace base

#endif  // PULSE_SHIM_BASE_STRINGS_STRING_NUMBER_CONVERSIONS_H_
