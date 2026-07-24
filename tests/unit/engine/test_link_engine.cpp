#include <gtest/gtest.h>

#include "microsim/engine/module_info.hpp"

// R1-01: proves the microsim_engine target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(engineLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::engine::module_name(), "engine");
}
