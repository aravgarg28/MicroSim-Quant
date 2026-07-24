#include <gtest/gtest.h>

#include "microsim/accounting/module_info.hpp"

// R1-01: proves the microsim_accounting target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(accountingLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::accounting::module_name(), "accounting");
}
