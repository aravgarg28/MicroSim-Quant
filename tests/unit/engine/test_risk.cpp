#include <cstdint>
#include <map>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/risk.hpp"
#include "microsim/engine/venue.hpp"

// R1-15: pins the pre-trade risk checks and the signed position tally (R-9).
// Boundary cases (at-limit passes, one-over fails) for the open-order (R-9.1),
// participant size (R-9.2), and worst-case position (R-9.3) checks; the
// same-side-only nature of the worst case; modify delta checks (R-9.5); and an
// INV-14 recompute-from-scratch of the position tally.

namespace me = microsim::engine;
namespace mc = microsim::core;
namespace mb = microsim::book;

using mc::ClientOrderId;
using mc::InstrumentId;
using mc::ModifyOrder;
using mc::OrderId;
using mc::OrderType;
using mc::ParticipantConfig;
using mc::ParticipantId;
using mc::ParticipantRisk;
using mc::Price;
using mc::Qty;
using mc::Side;

namespace {

constexpr InstrumentId kInstr{1};
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

using Engine = me::MatchingEngine<mb::ReferenceBook>;

// P1 carries the risk profile under test; P9 is an unconstrained counterparty.
Engine make_engine(ParticipantRisk p1_risk = {}) {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(ParticipantConfig{.id = kP1, .risk = p1_risk});
  v.add_participant(ParticipantConfig{.id = kP9});
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

bool rejected_with(const std::vector<mc::Outbound>& evs, mc::RejectReason reason) {
  return evs.size() == 1 && std::holds_alternative<mc::OrderRejected>(evs.front()) &&
         std::get<mc::OrderRejected>(evs.front()).reason == reason;
}

bool accepted(const std::vector<mc::Outbound>& evs) {
  return !evs.empty() && std::holds_alternative<mc::OrderAccepted>(evs.front());
}

}  // namespace

// ----- R-9.1: open-order count, boundary + slot recycle (R-9.4) ---------------

TEST(Risk, OpenOrderCountBoundaryAndRecycle) {
  Engine eng = make_engine(ParticipantRisk{.max_open_orders = 2});
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 1, Side::Buy, 1000, 1))));  // 1 open
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 2, Side::Buy, 1000, 1))));  // 2 open (at limit)
  EXPECT_TRUE(rejected_with(eng.process(limit(kP1, 3, Side::Buy, 1000, 1)),
                            mc::RejectReason::MaxOpenOrders));  // 3rd over

  // R-9.4: canceling frees the slot for the *next* message.
  eng.process(mc::CancelOrder{.participant = kP1, .order_id = OrderId{1}});
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 4, Side::Buy, 1000, 1))));  // fits again
}

// ----- R-9.2: participant's stricter per-order size cap ------------------------

TEST(Risk, ParticipantSizeCapBoundary) {
  Engine eng = make_engine(ParticipantRisk{.max_order_qty_lots = Qty{5}});
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 1, Side::Buy, 1000, 5))));  // at cap
  EXPECT_TRUE(rejected_with(eng.process(limit(kP1, 2, Side::Buy, 1000, 6)),
                            mc::RejectReason::RiskOrderTooLarge));  // one over
}

// ----- R-9.3: worst-case position, boundary ------------------------------------

TEST(Risk, WorstCasePositionBoundary) {
  Engine eng = make_engine(ParticipantRisk{.max_position_lots = Qty{10}});
  // First buy: worst case is 10 (nothing else open) -> at the limit, accepted.
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 1, Side::Buy, 1000, 10))));
  // Second buy: both could fill -> worst case 11 > 10, rejected.
  EXPECT_TRUE(
      rejected_with(eng.process(limit(kP1, 2, Side::Buy, 1000, 1)), mc::RejectReason::MaxPosition));
}

// ----- R-9.3: the worst case is same-side only --------------------------------

