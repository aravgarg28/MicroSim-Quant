#include <gtest/gtest.h>

#include "microsim/strategy/module_info.hpp"

// R1-01: proves the microsim_strategy target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(strategyLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::strategy::module_name(), "strategy");
}
