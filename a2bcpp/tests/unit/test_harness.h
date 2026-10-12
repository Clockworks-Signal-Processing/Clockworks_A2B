/**
 * @file test_harness.h
 * @brief A minimal unit-test harness: no dependencies, so the same tests can later run
 *        on the RP2040.
 *
 * Usage, in one .cpp file per test program:
 * @code
 * #include "test_harness.h"
 * A2B_TEST(AddsNumbers) { A2B_CHECK_EQ(1 + 1, 2); }
 * A2B_TEST_MAIN()
 * @endcode
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TESTS_UNIT_TEST_HARNESS_H_
#define A2BCPP_TESTS_UNIT_TEST_HARNESS_H_

#include <cstdio>
#include <cstring>

namespace a2b {
namespace test {

using TestFunction = void (*)();

struct TestCase {
  const char* name;
  TestFunction function;
};

inline constexpr int kMaxTests = 128;

/** The tests of one program and the failed-check count. */
struct Registry {
  TestCase tests[kMaxTests];
  int testCount;
  int failureCount;
};

inline Registry& TheRegistry() {
  static Registry registry = {};
  return registry;
}

/** Adds a test to the list at static-initialisation time. */
struct Registrar {
  Registrar(const char* name, TestFunction function) {
    Registry& registry = TheRegistry();
    if (registry.testCount < kMaxTests) {
      registry.tests[registry.testCount++] = {name, function};
    } else {
      std::printf("Too many tests; raise kMaxTests\n");
      ++registry.failureCount;
    }
  }
};

/** Runs every registered test; returns the process exit code. */
inline int RunAll() {
  Registry& registry = TheRegistry();
  for (int i = 0; i < registry.testCount; ++i) {
    const int failuresBefore = registry.failureCount;
    registry.tests[i].function();
    std::printf("%s %s\n", (registry.failureCount == failuresBefore) ? "[ OK ]" : "[FAIL]",
                registry.tests[i].name);
  }
  std::printf("%d tests, %d failed checks\n", registry.testCount, registry.failureCount);
  return (registry.failureCount == 0) ? 0 : 1;
}

/** Counts one failed check. */
inline void Fail() { ++TheRegistry().failureCount; }

}  // namespace test
}  // namespace a2b

#define A2B_TEST(name)                                                 \
  static void name();                                                  \
  static const ::a2b::test::Registrar k##name##Registrar(#name, name); \
  static void name()

#define A2B_CHECK(cond)                                                    \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ::a2b::test::Fail();                                                 \
    }                                                                      \
  } while (0)

#define A2B_CHECK_EQ(actual, expected)                                                      \
  do {                                                                                      \
    const long long actualValue = static_cast<long long>(actual);                           \
    const long long expectedValue = static_cast<long long>(expected);                       \
    if (actualValue != expectedValue) {                                                     \
      std::printf("%s:%d: check failed: %s == %s (0x%llX vs 0x%llX)\n", __FILE__, __LINE__, \
                  #actual, #expected, actualValue, expectedValue);                          \
      ::a2b::test::Fail();                                                                  \
    }                                                                                       \
  } while (0)

#define A2B_CHECK_STREQ(actual, expected)                                    \
  do {                                                                       \
    const char* actualText = (actual);                                       \
    const char* expectedText = (expected);                                   \
    if (std::strcmp(actualText, expectedText) != 0) {                        \
      std::printf(                                                           \
          "%s:%d: check failed: %s == %s\n--- actual:\n%s\n"                 \
          "--- expected:\n%s\n",                                             \
          __FILE__, __LINE__, #actual, #expected, actualText, expectedText); \
      ::a2b::test::Fail();                                                   \
    }                                                                        \
  } while (0)

#define A2B_TEST_MAIN() \
  int main() { return ::a2b::test::RunAll(); }

#endif  // A2BCPP_TESTS_UNIT_TEST_HARNESS_H_
