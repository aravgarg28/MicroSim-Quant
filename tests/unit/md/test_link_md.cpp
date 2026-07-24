#include <gtest/gtest.h>

#include "microsim/md/module_info.hpp"

// R1-01: proves the microsim_md target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(mdLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::md::module_name(), "md");
}
