#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/book/order_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/invariants.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/order_registry.hpp"
#include "microsim/engine/venue.hpp"

// R1-17: self-test of the invariant harness. Every checker is fed a hand-broken
// input and must report its invariant — a checker that cannot fail is decoration
// — and a healthy engine run must report nothing.

namespace me = microsim::engine;
namespace mc = microsim::core;
namespace mb = microsim::book;

using mc::ClientOrderId;
using mc::OrderId;
using mc::ParticipantId;
using mc::Price;
using mc::Qty;
using mc::Side;
using me::InvariantViolation;

namespace {

constexpr mc::InstrumentId kInstr{1};
constexpr ParticipantId kP1{1};
constexpr ParticipantId kP9{9};

mc::InstrumentConfig instrument() {
  return mc::InstrumentConfig{.id = kInstr,
                              .symbol = "SIM",
                              .tick_size = 1,
                              .lot_size = 1,
                              .min_price = Price{500},
                              .max_price = Price{1500},
                              .max_order_qty = Qty{1000}};
}

bool has(const std::vector<InvariantViolation>& v, std::string_view id) {
  for (const auto& x : v) {
    if (x.id == id) {
      return true;
    }
  }
  return false;
}

mb::RestingOrder resting(std::uint64_t id, Side side, std::int64_t px, std::int64_t qty,
                         std::uint64_t token) {
  return mb::RestingOrder{.id = OrderId{id},
                          .participant = kP1,
                          .side = side,
                          .price = Price{px},
                          .remaining = Qty{qty},
                          .queue_token = token};
}

// A well-formed two-sided book: bids best-first (1000, 999), asks best-first
// (1005, 1010), tokens increasing within each level.
mb::BookState healthy_book() {
  mb::BookState s;
  s.bids.push_back(
      {Price{1000}, {resting(1, Side::Buy, 1000, 5, 1), resting(2, Side::Buy, 1000, 3, 2)}});
  s.bids.push_back({Price{999}, {resting(3, Side::Buy, 999, 4, 3)}});
  s.asks.push_back({Price{1005}, {resting(4, Side::Sell, 1005, 6, 4)}});
  s.asks.push_back({Price{1010}, {resting(5, Side::Sell, 1010, 2, 5)}});
  return s;
}

}  // namespace

// ----- check_book: each structural invariant trips ----------------------------

TEST(Invariants, HealthyBookHasNoViolations) {
  std::vector<InvariantViolation> v;
  me::check_book(healthy_book(), instrument(), v);
  EXPECT_TRUE(v.empty());
}

TEST(Invariants, CrossedBookTripsInv1AndInv13) {
  mb::BookState s;
  s.bids.push_back({Price{1005}, {resting(1, Side::Buy, 1005, 5, 1)}});  // bid at/above ask
  s.asks.push_back({Price{1000}, {resting(2, Side::Sell, 1000, 5, 2)}});
  std::vector<InvariantViolation> v;
  me::check_book(s, instrument(), v);
  EXPECT_TRUE(has(v, "INV-1"));
  EXPECT_TRUE(has(v, "INV-13"));
}

TEST(Invariants, MisorderedLevelsTripInv2) {
  mb::BookState s;
  s.bids.push_back({Price{999}, {resting(1, Side::Buy, 999, 5, 1)}});    // worse first
  s.bids.push_back({Price{1000}, {resting(2, Side::Buy, 1000, 5, 2)}});  // better second
  std::vector<InvariantViolation> v;
  me::check_book(s, instrument(), v);
  EXPECT_TRUE(has(v, "INV-2"));
}

TEST(Invariants, EmptyLevelTripsInv2) {
  mb::BookState s;
  s.bids.push_back({Price{1000}, {}});  // level with no orders
  std::vector<InvariantViolation> v;
  me::check_book(s, instrument(), v);
  EXPECT_TRUE(has(v, "INV-2"));
}

TEST(Invariants, OutOfBandRestingPriceTripsInv12) {
  mb::BookState s;
  s.bids.push_back({Price{100}, {resting(1, Side::Buy, 100, 5, 1)}});  // below min_price 500
  std::vector<InvariantViolation> v;
  me::check_book(s, instrument(), v);
  EXPECT_TRUE(has(v, "INV-12"));
}