TEST(Risk, WorstCaseIsSameSideOnly) {
  Engine eng = make_engine(ParticipantRisk{.max_position_lots = Qty{10}});
  // A resting buy of 10 (worst long +10) and a resting sell of 10 (worst short
  // -10) can coexist: neither side's worst case exceeds the limit on its own.
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 1, Side::Buy, 1000, 10))));
  EXPECT_TRUE(accepted(eng.process(limit(kP1, 2, Side::Sell, 1100, 10))));
  // But a second same-side lot on either side breaches it.
  EXPECT_TRUE(rejected_with(eng.process(limit(kP1, 3, Side::Sell, 1100, 1)),
                            mc::RejectReason::MaxPosition));
}

// ----- position tally sign: buys go long, sells go short ----------------------

TEST(Risk, PositionTallySignedByFill) {
  Engine eng = make_engine();
  eng.process(limit(kP9, 1, Side::Sell, 1000, 10));     // P9 rests an ask
  eng.process(limit(kP1, 1, Side::Buy, 1000, 4));       // P1 buys 4 from P9
  EXPECT_EQ(eng.risk().position(kP1), 4);               // long
  EXPECT_EQ(eng.risk().position(kP9), -4);              // short
  EXPECT_EQ(eng.risk().position(ParticipantId{7}), 0);  // never traded
}

// ----- INV-14: the tally equals an independent recompute from the fills -------

TEST(Risk, PositionTallyMatchesRecomputeFromScratch) {
  Engine eng = make_engine();
  std::vector<mc::Trade> trades;
  auto run = [&](const mc::NewOrder& o) {
    for (const auto& ev : eng.process(o)) {
      if (std::holds_alternative<mc::Trade>(ev)) {
        trades.push_back(std::get<mc::Trade>(ev));
      }
    }
  };
  run(limit(kP9, 1, Side::Sell, 1000, 10));  // rests
  run(limit(kP1, 1, Side::Buy, 1000, 4));    // 4 trade
  run(limit(kP1, 2, Side::Buy, 1000, 6));    // 6 trade (P1 long 10, P9 short 10)

  // Recompute from scratch: replay each trade's signed lots onto a fresh tally.
  std::map<ParticipantId, std::int64_t> expect;
  for (const auto& t : trades) {
    expect[t.taker_participant] += mc::sign_of(t.aggressor) * t.qty.lots();
    expect[t.maker_participant] += mc::sign_of(mc::opposite(t.aggressor)) * t.qty.lots();
  }
  for (const auto& [p, lots] : expect) {
    EXPECT_EQ(eng.risk().position(p), lots);
  }
  EXPECT_EQ(eng.risk().position(kP1), 10);
  EXPECT_EQ(eng.risk().position(kP9), -10);
}

// ----- R-9.5: modify re-runs risk on the delta; failure leaves order untouched -

TEST(Risk, ModifyIncreaseBreachingPositionIsRejectedAndUntouched) {
  Engine eng = make_engine(ParticipantRisk{.max_position_lots = Qty{10}});
  eng.process(limit(kP1, 1, Side::Buy, 1000, 10));  // rests at the limit

  // Growing to 11 would make the worst case 11 > 10: rejected, order unchanged.
  const auto ev = eng.process(ModifyOrder{
      .participant = kP1, .order_id = OrderId{1}, .new_qty = Qty{11}, .new_price = Price{1000}});
  EXPECT_TRUE(rejected_with(ev, mc::RejectReason::MaxPosition));
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{10});  // still 10
}

TEST(Risk, ModifyDecreaseWithinLimitSucceeds) {
  Engine eng = make_engine(ParticipantRisk{.max_position_lots = Qty{10}});
  eng.process(limit(kP1, 1, Side::Buy, 1000, 10));
  const auto ev = eng.process(ModifyOrder{
      .participant = kP1, .order_id = OrderId{1}, .new_qty = Qty{8}, .new_price = Price{1000}});
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<mc::OrderModified>(ev.front()));
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{8});
}

TEST(Risk, ModifySizeCapRejected) {
  Engine eng = make_engine(ParticipantRisk{.max_order_qty_lots = Qty{5}});
  eng.process(limit(kP1, 1, Side::Buy, 1000, 5));  // at cap
  const auto ev = eng.process(ModifyOrder{
      .participant = kP1, .order_id = OrderId{1}, .new_qty = Qty{6}, .new_price = Price{1000}});
  EXPECT_TRUE(rejected_with(ev, mc::RejectReason::RiskOrderTooLarge));
}
