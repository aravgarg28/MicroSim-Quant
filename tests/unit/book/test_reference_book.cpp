#include <optional>

#include <gtest/gtest.h>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/types.hpp"

// R1-09: pins the ReferenceBook oracle against EXCHANGE_RULES.md price-time
// priority (R-5.2), FIFO joins (R-5.6), best/front selection, on-demand depth,
// and the canonical dump_state used by the differential harness (INV-15). Also
// walks the §15 worked-example book state.

namespace mb = microsim::book;
namespace mc = microsim::core;

using mc::OrderId;
using mc::ParticipantId;
using mc::Price;
using mc::Qty;
using mc::Side;

namespace {

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

TEST(ReferenceBook, BestBidIsHighestBestAskIsLowest) {
  mb::ReferenceBook b;
  b.add(ord(1, Side::Buy, 1000, 5));
  b.add(ord(2, Side::Buy, 1002, 5));  // more aggressive bid
  b.add(ord(3, Side::Sell, 1010, 5));
  b.add(ord(4, Side::Sell, 1008, 5));  // more aggressive ask

  EXPECT_EQ(b.best(Side::Buy), std::optional<Price>{Price{1002}});
  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1008}});
  EXPECT_EQ(b.front(Side::Buy)->id, OrderId{2});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{4});
}

TEST(ReferenceBook, EmptySideHasNoBestOrFront) {
  mb::ReferenceBook b;
  EXPECT_TRUE(b.empty(Side::Buy));
  EXPECT_TRUE(b.empty(Side::Sell));
  EXPECT_EQ(b.best(Side::Buy), std::nullopt);
  EXPECT_EQ(b.front(Side::Sell), nullptr);
}

// ----- time priority: FIFO within a level (R-5.2 (b), R-5.6) ------------------

TEST(ReferenceBook, FifoWithinLevel) {
  mb::ReferenceBook b;
  b.add(ord(10, Side::Sell, 1005, 7));
  b.add(ord(11, Side::Sell, 1005, 3));  // same price, later -> behind
  b.add(ord(12, Side::Sell, 1005, 9));

  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{10});
  EXPECT_EQ(b.depth(Side::Sell, Price{1005}), Qty{19});  // 7+3+9, summed on demand

  b.remove(OrderId{10});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{11});  // next in FIFO
}

// ----- reduce keeps queue position; remove drops it ---------------------------

TEST(ReferenceBook, ReduceKeepsPositionRemoveDropsLevel) {
  mb::ReferenceBook b;
  b.add(ord(20, Side::Buy, 999, 10));
  b.add(ord(21, Side::Buy, 999, 4));

  b.reduce(OrderId{20}, Qty{6});                   // partial fill in place
  EXPECT_EQ(b.front(Side::Buy)->id, OrderId{20});  // still ahead
  EXPECT_EQ(b.find(OrderId{20})->remaining, Qty{6});
  EXPECT_EQ(b.depth(Side::Buy, Price{999}), Qty{10});  // 6 + 4

  b.remove(OrderId{20});
  b.remove(OrderId{21});
  EXPECT_TRUE(b.empty(Side::Buy));  // level dropped when emptied
  EXPECT_EQ(b.depth(Side::Buy, Price{999}), Qty{0});
  EXPECT_EQ(b.find(OrderId{20}), nullptr);
}

// ----- find / membership ------------------------------------------------------

TEST(ReferenceBook, FindReturnsRestingOrderOrNull) {
  mb::ReferenceBook b;
  b.add(ord(30, Side::Sell, 1007, 12, /*party=*/9));
  const mb::RestingOrder* o = b.find(OrderId{30});
  ASSERT_NE(o, nullptr);
  EXPECT_EQ(o->participant, ParticipantId{9});
  EXPECT_EQ(o->remaining, Qty{12});
  EXPECT_EQ(b.find(OrderId{999}), nullptr);
}

// ----- dump_state: canonical, best-first, FIFO order (INV-15) -----------------

TEST(ReferenceBook, DumpStateIsCanonical) {
  mb::ReferenceBook b;
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

  // Equal books compare equal; a divergence in any field breaks equality.
  mb::ReferenceBook b2;
  b2.add(ord(1, Side::Buy, 1000, 5));
  b2.add(ord(2, Side::Buy, 1001, 6));
  b2.add(ord(3, Side::Sell, 1005, 7));
  b2.add(ord(4, Side::Sell, 1005, 8));
  b2.add(ord(5, Side::Sell, 1004, 9));
  EXPECT_EQ(b.dump_state(), b2.dump_state());
  b2.reduce(OrderId{3}, Qty{1});
  EXPECT_NE(b.dump_state(), b2.dump_state());
}

// ----- §15 worked-example book: asks C(10),D(15)@10.03, E(40)@10.05 -----------

TEST(ReferenceBook, WorkedExampleAskSweepState) {
  mb::ReferenceBook b;
  // Ask side as in the primer/worked example (prices in cent ticks).
  b.add(ord(/*C=*/101, Side::Sell, 1003, 10));
  b.add(ord(/*D=*/102, Side::Sell, 1003, 15));
  b.add(ord(/*E=*/103, Side::Sell, 1005, 40));
  b.add(ord(/*bid*/ 201, Side::Buy, 1001, 5));

  // Best ask is the 10.03 level; C is first by FIFO; that level holds 25.
  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1003}});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{101});
  EXPECT_EQ(b.depth(Side::Sell, Price{1003}), Qty{25});

  // Simulate a MARKET BUY 60 sweeping the book (matcher lands in R1-12): C and D
  // fully fill and are removed; E fills 35 of 40, keeping its queue position
  // with 5 remaining (R-5.6 not triggered — E never left the book).
  b.remove(OrderId{101});
  b.remove(OrderId{102});
  b.reduce(OrderId{103}, Qty{5});

  EXPECT_EQ(b.best(Side::Sell), std::optional<Price>{Price{1005}});  // 10.03 empty now
  EXPECT_EQ(b.depth(Side::Sell, Price{1005}), Qty{5});
  EXPECT_EQ(b.front(Side::Sell)->id, OrderId{103});
  // Post-state R-5.8: best bid 10.01 < best ask 10.05.
  EXPECT_LT(*b.best(Side::Buy), *b.best(Side::Sell));
}

// ----- for_each_order visits best-to-worst, FIFO within (R-12.3 order) --------

TEST(ReferenceBook, ForEachOrderIsSessionEndOrder) {
  mb::ReferenceBook b;
  b.add(ord(1, Side::Buy, 1000, 5));
  b.add(ord(2, Side::Buy, 1002, 5));
  b.add(ord(3, Side::Buy, 1002, 5));  // same level as #2, later

  std::vector<std::uint64_t> visited;
  b.for_each_order(Side::Buy, [&](const mb::RestingOrder& o) { visited.push_back(o.id.value()); });
  // Best price (1002) first, FIFO within it (#2 then #3), then 1000 (#1).
  EXPECT_EQ(visited, (std::vector<std::uint64_t>{2, 3, 1}));
}
