#pragma once

/// \file
/// The continuous price-time matching engine for new orders (task R1-12). This
/// is a direct translation of MATCHING_ENGINE_SPEC.md (`handle_new`,
/// `match_loop`, `execute_trade`) citing EXCHANGE_RULES.md rule IDs. It is
/// templated on `OrderBookLike` so the identical algorithm runs over the
/// ReferenceBook (the oracle) now and FastBook (R1-19) later — the differential
/// boundary is exactly this template plus the book concept.
///
/// Scope (R1-12): NewOrder — LIMIT and MARKET, fills, NO_LIQUIDITY cancels, and
/// per-fill fees (R-11.2). Cancel (R1-13), modify (R1-14), risk (R1-15), and
/// session end (R1-16) land in their own tasks. Sequencing headers (R-10.2) are
/// the sequencer's job (R1-11); this returns event payloads in emission order.

#include <algorithm>
#include <utility>
#include <vector>

#include "microsim/book/order_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/order_registry.hpp"
#include "microsim/engine/venue.hpp"

namespace microsim::engine {

using core::Cash;
using core::LiquidityFlag;
using core::Outbound;
using core::TradeId;

/// A matching engine over one instrument and one book type. Owns the book and
/// the order registry; validation reference data comes from the Venue.
template <class Book>
  requires book::OrderBookLike<Book>
class MatchingEngine {
 public:
  MatchingEngine(Venue venue, core::InstrumentConfig instrument)
      : venue_(std::move(venue)), instr_(std::move(instrument)) {}

  /// Process a NewOrder to completion (R-5.1: atomic, one message at a time),
  /// returning every event it produced, in emission order.
  std::vector<Outbound> process(const core::NewOrder& m) {
    out_.clear();

    // Gateway: R-3.3 items 1-8, first failure wins. (Risk item 9 is R1-15.)
    if (const auto reason = registry_.validate_new(m, venue_)) {
      out_.push_back(core::OrderRejected{.participant = m.participant,
                                         .client_order_id = m.client_order_id,
                                         .order_id = core::OrderId{},
                                         .reason = *reason});
      return std::move(out_);
    }

    // Accept: assign order_id (R-4.1) and emit OrderAccepted (R-4.3).
    const core::OrderId id = registry_.create(m);
    out_.push_back(core::OrderAccepted{
        .order_id = id, .participant = m.participant, .client_order_id = m.client_order_id});

    match_loop(id);

    const OrderRecord* o = registry_.lookup(id);
    if (o->remaining() > core::Qty{0}) {
      if (o->type == core::OrderType::Market) {
        // R-5.5: a MARKET order never rests; cancel the unfilled remainder.
        out_.push_back(core::OrderCanceled{.order_id = id,
                                           .participant = o->participant,
                                           .remaining_qty = o->remaining(),
                                           .reason = core::CancelReason::NoLiquidity});
        registry_.finalize(id, OrderState::Canceled);
      } else {
        // R-5.6: rest the LIMIT remainder at the back of its price level.
        book_.add(book::RestingOrder{.id = id,
                                     .participant = o->participant,
                                     .side = o->side,
                                     .price = o->price,
                                     .remaining = o->remaining()});
      }
    }
    // Fully filled: apply_fill already moved it to the FILLED terminal (R-4.3).
    return std::move(out_);
  }

  // ----- read-only views for the CLI, tests, and (later) MD --------------------

  [[nodiscard]] const Book& book() const noexcept { return book_; }

  [[nodiscard]] const OrderRegistry& registry() const noexcept { return registry_; }

  [[nodiscard]] const core::InstrumentConfig& instrument() const noexcept { return instr_; }

 private:
  /// R-5.3/5.4: while the taker has quantity and is marketable, trade the front
  /// of the best opposite level at the maker's price.
  void match_loop(core::OrderId taker_id) {
    OrderRecord* taker = registry_.lookup(taker_id);
    while (taker->remaining() > core::Qty{0} && marketable(*taker)) {
      const book::RestingOrder* front = book_.front(core::opposite(taker->side));
      // Capture the maker's identity/price before any mutation invalidates it.
      const core::OrderId maker_id = front->id;
      const core::ParticipantId maker_party = front->participant;
      const core::Price px = front->price;  // R-5.4: maker's resting price
      const core::Qty avail = front->remaining;
      const core::Qty q = std::min(taker->remaining(), avail);

      execute_trade(maker_id, maker_party, *taker, px, q);

      if (q == avail) {
        book_.remove(maker_id);  // maker fully filled (already FILLED in registry)
      } else {
        book_.reduce(maker_id, avail - q);  // maker keeps queue priority
      }
    }
  }

  /// R-5.3/5.5: a MARKET order matches any opposite liquidity; a LIMIT matches
  /// while the best opposite price is at or through its limit.
  [[nodiscard]] bool marketable(const OrderRecord& o) const {
    const auto opp_best = book_.best(core::opposite(o.side));
    if (!opp_best) {
      return false;
    }
    if (o.type == core::OrderType::Market) {
      return true;
    }
    return o.side == core::Side::Buy ? *opp_best <= o.price : *opp_best >= o.price;
  }

  /// R-5.7 + R-11.2: record the fill on both orders, compute flat per-lot fees,
  /// and emit the two private Fills (maker then taker) followed by the audit
  /// Trade — in exactly this order (determinism requirement).
  void execute_trade(core::OrderId maker_id, core::ParticipantId maker_party, OrderRecord& taker,
                     core::Price px, core::Qty q) {
    const TradeId trade_id = next_trade_id_;
    next_trade_id_ = next_trade_id_.next();

    registry_.apply_fill(maker_id, q);
    registry_.apply_fill(taker.id, q);

    // R-11.2: taker pays qty * taker_fee (positive cost); maker receives
    // qty * maker_rebate (negative cost — a credit).
    const Cash taker_fee{q.lots() * instr_.fees.taker_fee_per_lot.minor()};
    const Cash maker_fee{-(q.lots() * instr_.fees.maker_rebate_per_lot.minor())};

    out_.push_back(core::Fill{.order_id = maker_id,
                              .participant = maker_party,
                              .trade_id = trade_id,
                              .price = px,
                              .qty = q,
                              .fee = maker_fee,
                              .liquidity = LiquidityFlag::Maker});
    out_.push_back(core::Fill{.order_id = taker.id,
                              .participant = taker.participant,
                              .trade_id = trade_id,
                              .price = px,
                              .qty = q,
                              .fee = taker_fee,
                              .liquidity = LiquidityFlag::Taker});
    out_.push_back(core::Trade{.trade_id = trade_id,
                               .price = px,
                               .qty = q,
                               .maker_order_id = maker_id,
                               .taker_order_id = taker.id,
                               .maker_participant = maker_party,
                               .taker_participant = taker.participant,
                               .aggressor = taker.side});
  }

  Venue venue_;
  core::InstrumentConfig instr_;
  Book book_;
  OrderRegistry registry_;
  TradeId next_trade_id_{TradeId::first()};
  std::vector<Outbound> out_;
};

}  // namespace microsim::engine
