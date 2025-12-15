#include <gtest/gtest.h>

#include "../src/visualstudio.h"

TEST(Test1, to_version_range) {
  ASSERT_EQ((to_version_range("17")), "[17,17.65535.65535.65535)");
  ASSERT_EQ((to_version_range("17.0")), "[17.0,17.0.65535.65535)");
  ASSERT_EQ((to_version_range("17.0.0")), "[17.0.0,17.0.0.65535)");
  ASSERT_EQ((to_version_range("17.0.0.0")), "[17.0.0.0,17.0.0.65535)");

  ASSERT_EQ((to_version_range("[17.0,)")), "[17.0,)");
  ASSERT_EQ((to_version_range("{17.0")), "{17.0");
  ASSERT_EQ((to_version_range("")), "");
}
