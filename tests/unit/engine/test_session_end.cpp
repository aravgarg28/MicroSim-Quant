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

// R1-16: pins session end (R-12). OPEN->CLOSED, the deterministic cancel-all
// order (bids best-to-worst, then asks best-to-worst, FIFO within a level),
// MARKET_CLOSED rejects afterward, and the R-12.4 half-tick mark price.

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
using mc::SessionEnd;
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

Engine make_engine(Price initial_reference = Price{}) {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(mc::ParticipantConfig{.id = kP1});
  v.add_participant(mc::ParticipantConfig{.id = kP9});
  return Engine{v, instrument(), initial_reference};
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

// Two price levels a side, two orders on the top bid level, so best-to-worst and
// FIFO-within-level are both observable. Order ids are assigned 1..5 in creation
// order; the canonical cancel order is 1,2,3 (bids) then 4,5 (asks).
void seed_two_sided_book(Engine& eng) {
  eng.process(limit(kP1, 1, Side::Buy, 1000, 5));   // #1 top bid, first
  eng.process(limit(kP9, 1, Side::Buy, 1000, 3));   // #2 top bid, behind #1
  eng.process(limit(kP1, 2, Side::Buy, 999, 4));    // #3 worse bid
  eng.process(limit(kP1, 3, Side::Sell, 1005, 6));  // #4 best ask
  eng.process(limit(kP9, 2, Side::Sell, 1010, 2));  // #5 worse ask
}

}  // namespace

// ----- R-12.3: cancel-all emits in canonical book order -----------------------

TEST(SessionEnd, CancelsAllRestingInCanonicalOrder) {
  Engine eng = make_engine();
  seed_two_sided_book(eng);

  const auto ev = eng.process(SessionEnd{});
  ASSERT_EQ(ev.size(), 5u);

  const std::vector<std::uint64_t> expected_ids{1, 2, 3, 4, 5};
  const std::vector<std::int64_t> expected_remaining{5, 3, 4, 6, 2};
  for (std::size_t i = 0; i < ev.size(); ++i) {
    ASSERT_TRUE(std::holds_alternative<mc::OrderCanceled>(ev[i]));
    const auto& c = std::get<mc::OrderCanceled>(ev[i]);
    EXPECT_EQ(c.order_id, OrderId{expected_ids[i]});
    EXPECT_EQ(c.remaining_qty, Qty{expected_remaining[i]});
    EXPECT_EQ(c.reason, mc::CancelReason::SessionEnd);
  }
  EXPECT_TRUE(eng.book().empty(Side::Buy));
  EXPECT_TRUE(eng.book().empty(Side::Sell));
}

// ----- R-12.1: state transitions OPEN -> CLOSED, and stays closed -------------

TEST(SessionEnd, StateTransitionsAndIsIdempotent) {
  Engine eng = make_engine();
  EXPECT_EQ(eng.session_state(), me::SessionState::Open);
  eng.process(limit(kP1, 1, Side::Buy, 1000, 5));

  eng.process(SessionEnd{});
  EXPECT_EQ(eng.session_state(), me::SessionState::Closed);

  // A second SessionEnd does nothing (already closed).
  EXPECT_TRUE(eng.process(SessionEnd{}).empty());
  EXPECT_EQ(eng.session_state(), me::SessionState::Closed);
}

// ----- R-12.2: while CLOSED, every inbound order message is MARKET_CLOSED ------

TEST(SessionEnd, PostCloseMessagesRejected) {
  Engine eng = make_engine();
  eng.process(limit(kP1, 1, Side::Buy, 1000, 5));  // #1 rests
  eng.process(SessionEnd{});                       // cancels #1, closes

  const auto n = eng.process(limit(kP1, 2, Side::Buy, 1000, 5));
  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(std::get<mc::OrderRejected>(n.front()).reason, mc::RejectReason::MarketClosed);

  const auto c = eng.process(mc::CancelOrder{.participant = kP1, .order_id = OrderId{1}});
  EXPECT_EQ(std::get<mc::OrderRejected>(c.front()).reason, mc::RejectReason::MarketClosed);

  const auto m = eng.process(mc::ModifyOrder{
      .participant = kP1, .order_id = OrderId{1}, .new_qty = Qty{2}, .new_price = Price{1000}});
  EXPECT_EQ(std::get<mc::OrderRejected>(m.front()).reason, mc::RejectReason::MarketClosed);
}

// ----- R-12.4: mark price, all three branches (in half-ticks) -----------------

TEST(SessionEnd, MarkPriceTwoSidedIsSumOfBestBidAndAsk) {
  Engine eng = make_engine();
  seed_two_sided_book(eng);  // best bid 1000, best ask 1005
  eng.process(SessionEnd{});
  EXPECT_EQ(eng.mark_half_ticks(), 1000 + 1005);  // 2 x mid
}

TEST(SessionEnd, MarkPriceFallsBackToLastTrade) {
  Engine eng = make_engine();
  eng.process(limit(kP1, 1, Side::Sell, 1005, 10));  // rests
  eng.process(limit(kP9, 1, Side::Buy, 1005, 10));   // trades at 1005, book empties
  ASSERT_TRUE(eng.book().empty(Side::Buy));
  ASSERT_TRUE(eng.book().empty(Side::Sell));

  const auto ev = eng.process(SessionEnd{});
  EXPECT_TRUE(ev.empty());                     // nothing left to cancel
  EXPECT_EQ(eng.mark_half_ticks(), 2 * 1005);  // 2 x last trade
}

TEST(SessionEnd, MarkPriceFallsBackToInitialReference) {
  Engine eng = make_engine(Price{1200});  // no book, no trades
  eng.process(SessionEnd{});
  EXPECT_EQ(eng.mark_half_ticks(), 2 * 1200);  // 2 x initial reference
}

// ----- INV-17 skeleton: lots are conserved across a closed session ------------

TEST(SessionEnd, PositionsAreZeroSumAtClose) {
  Engine eng = make_engine();
  eng.process(limit(kP1, 1, Side::Sell, 1000, 7));  // rests
  eng.process(limit(kP9, 1, Side::Buy, 1000, 7));   // trades 7
  eng.process(SessionEnd{});
  // Every lot bought by someone was sold by someone: the tally sums to zero.
  EXPECT_EQ(eng.risk().position(kP1) + eng.risk().position(kP9), 0);
}
