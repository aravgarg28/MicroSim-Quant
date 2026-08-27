#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/venue.hpp"

// R1-12: pins new-order matching (R-5.2..5.8, R-11.2) end to end against the
// ReferenceBook, including the EXCHANGE_RULES.md §15 worked example.

namespace me = microsim::engine;
namespace mc = microsim::core;
namespace mb = microsim::book;

using mc::ClientOrderId;
using mc::InstrumentId;
using mc::OrderId;
using mc::OrderType;
using mc::ParticipantId;
using mc::Price;
using mc::Qty;
using mc::Side;

namespace {

constexpr InstrumentId kInstr{1};

// $5.00-$15.00 band, 1-cent tick, taker fee 2c/lot, maker rebate 1c/lot.
mc::InstrumentConfig instrument() {
  return mc::InstrumentConfig{
      .id = kInstr,
      .symbol = "SIM",
      .tick_size = 1,
      .lot_size = 1,
      .min_price = Price{500},
      .max_price = Price{1500},
      .max_order_qty = Qty{1000},
      .fees = {.taker_fee_per_lot = mc::Cash{2}, .maker_rebate_per_lot = mc::Cash{1}}};
}

using Engine = me::MatchingEngine<mb::ReferenceBook>;

Engine make_engine() {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(mc::ParticipantConfig{.id = ParticipantId{1}});  // makers + bid
  v.add_participant(mc::ParticipantConfig{.id = ParticipantId{9}});  // taker
  return Engine{v, instrument()};
}

mc::NewOrder limit(ParticipantId p, std::uint64_t clord, Side side, std::int64_t px,
                   std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = OrderType::Limit,
                      .qty = Qty{qty},
                      .price = Price{px}};
}

mc::NewOrder market(ParticipantId p, std::uint64_t clord, Side side, std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = OrderType::Market,
                      .qty = Qty{qty},
                      .price = Price{}};
}

// Count events of a given alternative.
template <class T>
std::size_t count(const std::vector<mc::Outbound>& evs) {
  std::size_t n = 0;
  for (const auto& e : evs) {
    n += std::holds_alternative<T>(e);
  }
  return n;
}

// Collect all Trade events in order.
std::vector<mc::Trade> trades(const std::vector<mc::Outbound>& evs) {
  std::vector<mc::Trade> ts;
  for (const auto& e : evs) {
    if (std::holds_alternative<mc::Trade>(e)) {
      ts.push_back(std::get<mc::Trade>(e));
    }
  }
  return ts;
}

}  // namespace

// ----- resting: a non-marketable limit joins the book, emits only Accepted ----

TEST(MatchingEngine, NonMarketableLimitRests) {
  Engine eng = make_engine();
  const auto ev = eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));
  EXPECT_EQ(ev.size(), 1u);
  EXPECT_EQ(count<mc::OrderAccepted>(ev), 1u);
  EXPECT_EQ(count<mc::Trade>(ev), 0u);
  EXPECT_EQ(eng.book().best(Side::Sell), std::optional<Price>{Price{1003}});
  EXPECT_EQ(eng.book().depth(Side::Sell, Price{1003}), Qty{10});
}

// ----- price improvement: execution at the maker's price (R-5.4) --------------

TEST(MatchingEngine, ExecutionAtMakerPriceNotAggressorPrice) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // maker ask @10.03
  // Aggressive buy limit priced up at 10.10 — marketable, but trades at 10.03.
  const auto ev = eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1010, 4));
  const auto ts = trades(ev);
  ASSERT_EQ(ts.size(), 1u);
  EXPECT_EQ(ts[0].price, Price{1003});  // maker's price, not 1010
  EXPECT_EQ(ts[0].qty, Qty{4});
  EXPECT_EQ(ts[0].aggressor, Side::Buy);
}

// ----- partial fill then rest the remainder (R-5.6) ---------------------------

TEST(MatchingEngine, MarketableLimitFillsThenRestsRemainder) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // 10 available
  const auto ev = eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 25));
  EXPECT_EQ(count<mc::Trade>(ev), 1u);                   // took all 10
  EXPECT_EQ(count<mc::OrderCanceled>(ev), 0u);           // limit remainder rests, no cancel
  EXPECT_EQ(eng.book().best(Side::Sell), std::nullopt);  // ask fully consumed
  EXPECT_EQ(eng.book().best(Side::Buy), std::optional<Price>{Price{1003}});
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1003}), Qty{15});  // 25 - 10 rests
}

