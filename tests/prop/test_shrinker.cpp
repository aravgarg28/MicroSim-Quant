#include <cstdint>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"

#include "shrinker.hpp"

// R1-18: shrinker unit tests. Against synthetic predicates (independent of the
// engine) the shrinker must reduce a failing stream to a minimal one: it finds
// the shortest failing prefix and deletes every message a failure does not need.

namespace mp = microsim::prop;
namespace mc = microsim::core;

namespace {

mc::Inbound filler(std::uint64_t clord) {
  return mc::NewOrder{.participant = mc::ParticipantId{1},
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = mc::InstrumentId{1},
                      .side = mc::Side::Buy,
                      .type = mc::OrderType::Limit,
                      .qty = mc::Qty{1},
                      .price = mc::Price{1000}};
}

mc::Inbound marker() {
  return mc::CancelOrder{.participant = mc::ParticipantId{1}, .order_id = mc::OrderId{999}};
}

bool contains_marker(const std::vector<mc::Inbound>& s) {
  for (const mc::Inbound& m : s) {
    if (const auto* c = std::get_if<mc::CancelOrder>(&m);
        c != nullptr && c->order_id == mc::OrderId{999}) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(PropShrinker, ReducesToTheSingleOffendingMessage) {
  std::vector<mc::Inbound> scenario;
  for (std::uint64_t i = 0; i < 40; ++i) {
    scenario.push_back(filler(i + 1));
  }
  scenario.insert(scenario.begin() + 17, marker());  // one bad message in the middle

  const auto minimal = mp::shrink(scenario, contains_marker);
  ASSERT_EQ(minimal.size(), 1u);
  EXPECT_TRUE(contains_marker(minimal));
}

TEST(PropShrinker, KeepsAllRequiredMessages) {
  // Predicate needs TWO markers present; the shrinker must keep both.
  std::vector<mc::Inbound> scenario;
  for (std::uint64_t i = 0; i < 30; ++i) {
    scenario.push_back(filler(i + 1));
  }
  scenario.insert(scenario.begin() + 5, marker());
  scenario.insert(scenario.begin() + 20, marker());

  const auto needs_two = [](const std::vector<mc::Inbound>& s) {
    int count = 0;
    for (const mc::Inbound& m : s) {
      if (const auto* c = std::get_if<mc::CancelOrder>(&m);
          c != nullptr && c->order_id == mc::OrderId{999}) {
        ++count;
      }
    }
    return count >= 2;
  };

  const auto minimal = mp::shrink(scenario, needs_two);
  EXPECT_EQ(minimal.size(), 2u);
}

TEST(PropShrinker, FindsShortestFailingPrefix) {
  // Predicate fails once the stream is at least 10 messages long: the minimal
  // failing case is exactly the first 10.
  std::vector<mc::Inbound> scenario;
  for (std::uint64_t i = 0; i < 50; ++i) {
    scenario.push_back(filler(i + 1));
  }
  const auto at_least_ten = [](const std::vector<mc::Inbound>& s) { return s.size() >= 10; };

  const auto minimal = mp::shrink(scenario, at_least_ten);
  EXPECT_EQ(minimal.size(), 10u);
}

TEST(PropShrinker, LeavesAnAlreadyMinimalCaseAlone) {
  std::vector<mc::Inbound> scenario{marker()};
  const auto minimal = mp::shrink(scenario, contains_marker);
  EXPECT_EQ(minimal.size(), 1u);
}
