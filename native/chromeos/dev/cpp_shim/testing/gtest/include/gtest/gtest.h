// Chromium includes googletest through this path; forward to the system copy
// when available, or fall back to a minimal header-only runner.
#ifndef TESTING_GTEST_INCLUDE_GTEST_GTEST_H_
#define TESTING_GTEST_INCLUDE_GTEST_GTEST_H_

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace pulse_gtest_shim {

struct TestCase {
  const char* suite;
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> r;
  return r;
}

inline int& Failures() {
  static int f = 0;
  return f;
}

inline bool& CurrentFailed() {
  static bool cf = false;
  return cf;
}

struct Registrar {
  Registrar(const char* suite, const char* name, std::function<void()> fn) {
    Registry().push_back({suite, name, std::move(fn)});
  }
};

template <typename A, typename B>
bool Equal(const A& a, const B& b) {
  if constexpr (std::is_convertible_v<A, std::string_view> &&
                std::is_convertible_v<B, std::string_view>) {
    return std::string_view(a) == std::string_view(b);
  } else if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
    return static_cast<std::common_type_t<A, B>>(a) ==
           static_cast<std::common_type_t<A, B>>(b);
  } else {
    return a == b;
  }
}

inline void Fail(const char* file, int line, const char* expr) {
  std::fprintf(stderr, "%s:%d: Failure: %s\n", file, line, expr);
  ++Failures();
  CurrentFailed() = true;
}

}  // namespace pulse_gtest_shim

#define TEST(suite, name)                                                     \
  static void suite##_##name##_Test();                                        \
  static ::pulse_gtest_shim::Registrar suite##_##name##_reg(                  \
      #suite, #name, &suite##_##name##_Test);                                 \
  static void suite##_##name##_Test()

#define EXPECT_TRUE(cond)                                                     \
  do {                                                                        \
    if (!(cond)) ::pulse_gtest_shim::Fail(__FILE__, __LINE__, #cond);         \
  } while (0)

#define EXPECT_FALSE(cond)                                                    \
  do {                                                                        \
    if (cond) ::pulse_gtest_shim::Fail(__FILE__, __LINE__, "!(" #cond ")");   \
  } while (0)

#define ASSERT_TRUE(cond)                                                     \
  do {                                                                        \
    if (!(cond)) {                                                            \
      ::pulse_gtest_shim::Fail(__FILE__, __LINE__, #cond);                    \
      return;                                                                 \
    }                                                                         \
  } while (0)

#define EXPECT_EQ(a, b)                                                       \
  do {                                                                        \
    if (!::pulse_gtest_shim::Equal((a), (b)))                                 \
      ::pulse_gtest_shim::Fail(__FILE__, __LINE__, #a " == " #b);             \
  } while (0)

#define ASSERT_EQ(a, b)                                                       \
  do {                                                                        \
    if (!::pulse_gtest_shim::Equal((a), (b))) {                               \
      ::pulse_gtest_shim::Fail(__FILE__, __LINE__, #a " == " #b);             \
      return;                                                                 \
    }                                                                         \
  } while (0)

#define EXPECT_NEAR(a, b, tol)                                                \
  do {                                                                        \
    if (std::fabs(static_cast<double>(a) - static_cast<double>(b)) >          \
        static_cast<double>(tol))                                             \
      ::pulse_gtest_shim::Fail(__FILE__, __LINE__, #a " ~= " #b);             \
  } while (0)

#define EXPECT_DOUBLE_EQ(a, b) EXPECT_NEAR((a), (b), 1e-9)

__attribute__((weak)) int main(int, char**) {
  int ran = 0;
  for (const auto& tc : ::pulse_gtest_shim::Registry()) {
    ::pulse_gtest_shim::CurrentFailed() = false;
    tc.fn();
    ++ran;
  }
  if (::pulse_gtest_shim::Failures() == 0) {
    std::printf("[==========] %d tests ran. [  PASSED  ]\n", ran);
    return 0;
  }
  std::fprintf(stderr, "[  FAILED  ] %d failure(s) in %d tests.\n",
               ::pulse_gtest_shim::Failures(), ran);
  return 1;
}

#endif  // __has_include(<gtest/gtest.h>)
#endif  // TESTING_GTEST_INCLUDE_GTEST_GTEST_H_

