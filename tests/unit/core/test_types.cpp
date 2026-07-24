#include <cstdint>
#include <format>
#include <sstream>
#include <type_traits>
#include <unordered_map>

#include <gtest/gtest.h>

#include "microsim/core/types.hpp"

// R1-03: pins the strong-type rules of docs/numerics/NUMERIC_REPRESENTATION.md.
// The banned operations are proven un-compilable by the compile-fail tests
// registered in tests/CMakeLists.txt; here we pin the *allowed* operations and
// their result types.

namespace mc = microsim::core;

// ----- allowed-operation result types (compile-time) --------------------------

static_assert(std::is_same_v<decltype(mc::Price{1} + std::int64_t{1}), mc::Price>);
static_assert(std::is_same_v<decltype(std::int64_t{1} + mc::Price{1}), mc::Price>);
static_assert(std::is_same_v<decltype(mc::Price{1} - std::int64_t{1}), mc::Price>);
static_assert(std::is_same_v<decltype(mc::Price{2} - mc::Price{1}), std::int64_t>);

static_assert(std::is_same_v<decltype(mc::Qty{1} + mc::Qty{1}), mc::Qty>);
static_assert(std::is_same_v<decltype(mc::Qty{1} - mc::Qty{1}), mc::Qty>);

static_assert(std::is_same_v<decltype(mc::Cash{1} + mc::Cash{1}), mc::Cash>);
static_assert(std::is_same_v<decltype(-mc::Cash{1}), mc::Cash>);

static_assert(std::is_same_v<decltype(mc::SimTime{1} + mc::Duration{1}), mc::SimTime>);
static_assert(std::is_same_v<decltype(mc::SimTime{2} - mc::SimTime{1}), mc::Duration>);
static_assert(std::is_same_v<decltype(mc::Duration{1} + mc::Duration{1}), mc::Duration>);
static_assert(std::is_same_v<decltype(mc::Duration{1} * std::int64_t{3}), mc::Duration>);

// ----- no implicit conversions to/from raw integers ---------------------------

static_assert(!std::is_convertible_v<std::int64_t, mc::Price>);
static_assert(!std::is_convertible_v<mc::Price, std::int64_t>);
static_assert(!std::is_convertible_v<std::int64_t, mc::Qty>);
static_assert(!std::is_convertible_v<mc::Cash, std::int64_t>);
static_assert(!std::is_convertible_v<mc::Price, mc::Qty>);  // cross-type

// ----- constructibility and triviality ----------------------------------------

static_assert(std::is_trivially_copyable_v<mc::Price>);
static_assert(std::is_trivially_copyable_v<mc::OrderId>);
static_assert(sizeof(mc::Price) == sizeof(std::int64_t));
static_assert(sizeof(mc::ParticipantId) == sizeof(std::uint32_t));

// ----- runtime arithmetic -----------------------------------------------------

TEST(Price, TickArithmeticAndDifference) {
  mc::Price best{1001};
  EXPECT_EQ((best + 2).ticks(), 1003);
  EXPECT_EQ((2 + best).ticks(), 1003);
  EXPECT_EQ((best - 1).ticks(), 1000);
  EXPECT_EQ(mc::Price{1003} - mc::Price{1001}, 2);  // difference is int64 ticks
  best += 5;
  EXPECT_EQ(best.ticks(), 1006);
  best -= 6;
  EXPECT_EQ(best.ticks(), 1000);
}

TEST(Price, OrderingIndexesTheBook) {
  EXPECT_LT(mc::Price{1000}, mc::Price{1001});
  EXPECT_GT(mc::Price{1002}, mc::Price{1001});
  EXPECT_EQ(mc::Price{1001}, mc::Price{1001});
}

TEST(Qty, AddSubtractAndCompare) {
  mc::Qty q{10};
  q += mc::Qty{5};
  EXPECT_EQ(q.lots(), 15);
  q -= mc::Qty{3};
  EXPECT_EQ(q.lots(), 12);
  EXPECT_EQ((mc::Qty{4} + mc::Qty{6}).lots(), 10);
  EXPECT_LT(mc::Qty{4}, mc::Qty{6});
  EXPECT_EQ(std::min(mc::Qty{4}, mc::Qty{6}), mc::Qty{4});  // used by the matching loop
}

TEST(Cash, SignedArithmetic) {
  mc::Cash pnl{100};
  pnl -= mc::Cash{150};
  EXPECT_EQ(pnl.minor(), -50);
  EXPECT_EQ((-pnl).minor(), 50);
  EXPECT_EQ((mc::Cash{30} + mc::Cash{20}).minor(), 50);
}

TEST(Time, InstantAndDuration) {
  mc::SimTime t0 = mc::SimTime::zero();
  EXPECT_EQ(t0.ns(), 0);
  mc::SimTime t1 = t0 + mc::Duration{500};
  EXPECT_EQ(t1.ns(), 500);
  EXPECT_EQ((t1 - t0).ns(), 500);  // difference is a Duration
  EXPECT_EQ((mc::Duration{100} * 3).ns(), 300);
  EXPECT_LT(t0, t1);
}

TEST(Side, OppositeAndSign) {
  EXPECT_EQ(mc::opposite(mc::Side::Buy), mc::Side::Sell);
  EXPECT_EQ(mc::opposite(mc::Side::Sell), mc::Side::Buy);
  EXPECT_EQ(mc::sign_of(mc::Side::Buy), 1);
  EXPECT_EQ(mc::sign_of(mc::Side::Sell), -1);
  EXPECT_STREQ(mc::to_cstr(mc::Side::Buy), "BUY");
}

// ----- identifiers ------------------------------------------------------------

TEST(Ids, SequentialIdsIncreaseMonotonically) {
  mc::OrderId a = mc::OrderId::first();
  EXPECT_EQ(a.value(), 1u);
  mc::OrderId b = a.next();
  EXPECT_EQ(b.value(), 2u);
  EXPECT_LT(a, b);  // strictly increasing (INV-9)
  EXPECT_NE(a, b);
}

TEST(Ids, HashableAsMapKeys) {
  std::unordered_map<mc::ParticipantId, int> book;
  book[mc::ParticipantId{7}] = 42;
  EXPECT_EQ(book.at(mc::ParticipantId{7}), 42);
  EXPECT_EQ(book.count(mc::ParticipantId{8}), 0u);
}

// ----- formatting -------------------------------------------------------------

TEST(Format, StdFormatUsesUnitTags) {
  EXPECT_EQ(std::format("{}", mc::Price{1003}), "1003t");
  EXPECT_EQ(std::format("{}", mc::Qty{50}), "50lot");
  EXPECT_EQ(std::format("{}", mc::Cash{-25}), "-25mu");
  EXPECT_EQ(std::format("{}", mc::SimTime{500}), "500ns");
  EXPECT_EQ(std::format("{}", mc::Duration{500}), "500ns");
  EXPECT_EQ(std::format("{}", mc::OrderId{42}), "order#42");
}

TEST(Format, OstreamMatchesFormat) {
  std::ostringstream os;
  os << mc::Price{1003} << ' ' << mc::Side::Sell << ' ' << mc::OrderId{42};
  EXPECT_EQ(os.str(), "1003t SELL order#42");
}
