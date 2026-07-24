#include <gtest/gtest.h>

#include "microsim/persist/module_info.hpp"

// R1-01: proves the microsim_persist target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(persistLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::persist::module_name(), "persist");
}
