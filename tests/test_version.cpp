#include <gtest/gtest.h>

#include "arena/version.hpp"

TEST(Version, MatchesTheProjectVersionInCMake) {
    EXPECT_EQ(arena::version(), "0.1.0");
}