TEST(Invariants, NonIncreasingQueueTokenTripsInv3) {
  mb::BookState s;
  // Same level, tokens out of order (5 then 2): FIFO ordering is broken.
  s.bids.push_back(
      {Price{1000}, {resting(1, Side::Buy, 1000, 5, 5), resting(2, Side::Buy, 1000, 3, 2)}});
  std::vector<InvariantViolation> v;
  me::check_book(s, instrument(), v);
  EXPECT_TRUE(has(v, "INV-3"));
}

// ----- check_orders: order-record invariants ----------------------------------

me::OrderRecord record(std::uint64_t id, std::uint64_t clord, std::int64_t total,
                       std::int64_t filled, me::OrderState state) {
  return me::OrderRecord{.id = OrderId{id},
                         .participant = kP1,
                         .client_order_id = ClientOrderId{clord},
                         .instrument = kInstr,
                         .side = Side::Buy,
                         .type = mc::OrderType::Limit,
                         .price = Price{1000},
                         .total_qty = Qty{total},
                         .filled_qty = Qty{filled},
                         .state = state};
}

TEST(Invariants, OverfillTripsInv4) {
  const std::vector<me::OrderRecord> orders{record(1, 1, 5, 7, me::OrderState::Filled)};  // 7 > 5
  std::vector<InvariantViolation> v;
  me::check_orders(orders, mb::BookState{}, v);
  EXPECT_TRUE(has(v, "INV-4"));
}

TEST(Invariants, NonDenseOrderIdTripsInv9) {
  const std::vector<me::OrderRecord> orders{
      record(2, 1, 5, 0, me::OrderState::Canceled)};  // id 2 at index 0
  std::vector<InvariantViolation> v;
  me::check_orders(orders, mb::BookState{}, v);
  EXPECT_TRUE(has(v, "INV-9"));
}

TEST(Invariants, DuplicateClientIdTripsInv8) {
  const std::vector<me::OrderRecord> orders{
      record(1, 7, 5, 0, me::OrderState::Canceled),
      record(2, 7, 5, 0, me::OrderState::Canceled)};  // same clord 7
  std::vector<InvariantViolation> v;
  me::check_orders(orders, mb::BookState{}, v);
  EXPECT_TRUE(has(v, "INV-8"));
}

TEST(Invariants, BookDepthMismatchTripsInv6) {
  // One live order resting 5 lots, but the book snapshot shows nothing.
  const std::vector<me::OrderRecord> orders{record(1, 1, 5, 0, me::OrderState::Live)};
  std::vector<InvariantViolation> v;
  me::check_orders(orders, mb::BookState{}, v);
  EXPECT_TRUE(has(v, "INV-6"));
}

TEST(Invariants, ConsistentOrdersAndBookHaveNoViolations) {
  const std::vector<me::OrderRecord> orders{record(1, 1, 5, 0, me::OrderState::Live)};
  mb::BookState s;
  s.bids.push_back({Price{1000}, {resting(1, Side::Buy, 1000, 5, 1)}});
  std::vector<InvariantViolation> v;
  me::check_orders(orders, s, v);
  EXPECT_TRUE(v.empty());
}

// ----- check_events: per-trade invariants -------------------------------------

TEST(Invariants, TradeMissingAFillTripsInv5) {
  std::vector<mc::Outbound> events;
  events.push_back(mc::Trade{.trade_id = mc::TradeId{1},
                             .price = Price{1000},
                             .qty = Qty{5},
                             .maker_order_id = OrderId{1},
                             .taker_order_id = OrderId{2},
                             .maker_participant = kP1,
                             .taker_participant = kP9,
                             .aggressor = Side::Buy});
  // Only the maker fill; the taker fill is missing.
  events.push_back(mc::Fill{.order_id = OrderId{1},
                            .participant = kP1,
                            .trade_id = mc::TradeId{1},
                            .price = Price{1000},
                            .qty = Qty{5},
                            .fee = mc::Cash{0},
                            .liquidity = mc::LiquidityFlag::Maker});
  std::vector<InvariantViolation> v;
  me::check_events(events, instrument(), v);
  EXPECT_TRUE(has(v, "INV-5"));
}

TEST(Invariants, TradePriceOutOfBandTripsInv12) {
  std::vector<mc::Outbound> events;
  events.push_back(mc::Trade{.trade_id = mc::TradeId{1},
                             .price = Price{9999},  // above max_price 1500
                             .qty = Qty{5},
                             .maker_order_id = OrderId{1},
                             .taker_order_id = OrderId{2},
                             .maker_participant = kP1,
                             .taker_participant = kP9,
                             .aggressor = Side::Buy});
  std::vector<InvariantViolation> v;
  me::check_events(events, instrument(), v);
  EXPECT_TRUE(has(v, "INV-12"));
}

