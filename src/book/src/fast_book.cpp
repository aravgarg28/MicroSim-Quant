#include "microsim/book/fast_book.hpp"

#include <cassert>
#include <utility>

namespace microsim::book {

FastBook::FastBook(core::Price min_price, core::Price max_price)
    : base_tick_(min_price.ticks()),
      span_(max_price.ticks() - min_price.ticks() + 1),
      bids_(static_cast<std::size_t>(max_price.ticks() - min_price.ticks() + 1)),
      asks_(static_cast<std::size_t>(max_price.ticks() - min_price.ticks() + 1)) {
  assert(max_price > min_price && "FastBook price band must be non-empty (R-1.1)");
}

FastBook::FastBook(const core::InstrumentConfig& instrument)
    : FastBook(instrument.min_price, instrument.max_price) {}

std::size_t FastBook::index_of(Price price) const {
  const std::int64_t off = price.ticks() - base_tick_;
  assert(off >= 0 && off < span_ && "price out of the book's configured band (R-1.1)");
  return static_cast<std::size_t>(off);
}

void FastBook::add(const RestingOrder& order) {
  assert(index_.find(order.id) == index_.end() && "order already resting");
  assert(order.remaining > Qty{0} && "resting order must have positive quantity");

  const std::size_t idx = index_of(order.price);

  // The book stamps every (re)queue with a strictly-increasing token (INV-3), so
  // FIFO within a level is by queue_token even after a modify re-queues an order.
  RestingOrder resting = order;
  resting.queue_token = next_queue_token_++;

  const std::int64_t signed_idx = static_cast<std::int64_t>(idx);
  if (resting.side == Side::Buy) {
    bids_[idx].push_back(resting);  // R-5.6: join the back of the FIFO
    if (best_bid_idx_ < 0 || signed_idx > best_bid_idx_) {
      best_bid_idx_ = signed_idx;  // highest occupied bid index is the best bid
    }
  } else {
    asks_[idx].push_back(resting);
    if (best_ask_idx_ < 0 || signed_idx < best_ask_idx_) {
      best_ask_idx_ = signed_idx;  // lowest occupied ask index is the best ask
    }
  }
  index_.emplace(order.id, Loc{resting.side, idx});
}

void FastBook::reduce(OrderId id, Qty new_remaining) {
  RestingOrder* o = locate(id);
  assert(o != nullptr && "reduce of an order not in the book");
  // R-7.2 quantity-decrease-same-price: strictly smaller, still positive; the
  // order keeps its queue position (deque slot) untouched, so the best index and
  // FIFO order are unaffected.
  assert(new_remaining > Qty{0} && new_remaining < o->remaining && "reduce must shrink to > 0");
  o->remaining = new_remaining;
}

void FastBook::remove(OrderId id) {
  auto i = index_.find(id);
  assert(i != index_.end() && "remove of an order not in the book");
  const Loc loc = i->second;
  Level& level = (loc.side == Side::Buy ? bids_ : asks_)[loc.index];

  for (auto it = level.begin(); it != level.end(); ++it) {
    if (it->id == id) {
      level.erase(it);
      break;
    }
  }
  index_.erase(i);

  // If the emptied level was the cached best, scan for the next non-empty level.
  if (level.empty()) {
    const std::int64_t idx = static_cast<std::int64_t>(loc.index);
    if (loc.side == Side::Buy) {
      if (idx == best_bid_idx_) {
        best_bid_idx_ = rescan_best(Side::Buy, idx);
      }
    } else if (idx == best_ask_idx_) {
      best_ask_idx_ = rescan_best(Side::Sell, idx);
    }
  }
}

void FastBook::clear() {
  for (Level& level : bids_) {
    level.clear();
  }
  for (Level& level : asks_) {
    level.clear();
  }
  index_.clear();
  best_bid_idx_ = -1;
  best_ask_idx_ = -1;
}

const RestingOrder* FastBook::find(OrderId id) const {
  return locate(id);
}

const RestingOrder* FastBook::front(Side side) const {
  const std::int64_t idx = (side == Side::Buy) ? best_bid_idx_ : best_ask_idx_;
  if (idx < 0) {
    return nullptr;
  }
  // The cached best always points to an occupied level, so front() is its head.
  const Level& level = (side == Side::Buy ? bids_ : asks_)[static_cast<std::size_t>(idx)];
  return &level.front();
}

std::optional<Price> FastBook::best(Side side) const {
  const std::int64_t idx = (side == Side::Buy) ? best_bid_idx_ : best_ask_idx_;
  if (idx < 0) {
    return std::nullopt;
  }
  return std::optional<Price>{Price{base_tick_ + idx}};
}

Qty FastBook::depth(Side side, Price price) const {
  const std::int64_t off = price.ticks() - base_tick_;
  if (off < 0 || off >= span_) {
    return Qty{0};
  }
  const Level& level = (side == Side::Buy ? bids_ : asks_)[static_cast<std::size_t>(off)];
  Qty total{0};
  for (const RestingOrder& o : level) {
    total += o.remaining;  // recompute on demand; never a cached aggregate (stage 1)
  }
  return total;
}

bool FastBook::empty(Side side) const {
  return (side == Side::Buy ? best_bid_idx_ : best_ask_idx_) < 0;
}

BookState FastBook::dump_state() const {
  BookState state;
  // Bids best-first: highest occupied index down to the lowest.
  for (std::int64_t idx = span_ - 1; idx >= 0; --idx) {
    const Level& level = bids_[static_cast<std::size_t>(idx)];
    if (level.empty()) {
      continue;
    }
    BookLevelState ls;
    ls.price = Price{base_tick_ + idx};
    ls.orders.assign(level.begin(), level.end());
    state.bids.push_back(std::move(ls));
  }
  // Asks best-first: lowest occupied index up to the highest.
  for (std::int64_t idx = 0; idx < span_; ++idx) {
    const Level& level = asks_[static_cast<std::size_t>(idx)];
    if (level.empty()) {
      continue;
    }
    BookLevelState ls;
    ls.price = Price{base_tick_ + idx};
    ls.orders.assign(level.begin(), level.end());
    state.asks.push_back(std::move(ls));
  }
  return state;
}

RestingOrder* FastBook::locate(OrderId id) {
  auto i = index_.find(id);
  if (i == index_.end()) {
    return nullptr;
  }
  Level& level = (i->second.side == Side::Buy ? bids_ : asks_)[i->second.index];
  for (RestingOrder& o : level) {
    if (o.id == id) {
      return &o;
    }
  }
  return nullptr;
}

const RestingOrder* FastBook::locate(OrderId id) const {
  auto i = index_.find(id);
  if (i == index_.end()) {
    return nullptr;
  }
  const Level& level = (i->second.side == Side::Buy ? bids_ : asks_)[i->second.index];
  for (const RestingOrder& o : level) {
    if (o.id == id) {
      return &o;
    }
  }
  return nullptr;
}

std::int64_t FastBook::rescan_best(Side side, std::int64_t from) const {
  if (side == Side::Buy) {
    for (std::int64_t idx = from - 1; idx >= 0; --idx) {  // toward worse (lower) bids
      if (!bids_[static_cast<std::size_t>(idx)].empty()) {
        return idx;
      }
    }
  } else {
    for (std::int64_t idx = from + 1; idx < span_; ++idx) {  // toward worse (higher) asks
      if (!asks_[static_cast<std::size_t>(idx)].empty()) {
        return idx;
      }
    }
  }
  return -1;
}

}  // namespace microsim::book
