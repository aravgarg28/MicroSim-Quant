#pragma once

/// \file
/// `FastBook` — the engine's production order book (task R1-19), stage 1 of the
/// rollout in ORDER_BOOK_DESIGN.md §"MVP vs optimized rollout". It is the fast
/// counterpart to `ReferenceBook`: same `OrderBookLike` interface, deliberately
/// different data structures (design D + E, simple forms), so the differential
/// harness (R1-20, INV-15) exercises the interface twice over from independent
/// code and a shared bug would have to be made twice in different shapes.
///
/// Stage-1 structures (the "simple, correct, already fast" set):
///   * one `std::vector<Level>` per side, indexed by *tick offset* from the
///     instrument's `min_price` — level lookup is O(1) arithmetic, no search
///     (design D). Both sides use the same index for the same price; the best
///     bid is the highest occupied index, the best ask the lowest;
///   * each `Level` is a `std::deque<RestingOrder>` FIFO — append at the back,
///     the front is the highest time priority (design E, simple form);
///   * a `std::unordered_map<OrderId, Loc>` for O(1)-expected locate on
///     cancel/reduce/modify;
///   * a cached best index per side; when the best level empties, a linear scan
///     toward worse prices finds the next non-empty level (the occupancy bitmap
///     that bounds that scan is a later optimization, E18).
///
/// The banded price array rests on R-1.1: an instrument declares an immutable
/// `[min_price, max_price]` band and the gateway rejects out-of-band prices
/// (`PRICE_OUT_OF_BANDS`) before they ever reach the book, so every price here
/// maps to a valid index. Stage 2 (post-R3, each step benchmarked) swaps the
/// deques/hash-map for intrusive lists, open addressing, and occupancy bitmaps
/// without touching this interface — "optimizations are refactorings".

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "microsim/book/order_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/types.hpp"

namespace microsim::book {

class FastBook {
 public:
  /// Build a book over the instrument's declared price band (R-1.1). The level
  /// arrays are sized to the band; every in-band price maps to an array slot.
  explicit FastBook(const core::InstrumentConfig& instrument);

  /// Build a book over an explicit `[min_price, max_price]` band (max > min).
  FastBook(core::Price min_price, core::Price max_price);

  // ----- mutation ------------------------------------------------------------

  /// Add a resting order at the back of its price level's FIFO queue (R-5.6).
  /// The order must not already be present and its price must be in band.
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

  /// Total remaining quantity resting at (`side`, `price`), summed on demand.
  /// Zero if the level is empty or the price is out of band.
  [[nodiscard]] Qty depth(Side side, Price price) const;

  /// Whether `side` has no resting orders.
  [[nodiscard]] bool empty(Side side) const;

  /// A canonical snapshot for differential comparison and tests (INV-15): both
  /// sides, best price first, each level's orders in FIFO order.
  [[nodiscard]] BookState dump_state() const;

 private:
  /// One price level: its resting orders in FIFO order, front() highest priority.
  using Level = std::deque<RestingOrder>;

  /// Where a resting order lives, so cancel/reduce/remove skip straight to the
  /// level (then a short in-level scan finds the order).
  struct Loc {
    Side side;
    std::size_t index;  ///< tick offset from base_tick_
  };

  /// The array index for a price; asserts the price is in band (the gateway
  /// guarantees it for anything the engine adds).
  [[nodiscard]] std::size_t index_of(Price price) const;

  /// Mutable/const locate of a resting order within its level, or nullptr.
  [[nodiscard]] RestingOrder* locate(OrderId id);
  [[nodiscard]] const RestingOrder* locate(OrderId id) const;

  /// Recompute the cached best index after the current best level emptied, by
  /// scanning toward worse prices (bids downward, asks upward). -1 if the side
  /// is now empty.
  [[nodiscard]] std::int64_t rescan_best(Side side, std::int64_t from) const;

  std::int64_t base_tick_;  ///< tick of array index 0 (= min_price)
  std::int64_t span_;       ///< number of levels per side (band width in ticks)
  std::vector<Level> bids_;
  std::vector<Level> asks_;
  std::unordered_map<OrderId, Loc> index_;
  std::int64_t best_bid_idx_{-1};      ///< highest occupied bid index, -1 if none
  std::int64_t best_ask_idx_{-1};      ///< lowest occupied ask index, -1 if none
  std::uint64_t next_queue_token_{1};  ///< stamped onto each (re)queued order (INV-3)
};

static_assert(OrderBookLike<FastBook>, "FastBook must satisfy the shared order-book interface");

}  // namespace microsim::book