// ----- check_risk: INV-14 -----------------------------------------------------

TEST(Invariants, OpenOrdersOverLimitTripsInv14) {
  const std::vector<me::ParticipantExposure> ex{{kP1, /*open=*/3, /*worst_long=*/0,
                                                 /*worst_short=*/0,
                                                 mc::ParticipantRisk{.max_open_orders = 2}}};
  std::vector<InvariantViolation> v;
  me::check_risk(ex, v);
  EXPECT_TRUE(has(v, "INV-14"));
}

TEST(Invariants, WorstCasePositionOverLimitTripsInv14) {
  const std::vector<me::ParticipantExposure> ex{
      {kP1, /*open=*/1, /*worst_long=*/11, /*worst_short=*/0,
       mc::ParticipantRisk{.max_position_lots = Qty{10}}}};
  std::vector<InvariantViolation> v;
  me::check_risk(ex, v);
  EXPECT_TRUE(has(v, "INV-14"));
}

// ----- INV-7: terminal orders are absorbing (cross-message) -------------------

TEST(Invariants, ReferencingATerminalOrderTripsInv7) {
  me::Venue venue;
  venue.add_instrument(instrument());
  venue.add_participant(mc::ParticipantConfig{.id = kP1});
  me::MatchingEngine<mb::ReferenceBook> eng{venue, instrument()};
  me::InvariantMonitor mon;

  // Message 1: place then cancel order #1 -> it is terminal at end of message.
  mon.after_message(eng, eng.process(mc::NewOrder{.participant = kP1,
                                                  .client_order_id = ClientOrderId{1},
                                                  .instrument = kInstr,
                                                  .side = Side::Buy,
                                                  .type = mc::OrderType::Limit,
                                                  .qty = Qty{5},
                                                  .price = Price{1000}}));
  mon.after_message(eng, eng.process(mc::CancelOrder{.participant = kP1, .order_id = OrderId{1}}));

  // Message 3: hand a forged event that acts on the now-terminal order #1.
  std::vector<mc::Outbound> forged{mc::OrderCanceled{.order_id = OrderId{1},
                                                     .participant = kP1,
                                                     .remaining_qty = Qty{5},
                                                     .reason = mc::CancelReason::ByRequest}};
  const auto v = mon.after_message(eng, forged);
  EXPECT_TRUE(has(v, "INV-7"));
}

// ----- healthy engine run: no invariant ever trips ----------------------------

TEST(Invariants, HealthyEngineRunIsClean) {
  me::Venue venue;
  venue.add_instrument(instrument());
  venue.add_participant(mc::ParticipantConfig{.id = kP1});
  venue.add_participant(mc::ParticipantConfig{.id = kP9});
  me::MatchingEngine<mb::ReferenceBook> eng{venue, instrument()};
  me::InvariantMonitor mon;

  const auto step = [&](const mc::Inbound& msg) {
    const auto v = mon.after_message(eng, eng.process(msg));
    EXPECT_TRUE(v.empty()) << (v.empty() ? ""
                                         : std::string(v.front().id) + ": " + v.front().detail);
  };

  auto lim = [](ParticipantId p, std::uint64_t clord, Side side, std::int64_t px,
                std::int64_t qty) {
    return mc::NewOrder{.participant = p,
                        .client_order_id = ClientOrderId{clord},
                        .instrument = kInstr,
                        .side = side,
                        .type = mc::OrderType::Limit,
                        .qty = Qty{qty},
                        .price = Price{px}};
  };

  step(lim(kP1, 1, Side::Buy, 1000, 5));  // #1 rests
  step(lim(kP9, 1, Side::Buy, 1000, 3));  // #2 rests behind #1
  // Modify #1 up in qty -> re-queued behind #2. order_id order is now 2,1, but
  // queue_token order is still increasing: INV-3 must NOT trip.
  step(mc::ModifyOrder{
      .participant = kP1, .order_id = OrderId{1}, .new_qty = Qty{9}, .new_price = Price{1000}});
  step(lim(kP9, 2, Side::Sell, 1000, 4));  // trades against the front of the bid
  step(mc::CancelOrder{.participant = kP9, .order_id = OrderId{2}});
  step(mc::SessionEnd{});  // cancels the rest, closes
}
