#include <gtest/gtest.h>

#include "microsim/agents/module_info.hpp"

// R1-01: proves the microsim_agents target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(agentsLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::agents::module_name(), "agents");
}
