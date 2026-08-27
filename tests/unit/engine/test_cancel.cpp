#include <variant>

#include <gtest/gtest.h>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/venue.hpp"

// R1-13: pins order cancellation (R-6). A participant may cancel only its own
// resting order; unknown and already-terminal orders reject with distinct
// reasons (the distinction matters for latency experiments).

namespace me = microsim::engine;
namespace mc = microsim::core;
namespace mb = microsim::book;

using mc::CancelOrder;
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

Engine make_engine() {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(mc::ParticipantConfig{.id = ParticipantId{1}});
  v.add_participant(mc::ParticipantConfig{.id = ParticipantId{9}});
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

template <class T>
std::size_t count(const std::vector<mc::Outbound>& evs) {
  std::size_t n = 0;
  for (const auto& e : evs) {
    n += std::holds_alternative<T>(e);
  }
  return n;
}

}  // namespace

// ----- cancel a resting order (R-6.2) -----------------------------------------

TEST(Cancel, RestingOrderRemovedAndReported) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Buy, 1000, 12));  // order #1 rests
  ASSERT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{12});

  const auto ev = eng.process(CancelOrder{.participant = ParticipantId{1}, .order_id = OrderId{1}});
  ASSERT_EQ(ev.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderCanceled>(ev.front()));
  const auto& c = std::get<mc::OrderCanceled>(ev.front());
  EXPECT_EQ(c.reason, mc::CancelReason::ByRequest);
  EXPECT_EQ(c.remaining_qty, Qty{12});
  EXPECT_TRUE(eng.book().empty(Side::Buy));  // gone from the book
}

// ----- unknown order (R-6.3) --------------------------------------------------

TEST(Cancel, UnknownOrderRejected) {
  Engine eng = make_engine();
  const auto ev =
      eng.process(CancelOrder{.participant = ParticipantId{1}, .order_id = OrderId{99}});
  ASSERT_EQ(ev.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderRejected>(ev.front()));
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::UnknownOrder);
}

// ----- not the owner (R-6.1) --------------------------------------------------

TEST(Cancel, NonOwnerRejected) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Buy, 1000, 5));  // owned by P1
  const auto ev = eng.process(CancelOrder{.participant = ParticipantId{9}, .order_id = OrderId{1}});
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::NotOrderOwner);
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{5});  // untouched
}

// ----- terminal order: too late (R-6.3) ---------------------------------------

TEST(Cancel, FilledOrderIsTooLate) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // #1 rests
  eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 10));   // #2 fills #1 fully
  // #1 is now FILLED (terminal); canceling it is too late, not "unknown".
  const auto ev = eng.process(CancelOrder{.participant = ParticipantId{1}, .order_id = OrderId{1}});
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::TooLateToCancel);
}

// ----- canceling a partially filled resting order reports the remainder -------

TEST(Cancel, PartiallyFilledRestingReportsRemaining) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // #1 rests, 10
  eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 4));    // takes 4 of #1
  const auto ev = eng.process(CancelOrder{.participant = ParticipantId{1}, .order_id = OrderId{1}});
  ASSERT_EQ(count<mc::OrderCanceled>(ev), 1u);
  EXPECT_EQ(std::get<mc::OrderCanceled>(ev.front()).remaining_qty, Qty{6});  // 10 - 4
  EXPECT_TRUE(eng.book().empty(Side::Sell));
}

// ----- double cancel: second is too late --------------------------------------

TEST(Cancel, SecondCancelIsTooLate) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Buy, 1000, 5));
  const CancelOrder c{.participant = ParticipantId{1}, .order_id = OrderId{1}};
  EXPECT_EQ(count<mc::OrderCanceled>(eng.process(c)), 1u);
  const auto ev2 = eng.process(c);
  EXPECT_EQ(std::get<mc::OrderRejected>(ev2.front()).reason, mc::RejectReason::TooLateToCancel);
}

// ----- reachable through the Inbound variant dispatch -------------------------

TEST(Cancel, ThroughInboundVariant) {
  Engine eng = make_engine();
  eng.process(mc::Inbound{limit(ParticipantId{1}, 1, Side::Buy, 1000, 5)});
  const auto ev = eng.process(
      mc::Inbound{CancelOrder{.participant = ParticipantId{1}, .order_id = OrderId{1}}});
  EXPECT_EQ(count<mc::OrderCanceled>(ev), 1u);
}
