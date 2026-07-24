#include <gtest/gtest.h>

#include "microsim/book/module_info.hpp"

// R1-01: proves the microsim_book target builds, links, and is reachable from
// the test suite. Superseded by real coverage as the module's tasks land.
TEST(bookLinkSmoke, ModuleNameIsReachable) {
  EXPECT_STREQ(microsim::book::module_name(), "book");
}
