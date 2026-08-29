#include <cstdint>
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

// R1-14: pins order modify / cancel-replace (R-7). The whole R-7.2 priority
// matrix (price change / qty up requeue; qty down same price keeps position),
// modify-to-done (R-7.3), immediate execution when the modify makes the order
// marketable (R-7.4), and the single OrderModified before any fills (R-7.5).

namespace me = microsim::engine;
namespace mc = microsim::core;
namespace mb = microsim::book;

using mc::ClientOrderId;
using mc::InstrumentId;
using mc::ModifyOrder;
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

ModifyOrder modify(ParticipantId p, std::uint64_t order_id, std::int64_t new_qty,
                   std::int64_t new_price) {
  return ModifyOrder{.participant = p,
                     .order_id = OrderId{order_id},
                     .new_qty = Qty{new_qty},
                     .new_price = Price{new_price}};
}

template <class T>
std::size_t count(const std::vector<mc::Outbound>& evs) {
  std::size_t n = 0;
  for (const auto& e : evs) {
    n += std::holds_alternative<T>(e);
  }
  return n;
}

// Two same-price resting buys so priority effects are observable: #1 then #2,
// so the front of the bid is #1 until priority changes.
void seed_two_bids(Engine& eng) {
  eng.process(limit(ParticipantId{1}, 1, Side::Buy, 1000, 10));  // order #1 (front)
  eng.process(limit(ParticipantId{9}, 2, Side::Buy, 1000, 10));  // order #2 (behind)
}

}  // namespace

// ----- gate: same rejects as cancel, plus band checks (R-7.1) -----------------

TEST(Modify, UnknownOrderRejected) {
  Engine eng = make_engine();
  const auto ev = eng.process(modify(ParticipantId{1}, 99, 5, 1000));
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::UnknownOrder);
}

TEST(Modify, NonOwnerRejected) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Buy, 1000, 10));
  const auto ev = eng.process(modify(ParticipantId{9}, 1, 5, 1000));
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::NotOrderOwner);
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{10});  // untouched
}

TEST(Modify, TerminalOrderTooLate) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // #1 rests
  eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 10));   // fills #1 fully -> terminal
  const auto ev = eng.process(modify(ParticipantId{1}, 1, 5, 1003));
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(std::get<mc::OrderRejected>(ev.front()).reason, mc::RejectReason::TooLateToModify);
}

TEST(Modify, BandChecksRejectAndLeaveOrderUntouched) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Buy, 1000, 10));  // #1

  EXPECT_EQ(
      std::get<mc::OrderRejected>(eng.process(modify(ParticipantId{1}, 1, 0, 1000)).front()).reason,
      mc::RejectReason::InvalidQty);  // item 5
  EXPECT_EQ(
      std::get<mc::OrderRejected>(eng.process(modify(ParticipantId{1}, 1, 5000, 1000)).front())
          .reason,
      mc::RejectReason::OrderTooLarge);  // item 6
  EXPECT_EQ(
      std::get<mc::OrderRejected>(eng.process(modify(ParticipantId{1}, 1, 10, 100)).front()).reason,
      mc::RejectReason::PriceOutOfBands);  // item 7

  // A rejected modify leaves the resting order exactly as it was.
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{10});
}

// ----- R-7.2 matrix cell: qty decrease, same price -> keeps priority ----------

TEST(Modify, QtyDecreaseSamePriceKeepsPriority) {
  Engine eng = make_engine();
  seed_two_bids(eng);
  ASSERT_EQ(eng.book().front(Side::Buy)->id, OrderId{1});  // #1 is ahead

  const auto ev = eng.process(modify(ParticipantId{1}, 1, 4, 1000));  // 10 -> 4, same price
  ASSERT_EQ(ev.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderModified>(ev.front()));

  EXPECT_EQ(eng.book().front(Side::Buy)->id, OrderId{1});            // still ahead of #2
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{4 + 10});  // #1 shrunk to 4, #2 still 10
}

// ----- R-7.2 matrix cell: qty increase, same price -> loses priority ----------

TEST(Modify, QtyIncreaseSamePriceLosesPriority) {
  Engine eng = make_engine();
  seed_two_bids(eng);

  const auto ev = eng.process(modify(ParticipantId{1}, 1, 15, 1000));  // 10 -> 15, same price
  ASSERT_EQ(ev.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderModified>(ev.front()));

  EXPECT_EQ(eng.book().front(Side::Buy)->id, OrderId{2});             // #2 now ahead
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{15 + 10});  // #1 grew to 15
}

// ----- R-7.2 matrix cell: price change -> loses priority (new level) ----------

