#pragma once

/// \file
/// `ReferenceBook` — the slow, obviously-correct order book that serves as the
/// differential-testing oracle (task R1-09, REFERENCE_MODEL.md). Every method is
/// written to read like the matching EXCHANGE_RULES.md paragraph; a reviewer
/// holding the rules doc verifies each by inspection. There is deliberately *no*
/// performance consideration here: `std::map` + `std::list`, linear scans, and
/// on-demand aggregate summation. The moment this is optimized it stops being a
/// reference (REFERENCE_MODEL.md §"No performance consideration whatsoever").
///
/// Independence from `FastBook` is on purpose: different data structures mean a
/// shared bug would require the same mistake twice in different shapes.

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>

#include "microsim/book/order_book.hpp"
#include "microsim/core/types.hpp"

namespace microsim::book {

class ReferenceBook {
 public:
  // ----- mutation ------------------------------------------------------------

  /// Add a resting order at the back of its price level's FIFO queue (R-5.6).
  /// The order must not already be present.
  void add(const RestingOrder& order);

  /// Reduce a resting order's remaining quantity, keeping its queue position
  /// (R-7.2 "quantity decrease, same price"). `new_remaining` must be > 0 and
  /// < the current remaining. To remove an order entirely, use remove().
  void reduce(OrderId id, Qty new_remaining);

  /// Remove a resting order from the book (a full fill, a cancel, session end).
  void remove(OrderId id);

  /// Remove every resting order (session end, R-12.3 clears after cancels).
  void clear();

  // ----- inspection ----------------------------------------------------------

  /// The resting order with this id, or nullptr if it is not in the book.
  [[nodiscard]] const RestingOrder* find(OrderId id) const;

  /// The highest-priority resting order on `side` (best price, FIFO front), or
  /// nullptr if that side is empty. This is what the match loop consumes.
  [[nodiscard]] const RestingOrder* front(Side side) const;

  /// The best (most aggressive) price resting on `side`, or nullopt if empty:
  /// highest bid, lowest ask (R-5.2).
  [[nodiscard]] std::optional<Price> best(Side side) const;

  /// Total remaining quantity resting at (`side`, `price`) — recomputed by
  /// summation, never cached (REFERENCE_MODEL.md: no cached aggregates to get
  /// wrong). Zero if the level is empty.
  [[nodiscard]] Qty depth(Side side, Price price) const;

  /// Whether `side` has no resting orders.
  [[nodiscard]] bool empty(Side side) const;

  /// A canonical snapshot for differential comparison and tests (INV-15): both
  /// sides, best price first, each level's orders in FIFO order.
  [[nodiscard]] BookState dump_state() const;

  /// Visit every level of `side` best price first, and within a level every
  /// order in FIFO priority order. Used by session end (R-12.3), which cancels
  /// resting orders in exactly this order.
  template <class Fn>
  void for_each_order(Side side, Fn&& fn) const {
    if (side == Side::Buy) {
      for (const auto& [price, orders] : bids_) {
        for (const RestingOrder& o : orders) {
          fn(o);
        }
      }
    } else {
      for (const auto& [price, orders] : asks_) {
        for (const RestingOrder& o : orders) {
          fn(o);
        }
      }
    }
  }

 private:
  // Best-first order per side is baked into the map comparator: bids highest
  // price first, asks lowest first (R-5.2). Each level is a FIFO list; the front
  // of the list is the oldest (highest time priority) order.
  using BidLevels = std::map<Price, std::list<RestingOrder>, std::greater<>>;
  using AskLevels = std::map<Price, std::list<RestingOrder>, std::less<>>;

  /// Where a resting order lives, so cancel/reduce/remove are direct lookups.
  struct Locator {
    Side side;
    Price price;
    std::list<RestingOrder>::iterator it;
  };

  BidLevels bids_;
  AskLevels asks_;
  std::map<OrderId, Locator> index_;
  std::uint64_t next_queue_token_{1};  ///< stamped onto each (re)queued order (INV-3)
};

static_assert(OrderBookLike<ReferenceBook>,
              "ReferenceBook must satisfy the shared order-book interface");

}  // namespace microsim::book
