#include <gtest/gtest.h>

#include "microsim/metrics/module_info.hpp"

// R1-01: proves the microsim_metrics target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(metricsLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::metrics::module_name(), "metrics");
}
