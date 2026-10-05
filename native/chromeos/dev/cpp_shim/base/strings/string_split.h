// Minimal stand-in for Chromium's base/strings/string_split.h (see
// string_util.h in this folder).
#ifndef PULSE_SHIM_BASE_STRINGS_STRING_SPLIT_H_
#define PULSE_SHIM_BASE_STRINGS_STRING_SPLIT_H_

#include <string_view>
#include <vector>

#include "base/strings/string_util.h"

namespace base {

enum WhitespaceHandling { KEEP_WHITESPACE, TRIM_WHITESPACE };
enum SplitResult { SPLIT_WANT_ALL, SPLIT_WANT_NONEMPTY };

// Splits on any character in `separators`, like Chromium does.
inline std::vector<std::string_view> SplitStringPiece(std::string_view input,
                                                      std::string_view separators,
                                                      WhitespaceHandling ws,
                                                      SplitResult result) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start <= input.size()) {
    size_t end = input.find_first_of(separators, start);
    if (end == std::string_view::npos) {
      end = input.size();
    }
    std::string_view piece = input.substr(start, end - start);
    if (ws == TRIM_WHITESPACE) {
      piece = TrimWhitespaceASCII(piece, TRIM_ALL);
    }
    if (result == SPLIT_WANT_ALL || !piece.empty()) {
      out.push_back(piece);
    }
    if (end == input.size()) {
      break;
    }
    start = end + 1;
  }
  return out;
}

}  // namespace base

#endif  // PULSE_SHIM_BASE_STRINGS_STRING_SPLIT_H_
