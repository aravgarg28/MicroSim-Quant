#include <optional>

#include <gtest/gtest.h>

#include "microsim/book/fast_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/types.hpp"

// R1-19: pins the FastBook against the same EXCHANGE_RULES.md behavior the
// ReferenceBook is pinned to (price-time priority R-5.2, FIFO joins R-5.6,
// best/front selection, on-demand depth, canonical dump_state for INV-15). These
// are the oracle's own tests re-run against the fast implementation; the
// exhaustive equivalence check is the differential harness (R1-20).

namespace mb = microsim::book;
namespace mc = microsim::core;

using mc::OrderId;
using mc::ParticipantId;
using mc::Price;
using mc::Qty;
using mc::Side;

namespace {

// A band comfortably wider than any price the tests use ($5.00-$15.00, cent tick).
mb::FastBook make_book() {
  return mb::FastBook{Price{500}, Price{1500}};
}

mb::RestingOrder ord(std::uint64_t id, Side side, std::int64_t price, std::int64_t qty,
                     std::uint32_t party = 1) {
  return mb::RestingOrder{.id = OrderId{id},
                          .participant = ParticipantId{party},
                          .side = side,
                          .price = Price{price},
                          .remaining = Qty{qty}};
}

}  // namespace

// ----- price priority: best bid highest, best ask lowest (R-5.2) --------------

TEST(FastBook, BestBidIsHighestBestAskIsLowest) {
  mb::FastBook b = make_book();
  b.add(ord(1, Side::Buy, 1000, 5));
  b.add(ord(2, Side::Buy, 1002, 5));  // more aggressive bid
  b.add(ord(3, Side::Sell, 1010, 5));
  b.add(ord(4, Side::Sell, 1008, 5));  // more aggressive ask

  EXPECT_EQ(b.best(Side::Buy), std::optional<Price>{Price{1002}});
  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1008}});
  EXPECT_EQ(b.front(Side::Buy)->id, OrderId{2});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{4});
}

TEST(FastBook, EmptySideHasNoBestOrFront) {
  mb::FastBook b = make_book();
  EXPECT_TRUE(b.empty(Side::Buy));
  EXPECT_TRUE(b.empty(Side::Sell));
  EXPECT_EQ(b.best(Side::Buy), std::nullopt);
  EXPECT_EQ(b.front(Side::Sell), nullptr);
}

// ----- time priority: FIFO within a level (R-5.2 (b), R-5.6) ------------------

TEST(FastBook, FifoWithinLevel) {
  mb::FastBook b = make_book();
  b.add(ord(10, Side::Sell, 1005, 7));
  b.add(ord(11, Side::Sell, 1005, 3));  // same price, later -> behind
  b.add(ord(12, Side::Sell, 1005, 9));

  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{10});
  EXPECT_EQ(b.depth(Side::Sell, Price{1005}), Qty{19});  // 7+3+9, summed on demand

  b.remove(OrderId{10});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{11});  // next in FIFO
}

// ----- reduce keeps queue position; remove drops it ---------------------------

TEST(FastBook, ReduceKeepsPositionRemoveDropsLevel) {
  mb::FastBook b = make_book();
  b.add(ord(20, Side::Buy, 999, 10));
  b.add(ord(21, Side::Buy, 999, 4));

  b.reduce(OrderId{20}, Qty{6});                   // partial fill in place
  EXPECT_EQ(b.front(Side::Buy)->id, OrderId{20});  // still ahead
  EXPECT_EQ(b.find(OrderId{20})->remaining, Qty{6});
  EXPECT_EQ(b.depth(Side::Buy, Price{999}), Qty{10});  // 6 + 4

  b.remove(OrderId{20});
  b.remove(OrderId{21});
  EXPECT_TRUE(b.empty(Side::Buy));  // best index falls back to none when emptied
  EXPECT_EQ(b.depth(Side::Buy, Price{999}), Qty{0});
  EXPECT_EQ(b.find(OrderId{20}), nullptr);
}

// ----- best-index maintenance when the best level empties ---------------------

TEST(FastBook, BestFallsToNextLevelWhenTopEmpties) {
  mb::FastBook b = make_book();
  b.add(ord(1, Side::Buy, 1000, 5));
  b.add(ord(2, Side::Buy, 1002, 5));   // best bid
  b.add(ord(3, Side::Sell, 1008, 5));  // best ask
  b.add(ord(4, Side::Sell, 1010, 5));

  b.remove(OrderId{2});  // drop the best bid
  EXPECT_EQ(b.best(Side::Buy), std::optional<Price>{Price{1000}});
  b.remove(OrderId{3});  // drop the best ask
  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1010}});
}

