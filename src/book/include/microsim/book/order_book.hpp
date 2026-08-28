#pragma once

/// \file
/// The order-book interface shared by both implementations (task R1-09): the
/// `RestingOrder` value a book stores, the canonical `BookState` snapshot used
/// for differential comparison (INV-15), and the `OrderBookLike` concept the
/// matching engine templates over (ORDER_BOOK_DESIGN.md §"Shared interface").
///
/// Both `ReferenceBook` (the oracle, this release) and `FastBook` (R1-19) must
/// satisfy `OrderBookLike`, so the engine, unit/property/differential tests, and
/// benchmarks all run over identical scenarios against either book. The two
/// share *only* this header and `core` — no implementation code (independence,
/// REFERENCE_MODEL.md §"Design constraints").

#include <concepts>
#include <optional>
#include <vector>

#include "microsim/core/types.hpp"

namespace microsim::book {

using core::OrderId;
using core::ParticipantId;
using core::Price;
using core::Qty;
using core::Side;

/// A live order as the book sees it. The book tracks only what price-time
/// priority needs; the order's original/filled totals live in the registry
/// (R1-10). `remaining` is the unfilled quantity currently resting.
struct RestingOrder {
  OrderId id{};
  ParticipantId participant{};
  Side side{};
  Price price{};
  Qty remaining{};

  friend bool operator==(const RestingOrder&, const RestingOrder&) noexcept = default;
};

/// One price level of a `BookState` snapshot: the level's price and its orders
/// in FIFO (time-priority) order.
struct BookLevelState {
  Price price{};
  std::vector<RestingOrder> orders;  ///< front() is the highest-priority order

  friend bool operator==(const BookLevelState&, const BookLevelState&) noexcept = default;
};

/// A full, canonical snapshot of a book: both sides as level lists, each ordered
/// best price first, each level's orders in FIFO order. Two books are in the
/// same state iff their `BookState`s compare equal — the book half of the
/// differential check (the event-stream half is the stronger one).
struct BookState {
  std::vector<BookLevelState> bids;  ///< best (highest) first
  std::vector<BookLevelState> asks;  ///< best (lowest) first

  friend bool operator==(const BookState&, const BookState&) noexcept = default;
};

/// The book operations the matching engine and session-end path depend on. Any
/// conforming book keeps orders in price-time priority: `front(side)` is the
/// best-priced, then oldest, resting order on that side.
///
/// (`for_each_level` is a template member — it cannot appear in a `requires`
/// clause cleanly — and is validated by use, not by this concept.)
template <class B>
concept OrderBookLike = requires(B book, const B cbook, const RestingOrder& order, OrderId id,
                                 Side side, Price price, Qty qty) {
  // Mutation.
  { book.add(order) } -> std::same_as<void>;
  { book.reduce(id, qty) } -> std::same_as<void>;
  { book.remove(id) } -> std::same_as<void>;
  { book.clear() } -> std::same_as<void>;
  // Inspection.
  { cbook.find(id) } -> std::same_as<const RestingOrder*>;
  { cbook.front(side) } -> std::same_as<const RestingOrder*>;
  { cbook.best(side) } -> std::same_as<std::optional<Price>>;
  { cbook.depth(side, price) } -> std::same_as<Qty>;
  { cbook.empty(side) } -> std::same_as<bool>;
  { cbook.dump_state() } -> std::same_as<BookState>;
};

}  // namespace microsim::book
