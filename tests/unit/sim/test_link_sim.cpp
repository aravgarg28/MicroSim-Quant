#include <gtest/gtest.h>

#include "microsim/sim/module_info.hpp"

// R1-01: proves the microsim_sim target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(simLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::sim::module_name(), "sim");
}