// ----- market order with no opposite liquidity -> NO_LIQUIDITY (R-5.5) --------

TEST(MatchingEngine, MarketOrderNoLiquidityCanceled) {
  Engine eng = make_engine();
  const auto ev = eng.process(market(ParticipantId{9}, 1, Side::Buy, 30));
  EXPECT_EQ(count<mc::OrderAccepted>(ev), 1u);
  EXPECT_EQ(count<mc::Trade>(ev), 0u);
  ASSERT_EQ(count<mc::OrderCanceled>(ev), 1u);
  const auto& c = std::get<mc::OrderCanceled>(ev.back());
  EXPECT_EQ(c.reason, mc::CancelReason::NoLiquidity);
  EXPECT_EQ(c.remaining_qty, Qty{30});
}

// ----- fees per fill (R-11.2): taker pays, maker is credited ------------------

TEST(MatchingEngine, FeesAreFlatPerLotSigned) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));
  const auto ev = eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 10));
  mc::Fill maker_fill{};
  mc::Fill taker_fill{};
  for (const auto& e : ev) {
    if (std::holds_alternative<mc::Fill>(e)) {
      const auto& f = std::get<mc::Fill>(e);
      (f.liquidity == mc::LiquidityFlag::Maker ? maker_fill : taker_fill) = f;
    }
  }
  // 10 lots: taker pays 10*2 = 20 (positive cost); maker receives 10*1 = 10 (a
  // credit, stored as negative cost).
  EXPECT_EQ(taker_fill.fee, mc::Cash{20});
  EXPECT_EQ(maker_fill.fee, mc::Cash{-10});
}

// ----- the §15 worked example: MARKET BUY 60 sweeps C(10),D(15)@10.03,E@10.05 -

TEST(MatchingEngine, WorkedExampleMarketBuySixty) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // C -> order 1
  eng.process(limit(ParticipantId{1}, 2, Side::Sell, 1003, 15));  // D -> order 2
  eng.process(limit(ParticipantId{1}, 3, Side::Sell, 1005, 40));  // E -> order 3
  eng.process(limit(ParticipantId{1}, 4, Side::Buy, 1001, 5));    // resting bid -> order 4

  const auto ev = eng.process(market(ParticipantId{9}, 1, Side::Buy, 60));  // order 5
  const auto ts = trades(ev);

  ASSERT_EQ(ts.size(), 3u);
  EXPECT_EQ(ts[0].qty, Qty{10});
  EXPECT_EQ(ts[0].price, Price{1003});  // vs C
  EXPECT_EQ(ts[0].maker_order_id, OrderId{1});
  EXPECT_EQ(ts[1].qty, Qty{15});
  EXPECT_EQ(ts[1].price, Price{1003});  // vs D
  EXPECT_EQ(ts[1].maker_order_id, OrderId{2});
  EXPECT_EQ(ts[2].qty, Qty{35});
  EXPECT_EQ(ts[2].price, Price{1005});  // vs E, price-improved book walk
  EXPECT_EQ(ts[2].maker_order_id, OrderId{3});

  // Taker filled 60/60 -> no NO_LIQUIDITY cancel.
  EXPECT_EQ(count<mc::OrderCanceled>(ev), 0u);
  // Post-state (R-5.8): best bid 10.01 < best ask 10.05, E has 5 left.
  EXPECT_EQ(eng.book().best(Side::Buy), std::optional<Price>{Price{1001}});
  EXPECT_EQ(eng.book().best(Side::Sell), std::optional<Price>{Price{1005}});
  EXPECT_EQ(eng.book().depth(Side::Sell, Price{1005}), Qty{5});

  // Event emission order within the message: Accepted, then per trade
  // (Fill maker, Fill taker, Trade) x3 = 1 + 9 = 10 events.
  EXPECT_EQ(ev.size(), 10u);
  EXPECT_TRUE(std::holds_alternative<mc::OrderAccepted>(ev.front()));
}

// ----- rejects still produce exactly one OrderRejected ------------------------

TEST(MatchingEngine, RejectedOrderEmitsSingleReject) {
  Engine eng = make_engine();
  auto bad = limit(ParticipantId{1}, 1, Side::Buy, 400, 10);  // price below band
  const auto ev = eng.process(bad);
  ASSERT_EQ(ev.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderRejected>(ev.front()));
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::PriceOutOfBands);
}