// ----- find / membership ------------------------------------------------------

TEST(FastBook, FindReturnsRestingOrderOrNull) {
  mb::FastBook b = make_book();
  b.add(ord(30, Side::Sell, 1007, 12, /*party=*/9));
  const mb::RestingOrder* o = b.find(OrderId{30});
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->participant, ParticipantId{9});
  EXPECT_EQ(o->remaining, Qty{12});
  EXPECT_EQ(b.find(OrderId{999}), nullptr);
}

// ----- dump_state: canonical, best-first, FIFO order (INV-15) -----------------

TEST(FastBook, DumpStateIsCanonical) {
  mb::FastBook b = make_book();
  b.add(ord(1, Side::Buy, 1000, 5));
  b.add(ord(2, Side::Buy, 1001, 6));  // better bid -> listed first
  b.add(ord(3, Side::Sell, 1005, 7));
  b.add(ord(4, Side::Sell, 1005, 8));  // same level, FIFO after #3
  b.add(ord(5, Side::Sell, 1004, 9));  // better ask -> listed first

  const mb::BookState s = b.dump_state();
  ASSERT_EQ(s.bids.size(), 2u);
  EXPECT_EQ(s.bids[0].price, Price{1001});  // best (highest) bid first
  EXPECT_EQ(s.bids[1].price, Price{1000});

  ASSERT_EQ(s.asks.size(), 2u);
  EXPECT_EQ(s.asks[0].price, Price{1004});  // best (lowest) ask first
  ASSERT_EQ(s.asks[1].orders.size(), 2u);
  EXPECT_EQ(s.asks[1].orders[0].id, OrderId{3});  // FIFO within the 1005 level
  EXPECT_EQ(s.asks[1].orders[1].id, OrderId{4});
}

// ----- FastBook dump_state matches ReferenceBook dump_state (INV-15) ----------

TEST(FastBook, DumpStateEqualsReferenceOnSameInput) {
  mb::FastBook fast = make_book();
  mb::ReferenceBook ref;
  const auto both = [&](const mb::RestingOrder& o) {
    fast.add(o);
    ref.add(o);
  };
  both(ord(1, Side::Buy, 1000, 5));
  both(ord(2, Side::Buy, 1001, 6));
  both(ord(3, Side::Sell, 1005, 7));
  both(ord(4, Side::Sell, 1005, 8));
  both(ord(5, Side::Sell, 1004, 9));

  // RestingOrder equality excludes queue_token, so the two independently assigned
  // token streams do not affect the comparison — observable state is identical.
  EXPECT_EQ(fast.dump_state(), ref.dump_state());

  fast.reduce(OrderId{3}, Qty{1});
  ref.reduce(OrderId{3}, Qty{1});
  EXPECT_EQ(fast.dump_state(), ref.dump_state());

  fast.remove(OrderId{2});
  ref.remove(OrderId{2});
  EXPECT_EQ(fast.dump_state(), ref.dump_state());
}

// ----- §15 worked-example book: asks C(10),D(15)@10.03, E(40)@10.05 -----------

TEST(FastBook, WorkedExampleAskSweepState) {
  mb::FastBook b = make_book();
  b.add(ord(/*C=*/101, Side::Sell, 1003, 10));
  b.add(ord(/*D=*/102, Side::Sell, 1003, 15));
  b.add(ord(/*E=*/103, Side::Sell, 1005, 40));
  b.add(ord(/*bid*/ 201, Side::Buy, 1001, 5));

  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1003}});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{101});
  EXPECT_EQ(b.depth(Side::Sell, Price{1003}), Qty{25});

  // MARKET BUY 60 sweeps: C and D fully fill and are removed; E fills 35 of 40,
  // keeping its queue position with 5 remaining.
  b.remove(OrderId{101});
  b.remove(OrderId{102});
  b.reduce(OrderId{103}, Qty{5});

  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1005}});  // 10.03 empty now
  EXPECT_EQ(b.depth(Side::Sell, Price{1005}), Qty{5});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{103});
  EXPECT_LT(*b.best(Side::Buy), *b.best(Side::Sell));
}

// ----- clear empties the book (session end, R-12.3) ---------------------------

TEST(FastBook, ClearEmptiesBothSides) {
  mb::FastBook b = make_book();
  b.add(ord(1, Side::Buy, 1000, 5));
  b.add(ord(2, Side::Sell, 1005, 5));
  b.clear();
  EXPECT_TRUE(b.empty(Side::Buy));
  EXPECT_TRUE(b.empty(Side::Sell));
  EXPECT_EQ(b.find(OrderId{1}), nullptr);
  EXPECT_EQ(b.dump_state(), mb::BookState{});
}
