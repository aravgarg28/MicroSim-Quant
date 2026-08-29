#pragma once

/// \file
/// The invariant checker harness (task R1-17): executable statements of the
/// ENGINE_INVARIANTS.md contract, run after each message in test/property builds
/// so a violation is caught at the message that caused it. Each checker is a pure
/// function over plain data (a book snapshot, the order records, one message's
/// events, per-participant exposure) so its self-test can hand-build a broken
/// input and prove the checker actually trips — "a checker that can't fail is
/// decoration" (REFERENCE_MODEL.md).
///
/// Coverage here: INV-1, 2, 3, 4, 5, 6, 8, 9, 12, 13, 14. INV-7 (terminal states
/// absorbing) needs cross-message memory and lives in `InvariantMonitor`. INV-10
/// (determinism) and INV-15 (reference agreement) are differential/replay checks
/// (R1-20); INV-11 accounting and INV-16 causality arrive with their subjects
/// (E11, R2); INV-17 session-end cleanliness is checked in the session-end tests
/// and reduces to INV-11 + an empty book here.

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "microsim/book/order_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/order_registry.hpp"

namespace microsim::engine {

/// One invariant failure: which invariant, and a human-readable detail. Checkers
/// append these rather than asserting, so the failing message can be reported and
/// so the self-tests can assert a specific invariant tripped.
struct InvariantViolation {
  std::string_view id;  ///< e.g. "INV-1"
  std::string detail;
};

// ----- INV-1, 2, 3, 12 (resting), 13: book structure from a snapshot ----------

/// Check the structural book invariants against a canonical snapshot: no crossed
/// book (INV-1/13), correct side ordering and non-empty levels (INV-2), FIFO by
/// queue_token within a level (INV-3), and every resting price in band (INV-12).
inline void check_book(const book::BookState& state, const core::InstrumentConfig& instr,
                       std::vector<InvariantViolation>& out) {
  const auto side_price_ordered = [&](const std::vector<book::BookLevelState>& levels,
                                      core::Side side, bool ascending) {
    for (std::size_t i = 0; i < levels.size(); ++i) {
      const book::BookLevelState& level = levels[i];
      if (level.orders.empty()) {
        out.push_back({"INV-2", "empty price level object persists"});
      }
      if (level.price < instr.min_price || level.price > instr.max_price) {
        out.push_back({"INV-12", "resting level price out of instrument bands"});
      }
      if (i > 0) {
        const bool ok =
            ascending ? (levels[i - 1].price < level.price) : (levels[i - 1].price > level.price);
        if (!ok) {
          out.push_back({"INV-2", "levels not strictly monotonic best-to-worst"});
        }
      }
      std::uint64_t prev_token = 0;
      for (std::size_t j = 0; j < level.orders.size(); ++j) {
        const book::RestingOrder& o = level.orders[j];
        if (o.side != side) {
          out.push_back({"INV-2", "resting order on the wrong side"});
        }
        if (j > 0 && !(o.queue_token > prev_token)) {
          out.push_back({"INV-3", "FIFO queue_token not strictly increasing within level"});
        }
        prev_token = o.queue_token;
      }
    }
  };
  side_price_ordered(state.bids, core::Side::Buy, /*ascending=*/false);
  side_price_ordered(state.asks, core::Side::Sell, /*ascending=*/true);

  // INV-1 / INV-13: a non-empty bid and ask must not cross or lock. With one
  // price per level, "no resting order is marketable" (INV-13) is exactly this.
  if (!state.bids.empty() && !state.asks.empty()) {
    const core::Price best_bid = state.bids.front().price;
    const core::Price best_ask = state.asks.front().price;
    if (!(best_bid < best_ask)) {
      out.push_back({"INV-1", "book is crossed or locked (best_bid >= best_ask)"});
      out.push_back({"INV-13", "a resting order remains marketable against the other side"});
    }
  }
}

// ----- INV-4, 6, 8, 9: order records vs the book ------------------------------

/// Check the order-record invariants. `orders` are every record, dense in
/// order_id from 1 (so orders[i] has order_id i+1); `state` is the current book.
inline void check_orders(const std::vector<OrderRecord>& orders, const book::BookState& state,
                         std::vector<InvariantViolation>& out) {
  std::set<std::pair<std::uint32_t, std::uint64_t>> client_ids;
  std::int64_t live_resting_lots = 0;
  std::int64_t live_count = 0;

  for (std::size_t i = 0; i < orders.size(); ++i) {
    const OrderRecord& o = orders[i];

    // INV-9: order_ids are strictly increasing and gap-free (dense from 1).
    if (o.id != core::OrderId{static_cast<std::uint64_t>(i) + 1}) {
      out.push_back({"INV-9", "order_id sequence is not dense / strictly increasing"});
    }
    // INV-4: no overfill; remaining is exactly total - filled.
    if (o.filled_qty < core::Qty{0} || o.filled_qty > o.total_qty) {
      out.push_back({"INV-4", "cumulative filled outside [0, total_qty]"});
    }
    // INV-8: a (participant, client_order_id) pair never created two orders.
    const auto key = std::pair{o.participant.value(), o.client_order_id.value()};
    if (!client_ids.insert(key).second) {
      out.push_back({"INV-8", "duplicate (participant, client_order_id) created an order"});
    }
    if (o.state == OrderState::Live) {
      ++live_count;
      live_resting_lots += o.remaining().lots();
    }
  }

  // INV-6: summed over all orders, book depth equals total resting quantity, and
  // every Live order is resting (their counts match).
  std::int64_t book_lots = 0;
  std::int64_t book_count = 0;
  for (const auto* levels : {&state.bids, &state.asks}) {
    for (const book::BookLevelState& level : *levels) {
      for (const book::RestingOrder& o : level.orders) {
        book_lots += o.remaining.lots();
        ++book_count;
      }
    }
  }
  if (book_lots != live_resting_lots) {
    out.push_back({"INV-6", "book depth != total resting quantity of live orders"});
  }
  if (book_count != live_count) {
    out.push_back({"INV-6", "count of resting orders != count of live orders"});
  }
}

// ----- INV-5, 12 (trades): one message's outbound events ----------------------

/// Check the per-trade invariants over the events a single message produced:
/// every trade has one maker and one taker fill of equal quantity (INV-5) and a
/// price within the instrument bands (INV-12).
inline void check_events(const std::vector<core::Outbound>& events,
                         const core::InstrumentConfig& instr,
                         std::vector<InvariantViolation>& out) {
  struct TradeTally {
    core::Qty trade_qty{};
    bool saw_trade = false;
    int maker_fills = 0;
    int taker_fills = 0;
    bool qty_mismatch = false;
  };

  std::unordered_map<std::uint64_t, TradeTally> by_trade;

  for (const core::Outbound& ev : events) {
    if (const auto* t = std::get_if<core::Trade>(&ev)) {
      TradeTally& tally = by_trade[t->trade_id.value()];
      tally.saw_trade = true;
      tally.trade_qty = t->qty;
      if (t->price < instr.min_price || t->price > instr.max_price) {
        out.push_back({"INV-12", "trade price out of instrument bands"});
      }
    } else if (const auto* f = std::get_if<core::Fill>(&ev)) {
      TradeTally& tally = by_trade[f->trade_id.value()];
      (f->liquidity == core::LiquidityFlag::Maker ? tally.maker_fills : tally.taker_fills) += 1;
      if (tally.saw_trade && f->qty != tally.trade_qty) {
        tally.qty_mismatch = true;
      }
    }
  }

  for (const auto& [trade_id, tally] : by_trade) {
    if (!tally.saw_trade || tally.maker_fills != 1 || tally.taker_fills != 1) {
      out.push_back({"INV-5", "trade lacks exactly one maker fill and one taker fill"});
    }
    if (tally.qty_mismatch) {
      out.push_back({"INV-5", "maker/taker fill quantity != trade quantity"});
    }
  }
}

// ----- INV-14: risk-limit compliance ------------------------------------------

/// One participant's exposure at a checkpoint, for the risk invariant.
struct ParticipantExposure {
  core::ParticipantId id;
  std::int64_t open_orders;
  std::int64_t worst_long;   ///< position + same-side buy open quantity
  std::int64_t worst_short;  ///< position − same-side sell open quantity
  core::ParticipantRisk limits;
};

/// INV-14: every participant is within its open-order and worst-case position
/// limits (no accepted order could have breached R-9.3 at acceptance).
inline void check_risk(const std::vector<ParticipantExposure>& exposures,
                       std::vector<InvariantViolation>& out) {
  for (const ParticipantExposure& e : exposures) {
    if (e.open_orders > e.limits.max_open_orders) {
      out.push_back({"INV-14", "open_orders exceeds max_open_orders"});
    }
    const std::int64_t max_pos = e.limits.max_position_lots.lots();
    if (e.worst_long > max_pos || e.worst_short < -max_pos) {
      out.push_back({"INV-14", "worst-case position exceeds max_position_lots"});
    }
  }
}

// ----- engine adapter: gather live state and run every state-based check ------

/// Collect the current state from a live engine and run INV-1..4, 6, 8, 9, 12
/// (resting), 13, 14. Event-based INV-5/12 (trades) run via check_events on the
/// message's output; INV-7 runs via InvariantMonitor.
template <class Book>
std::vector<InvariantViolation> check_engine_state(const MatchingEngine<Book>& engine) {
  std::vector<InvariantViolation> out;
  const book::BookState state = engine.book().dump_state();
  const core::InstrumentConfig& instr = engine.instrument();

  // Order ids are dense from 1 (OrderRegistry::create), so lookup 1..size gathers
  // every record in id order.
  const OrderRegistry& registry = engine.registry();
  std::vector<OrderRecord> orders;
  orders.reserve(registry.size());
  std::set<std::uint32_t> participants;
  for (std::size_t i = 1; i <= registry.size(); ++i) {
    const OrderRecord* rec = registry.lookup(core::OrderId{static_cast<std::uint64_t>(i)});
    if (rec != nullptr) {
      orders.push_back(*rec);
      participants.insert(rec->participant.value());
    }
  }

  check_book(state, instr, out);
  check_orders(orders, state, out);

  std::vector<ParticipantExposure> exposures;
  exposures.reserve(participants.size());
  for (std::uint32_t raw : participants) {
    const core::ParticipantId p{raw};
    const core::ParticipantConfig* cfg = engine.venue().find_participant(p);
    if (cfg == nullptr) {
      continue;  // should not happen: every order's participant is registered
    }
    const std::int64_t position = engine.risk().position(p);
    exposures.push_back(ParticipantExposure{
        .id = p,
        .open_orders = registry.open_order_count(p),
        .worst_long = position + registry.same_side_open_qty(p, core::Side::Buy),
        .worst_short = position - registry.same_side_open_qty(p, core::Side::Sell),
        .limits = cfg->risk});
  }
  check_risk(exposures, out);
  return out;
}

// ----- INV-7: terminal states are absorbing (needs cross-message memory) ------

/// A shadow of which orders are already terminal, so INV-7 — no event may act on
/// an order after its terminal event — is checkable across messages. Feed it each
/// message's events with `after_message`; it also runs every state check.
class InvariantMonitor {
 public:
  /// Check the invariants after one message: INV-7 against the events (no event
  /// references an order that was already terminal before this message), plus all
  /// state and per-trade checks. Returns every violation found.
  template <class Book>
  std::vector<InvariantViolation> after_message(const MatchingEngine<Book>& engine,
                                                const std::vector<core::Outbound>& events) {
    std::vector<InvariantViolation> out = check_engine_state(engine);
    check_events(events, engine.instrument(), out);

    // INV-7: an event touching an order that was terminal at the end of a prior
    // message means a terminal order was acted on again.
    for (const core::Outbound& ev : events) {
      const core::OrderId ref = referenced_order(ev);
      if (ref != core::OrderId{} && already_terminal_.contains(ref)) {
        out.push_back({"INV-7", "event references an order that was already terminal"});
      }
    }

    // Refresh the shadow from the authoritative registry for the next message.
    const OrderRegistry& registry = engine.registry();
    for (std::size_t i = 1; i <= registry.size(); ++i) {
      const core::OrderId id{static_cast<std::uint64_t>(i)};
      const OrderRecord* rec = registry.lookup(id);
      if (rec != nullptr && rec->terminal()) {
        already_terminal_.insert(id);
      }
    }
    return out;
  }

 private:
  /// The order an event acts on, or a default OrderId{} for events without one
  /// (a rejected new order carries no assigned order_id).
  [[nodiscard]] static core::OrderId referenced_order(const core::Outbound& ev) {
    if (const auto* f = std::get_if<core::Fill>(&ev)) {
      return f->order_id;
    }
    if (const auto* c = std::get_if<core::OrderCanceled>(&ev)) {
      return c->order_id;
    }
    if (const auto* m = std::get_if<core::OrderModified>(&ev)) {
      return m->order_id;
    }
    return core::OrderId{};
  }

  std::set<core::OrderId> already_terminal_;
};

}  // namespace microsim::engine
