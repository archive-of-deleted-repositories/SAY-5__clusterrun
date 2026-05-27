#include <gtest/gtest.h>

#include <string>

#include "core/version.hpp"

TEST(Smoke, VersionIsNonEmpty) {
  EXPECT_FALSE(std::string(clusterrun::version()).empty());
}
