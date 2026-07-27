#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <gtest/gtest.h>

#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"

// R1-04: pins the message/event structs and reason-code enums against
// EXCHANGE_RULES.md §3/§4/§5/§14 and the MATCHING_ENGINE_SPEC I/O lists. Layout
// (trivial-copyability, size budget) is enforced by static_assert in the headers
// and re-stated here; this file covers the enum tables and struct field wiring.

namespace mc = microsim::core;

// ----- layout, re-stated so a regression shows up as a failing test ----------

static_assert(std::is_trivially_copyable_v<mc::NewOrder>);
static_assert(std::is_trivially_copyable_v<mc::Trade>);
static_assert(sizeof(mc::Trade) <= 64);
static_assert(sizeof(mc::Fill) <= 64);
static_assert(sizeof(mc::NewOrder) <= 64);

// ----- R-3.2: NewOrder field wiring ------------------------------------------

TEST(NewOrder, HoldsAllFields) {
  mc::NewOrder o{.participant = mc::ParticipantId{3},
                 .client_order_id = mc::ClientOrderId{99},
                 .instrument = mc::InstrumentId{1},
                 .side = mc::Side::Buy,
                 .type = mc::OrderType::Limit,
                 .qty = mc::Qty{25},
                 .price = mc::Price{1003}};
  EXPECT_EQ(o.participant.value(), 3u);
  EXPECT_EQ(o.client_order_id.value(), 99u);
  EXPECT_EQ(o.side, mc::Side::Buy);
  EXPECT_EQ(o.type, mc::OrderType::Limit);
  EXPECT_EQ(o.qty.lots(), 25);
  EXPECT_EQ(o.price.ticks(), 1003);
}

TEST(NewOrder, MarketOrderPriceIsDefaultZero) {
  // R-3.2: a MARKET order has no price; the convention is the default Price{}.
  mc::NewOrder o{};
  o.type = mc::OrderType::Market;
  EXPECT_EQ(o.price.ticks(), 0);
}

// ----- Inbound / Outbound variant dispatch -----------------------------------

TEST(Inbound, VariantDispatch) {
  mc::Inbound m = mc::CancelOrder{.participant = mc::ParticipantId{2}, .order_id = mc::OrderId{7}};
  ASSERT_TRUE(std::holds_alternative<mc::CancelOrder>(m));
  EXPECT_EQ(std::get<mc::CancelOrder>(m).order_id.value(), 7u);
}

TEST(Outbound, CarriesTradeAndFill) {
  mc::Outbound e = mc::Fill{.order_id = mc::OrderId{5},
                            .participant = mc::ParticipantId{2},
                            .trade_id = mc::TradeId{1},
                            .price = mc::Price{1003},
                            .qty = mc::Qty{10},
                            .fee = mc::Cash{20},
                            .liquidity = mc::LiquidityFlag::Taker};
  const auto& f = std::get<mc::Fill>(e);
  EXPECT_EQ(f.liquidity, mc::LiquidityFlag::Taker);
  EXPECT_EQ(f.fee.minor(), 20);
}

// ----- R-14: reason-code enum tables -----------------------------------------

TEST(RejectReason, EveryValueHasAUniqueName) {
  std::set<std::string> names;
  for (mc::RejectReason r : mc::kAllRejectReasons) {
    const char* s = mc::to_cstr(r);
    ASSERT_NE(s, nullptr);
    EXPECT_GT(std::strlen(s), 0u);
    EXPECT_STRNE(s, "?") << "unnamed RejectReason";
    EXPECT_TRUE(names.insert(s).second) << "duplicate name: " << s;
  }
  EXPECT_EQ(names.size(), mc::kAllRejectReasons.size());
}

TEST(RejectReason, RoundTripsThroughName) {
  for (mc::RejectReason r : mc::kAllRejectReasons) {
    mc::RejectReason back{};
    ASSERT_TRUE(mc::from_cstr(mc::to_cstr(r), back)) << mc::to_cstr(r);
    EXPECT_EQ(back, r);
  }
  mc::RejectReason unused{};
  EXPECT_FALSE(mc::from_cstr("NOT_A_REASON", unused));
}

TEST(RejectReason, MatchesSpecCanonicalNames) {
  // Spot-check the exact strings from EXCHANGE_RULES.md §14.
  EXPECT_STREQ(mc::to_cstr(mc::RejectReason::DuplicateClientOrderId), "DUPLICATE_CLIENT_ORDER_ID");
  EXPECT_STREQ(mc::to_cstr(mc::RejectReason::PriceOutOfBands), "PRICE_OUT_OF_BANDS");
  EXPECT_STREQ(mc::to_cstr(mc::RejectReason::MarketClosed), "MARKET_CLOSED");
}

TEST(CancelReason, EveryValueHasAUniqueNameAndRoundTrips) {
  std::set<std::string> names;
  for (mc::CancelReason r : mc::kAllCancelReasons) {
    const char* s = mc::to_cstr(r);
    EXPECT_STRNE(s, "?");
    EXPECT_TRUE(names.insert(s).second);
    mc::CancelReason back{};
    ASSERT_TRUE(mc::from_cstr(s, back));
    EXPECT_EQ(back, r);
  }
  EXPECT_EQ(names.size(), mc::kAllCancelReasons.size());
}

TEST(OrderTypeAndLiquidity, Names) {
  EXPECT_STREQ(mc::to_cstr(mc::OrderType::Limit), "LIMIT");
  EXPECT_STREQ(mc::to_cstr(mc::OrderType::Market), "MARKET");
  EXPECT_STREQ(mc::to_cstr(mc::LiquidityFlag::Maker), "MAKER");
  EXPECT_STREQ(mc::to_cstr(mc::LiquidityFlag::Taker), "TAKER");
}
