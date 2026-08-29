#include "microsim/book/reference_book.hpp"

#include <cassert>
#include <iterator>
#include <utility>

namespace microsim::book {

void ReferenceBook::add(const RestingOrder& order) {
  assert(index_.find(order.id) == index_.end() && "order already resting");
  assert(order.remaining > Qty{0} && "resting order must have positive quantity");

  // The book stamps every (re)queue with a strictly-increasing token (INV-3), so
  // FIFO within a level is by queue_token even after a modify re-queues an order.
  RestingOrder resting = order;
  resting.queue_token = next_queue_token_++;

  // R-5.6: the order joins the back of its price level's FIFO queue. A new level
  // is created on first use; the map comparator keeps levels best-price-first.
  std::list<RestingOrder>::iterator it;
  if (resting.side == Side::Buy) {
    std::list<RestingOrder>& level = bids_[resting.price];
    level.push_back(resting);
    it = std::prev(level.end());
  } else {
    std::list<RestingOrder>& level = asks_[resting.price];
    level.push_back(resting);
    it = std::prev(level.end());
  }
  index_.emplace(resting.id, Locator{resting.side, resting.price, it});
}

void ReferenceBook::reduce(OrderId id, Qty new_remaining) {
  auto i = index_.find(id);
  assert(i != index_.end() && "reduce of an order not in the book");
  RestingOrder& o = *i->second.it;
  // R-7.2 quantity-decrease-same-price: strictly smaller, still positive; the
  // order keeps its list position (time priority) untouched.
  assert(new_remaining > Qty{0} && new_remaining < o.remaining && "reduce must shrink to > 0");
  o.remaining = new_remaining;
}

void ReferenceBook::remove(OrderId id) {
  auto i = index_.find(id);
  assert(i != index_.end() && "remove of an order not in the book");
  const Locator& loc = i->second;
  if (loc.side == Side::Buy) {
    auto level = bids_.find(loc.price);
    level->second.erase(loc.it);
    if (level->second.empty()) {
      bids_.erase(level);  // drop empty levels so begin() is always the best
    }
  } else {
    auto level = asks_.find(loc.price);
    level->second.erase(loc.it);
    if (level->second.empty()) {
      asks_.erase(level);
    }
  }
  index_.erase(i);
}

void ReferenceBook::clear() {
  bids_.clear();
  asks_.clear();
  index_.clear();
}

const RestingOrder* ReferenceBook::find(OrderId id) const {
  auto i = index_.find(id);
  if (i == index_.end()) {
    return nullptr;
  }
  return &*i->second.it;
}

const RestingOrder* ReferenceBook::front(Side side) const {
  // begin() is the best level (map comparator); a resting level is never empty
  // (remove() drops emptied levels), so front() of it is the FIFO head.
  if (side == Side::Buy) {
    return bids_.empty() ? nullptr : &bids_.begin()->second.front();
  }
  return asks_.empty() ? nullptr : &asks_.begin()->second.front();
}

std::optional<Price> ReferenceBook::best(Side side) const {
  if (side == Side::Buy) {
    return bids_.empty() ? std::nullopt : std::optional<Price>{bids_.begin()->first};
  }
  return asks_.empty() ? std::nullopt : std::optional<Price>{asks_.begin()->first};
}

Qty ReferenceBook::depth(Side side, Price price) const {
  Qty total{0};
  const auto sum_level = [&](const auto& levels) {
    auto level = levels.find(price);
    if (level == levels.end()) {
      return;
    }
    for (const RestingOrder& o : level->second) {
      total += o.remaining;  // recompute on demand; never a cached aggregate
    }
  };
  if (side == Side::Buy) {
    sum_level(bids_);
  } else {
    sum_level(asks_);
  }
  return total;
}

bool ReferenceBook::empty(Side side) const {
  return side == Side::Buy ? bids_.empty() : asks_.empty();
}

BookState ReferenceBook::dump_state() const {
  BookState state;
  for (const auto& [price, orders] : bids_) {
    BookLevelState level;
    level.price = price;
    level.orders.assign(orders.begin(), orders.end());
    state.bids.push_back(std::move(level));
  }
  for (const auto& [price, orders] : asks_) {
    BookLevelState level;
    level.price = price;
    level.orders.assign(orders.begin(), orders.end());
    state.asks.push_back(std::move(level));
  }
  return state;
}

}  // namespace microsim::book
