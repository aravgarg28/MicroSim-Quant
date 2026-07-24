#include <gtest/gtest.h>

#include "microsim/core/module_info.hpp"

// R1-01: proves the microsim_core target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(coreLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::core::module_name(), "core");
}