TEST(Modify, PriceChangeLosesPriorityAndMovesLevel) {
  Engine eng = make_engine();
  seed_two_bids(eng);

  const auto ev = eng.process(modify(ParticipantId{1}, 1, 10, 999));  // reprice down, same qty
  ASSERT_EQ(ev.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderModified>(ev.front()));

  EXPECT_EQ(eng.book().front(Side::Buy)->id, OrderId{2});        // #2 is best (1000 > 999)
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{10});  // only #2 left at 1000
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{999}), Qty{10});   // #1 moved to 999
}

// ----- R-7.2 matrix cell: qty unchanged, same price -> re-queued (per R-7.2) --

TEST(Modify, NoChangeStillLosesPriority) {
  Engine eng = make_engine();
  seed_two_bids(eng);
  // new_qty == total and same price: not a decrease, so it re-queues at the back
  // (keep_priority is a strict qty decrease only, R-7.2 pseudocode).
  eng.process(modify(ParticipantId{1}, 1, 10, 1000));
  EXPECT_EQ(eng.book().front(Side::Buy)->id, OrderId{2});
}

// ----- R-7.3: new total at or below already filled cancels the remainder ------

TEST(Modify, NewQtyEqualToFilledCancelsRemainder) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));  // #1 rests, 10
  eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 4));    // 4 of #1 fill

  const auto ev = eng.process(modify(ParticipantId{1}, 1, 4, 1003));  // new total == filled
  ASSERT_EQ(ev.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderModified>(ev[0]));  // R-7.5: modified first
  ASSERT_TRUE(std::holds_alternative<mc::OrderCanceled>(ev[1]));
  const auto& c = std::get<mc::OrderCanceled>(ev[1]);
  EXPECT_EQ(c.reason, mc::CancelReason::ModifyToDone);  // R-7.3
  EXPECT_EQ(c.remaining_qty, Qty{6});                   // 10 - 4 withdrawn
  EXPECT_TRUE(eng.book().empty(Side::Sell));            // gone from the book
}

TEST(Modify, NewQtyBelowFilledAlsoCancels) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10));
  eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1003, 4));

  const auto ev = eng.process(modify(ParticipantId{1}, 1, 3, 1003));  // below filled (4)
  ASSERT_EQ(ev.size(), 2u);
  EXPECT_EQ(std::get<mc::OrderCanceled>(ev[1]).reason, mc::CancelReason::ModifyToDone);
}

// ----- R-7.4: a modify that makes the order marketable executes immediately ----

TEST(Modify, RepriceIntoMarketExecutesImmediately) {
  Engine eng = make_engine();
  eng.process(limit(ParticipantId{1}, 1, Side::Sell, 1005, 10));  // #1 ask @1005
  eng.process(limit(ParticipantId{9}, 1, Side::Buy, 1000, 10));   // #2 bid @1000 (rests)

  // Lift #2's price to 1005: now marketable against #1, must trade at once.
  const auto ev = eng.process(modify(ParticipantId{9}, 2, 10, 1005));
  ASSERT_EQ(ev.size(), 4u);
  EXPECT_TRUE(std::holds_alternative<mc::OrderModified>(ev[0]));  // R-7.5: before fills
  EXPECT_EQ(count<mc::Fill>(ev), 2u);                             // maker + taker
  EXPECT_EQ(count<mc::Trade>(ev), 1u);

  const auto& mod = std::get<mc::OrderModified>(ev[0]);
  EXPECT_EQ(mod.new_price, Price{1005});
  EXPECT_EQ(mod.new_qty, Qty{10});
  const auto& tr = std::get<mc::Trade>(ev[3]);
  EXPECT_EQ(tr.price, Price{1005});  // trades at the resting maker's price (R-5.4)
  EXPECT_EQ(tr.qty, Qty{10});
  EXPECT_TRUE(eng.book().empty(Side::Buy));   // #2 fully filled, not resting
  EXPECT_TRUE(eng.book().empty(Side::Sell));  // #1 fully filled
}

// ----- reachable through the Inbound variant dispatch -------------------------

TEST(Modify, ThroughInboundVariant) {
  Engine eng = make_engine();
  eng.process(mc::Inbound{limit(ParticipantId{1}, 1, Side::Buy, 1000, 10)});
  const auto ev = eng.process(mc::Inbound{modify(ParticipantId{1}, 1, 5, 1000)});
  EXPECT_EQ(count<mc::OrderModified>(ev), 1u);
  EXPECT_EQ(eng.book().depth(Side::Buy, Price{1000}), Qty{5});
}
