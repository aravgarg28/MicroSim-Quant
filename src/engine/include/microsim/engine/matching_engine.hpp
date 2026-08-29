#pragma once

/// \file
/// The continuous price-time matching engine for new orders (task R1-12). This
/// is a direct translation of MATCHING_ENGINE_SPEC.md (`handle_new`,
/// `match_loop`, `execute_trade`) citing EXCHANGE_RULES.md rule IDs. It is
/// templated on `OrderBookLike` so the identical algorithm runs over the
/// ReferenceBook (the oracle) now and FastBook (R1-19) later — the differential
/// boundary is exactly this template plus the book concept.
///
/// Scope: NewOrder — LIMIT and MARKET, fills, NO_LIQUIDITY cancels, and per-fill
/// fees (R-11.2) (R1-12); cancel (R1-13); modify (R1-14); pre-trade risk with a
/// signed position tally (R1-15, R-9); and session end (R1-16, R-12). Sequencing
/// headers (R-10.2) are the sequencer's job (R1-11); this returns event payloads
/// in emission order.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "microsim/book/order_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/order_registry.hpp"
#include "microsim/engine/risk.hpp"
#include "microsim/engine/venue.hpp"

namespace microsim::engine {

using core::Cash;
using core::LiquidityFlag;
using core::Outbound;
using core::TradeId;

/// Trading state (R-12.1): one session per run, OPEN then CLOSED, never reopened.
enum class SessionState : std::uint8_t { Open = 0, Closed };

/// A matching engine over one instrument and one book type. Owns the book and
/// the order registry; validation reference data comes from the Venue.
template <class Book>
  requires book::OrderBookLike<Book>
class MatchingEngine {
 public:
  /// `initial_reference` is the R-12.4 mark-price fallback used when the book is
  /// empty and nothing has traded at session end; it defaults to the instrument's
  /// minimum price (any in-band value works — it only matters for that fallback).
  explicit MatchingEngine(Venue venue, core::InstrumentConfig instrument,
                          core::Price initial_reference = core::Price{})
      : venue_(std::move(venue)),
        instr_(std::move(instrument)),
        initial_reference_(initial_reference == core::Price{} ? instr_.min_price
                                                              : initial_reference) {}

  /// Process a NewOrder to completion (R-5.1: atomic, one message at a time),
  /// returning every event it produced, in emission order.
  std::vector<Outbound> process(const core::NewOrder& m) {
    out_.clear();

    if (state_ == SessionState::Closed) {  // R-12.2
      out_.push_back(core::OrderRejected{.participant = m.participant,
                                         .client_order_id = m.client_order_id,
                                         .order_id = core::OrderId{},
                                         .reason = core::RejectReason::MarketClosed});
      return std::move(out_);
    }

    // Gateway: R-3.3 items 1-8, first failure wins. (Risk item 9 is R1-15.)
    if (const auto reason = registry_.validate_new(m, venue_)) {
      out_.push_back(core::OrderRejected{.participant = m.participant,
                                         .client_order_id = m.client_order_id,
                                         .order_id = core::OrderId{},
                                         .reason = *reason});
      return std::move(out_);
    }

    // Risk (R-3.3 item 9 / R-9): open-order slots, participant size cap, and the
    // worst-case position limit. Participant is registered (validate_new item 2).
    const core::ParticipantRisk& limits = venue_.find_participant(m.participant)->risk;
    if (const auto reason = risk_.check_new(m, limits, registry_)) {
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

  /// Cancel a resting order (R-6). A participant may cancel only its own order;
  /// an unknown or already-terminal order is rejected with a distinct reason.
  std::vector<Outbound> process(const core::CancelOrder& m) {
    out_.clear();

    if (state_ == SessionState::Closed) {  // R-12.2
      out_.push_back(reject_cancel(m, core::RejectReason::MarketClosed));
      return std::move(out_);
    }

    OrderRecord* o = registry_.lookup(m.order_id);
    if (o == nullptr) {
      out_.push_back(reject_cancel(m, core::RejectReason::UnknownOrder));  // R-6.3
      return std::move(out_);
    }
    if (o->participant != m.participant) {
      out_.push_back(reject_cancel(m, core::RejectReason::NotOrderOwner));  // R-6.1
      return std::move(out_);
    }
    if (o->terminal()) {
      out_.push_back(reject_cancel(m, core::RejectReason::TooLateToCancel));  // R-6.3
      return std::move(out_);
    }

    // A live order is always resting in the book here (a fully-filled order is
    // already terminal). Remove it and report the quantity that did not trade.
    const core::Qty remaining = o->remaining();
    book_.remove(m.order_id);
    out_.push_back(core::OrderCanceled{.order_id = m.order_id,
                                       .participant = o->participant,
                                       .remaining_qty = remaining,
                                       .reason = core::CancelReason::ByRequest});  // R-6.2
    registry_.finalize(m.order_id, OrderState::Canceled);
    return std::move(out_);
  }

  /// Modify a resting order (R-7). Same ownership/terminal gate as cancel, then
  /// R-7.1 band validation; a new total at or below what already filled cancels
  /// the remainder (R-7.3); otherwise a same-price quantity decrease keeps queue
  /// priority (R-7.2) while a price change or quantity increase re-queues and may
  /// execute immediately (R-7.4). Exactly one OrderModified precedes any fills
  /// (R-7.5).
  std::vector<Outbound> process(const core::ModifyOrder& m) {
    out_.clear();

    if (state_ == SessionState::Closed) {  // R-12.2
      out_.push_back(reject_modify(m, core::RejectReason::MarketClosed));
      return std::move(out_);
    }

    OrderRecord* o = registry_.lookup(m.order_id);
    if (o == nullptr) {
      out_.push_back(reject_modify(m, core::RejectReason::UnknownOrder));  // R-6.3
      return std::move(out_);
    }
    if (o->participant != m.participant) {
      out_.push_back(reject_modify(m, core::RejectReason::NotOrderOwner));  // R-6.1
      return std::move(out_);
    }
    if (o->terminal()) {
      out_.push_back(reject_modify(m, core::RejectReason::TooLateToModify));  // R-6.3
      return std::move(out_);
    }
    if (const auto reason = registry_.validate_modify(m, instr_)) {
      out_.push_back(reject_modify(m, *reason));  // R-7.1 bands
      return std::move(out_);
    }
    // R-9.5: re-run risk on the delta; a failure leaves the order untouched (we
    // have not mutated anything yet). Owner is registered (checked at NewOrder).
    const core::ParticipantRisk& limits = venue_.find_participant(m.participant)->risk;
    if (const auto reason = risk_.check_modify(*o, m, limits, registry_)) {
      out_.push_back(reject_modify(m, *reason));
      return std::move(out_);
    }

    // R-7.3: a new total at or below the already-filled quantity cancels the
    // remainder. The single OrderModified still precedes the cancel (R-7.5).
    if (m.new_qty <= o->filled_qty) {
      const core::Qty remaining = o->remaining();
      book_.remove(m.order_id);
      out_.push_back(modified_event(m, *o));
      out_.push_back(core::OrderCanceled{.order_id = m.order_id,
                                         .participant = o->participant,
                                         .remaining_qty = remaining,
                                         .reason = core::CancelReason::ModifyToDone});
      registry_.finalize(m.order_id, OrderState::Canceled);
      return std::move(out_);
    }

    // R-7.2: keep the queue position only on a same-price quantity decrease; a
    // price change or a quantity increase loses time priority.
    const bool keep_priority = (m.new_price == o->price) && (m.new_qty < o->total_qty);

    registry_.modify(m.order_id, m.new_qty, m.new_price);  // sets total_qty + price
    out_.push_back(modified_event(m, *o));                 // R-7.5: one event, before fills

    if (keep_priority) {
      book_.reduce(m.order_id, o->remaining());  // shrink in place, priority kept
    } else {
      // Atomic cancel + fresh arrival at the new price (R-7.2): out of the book,
      // match if now marketable (R-7.4), then rest any remainder at the back.
      book_.remove(m.order_id);
      match_loop(m.order_id);
      if (o->remaining() > core::Qty{0}) {
        book_.add(book::RestingOrder{.id = m.order_id,
                                     .participant = o->participant,
                                     .side = o->side,
                                     .price = o->price,
                                     .remaining = o->remaining()});
      }
      // Fully filled by the immediate match: apply_fill already set FILLED.
    }
    return std::move(out_);
  }

  /// Close the session (R-12.3): compute the session-end mark price from the book
  /// as it stands, then cancel every resting order in canonical order — bids
  /// best-to-worst, then asks best-to-worst, FIFO within a level — emitting an
  /// OrderCanceled(SESSION_END) for each, and move to CLOSED. Idempotent: a second
  /// SessionEnd does nothing.
  std::vector<Outbound> process(const core::SessionEnd&) {
    out_.clear();
    if (state_ == SessionState::Closed) {
      return std::move(out_);
    }

    // R-12.4: the mark is read from the book before any orders are removed.
    mark_half_ticks_ = compute_mark_half_ticks();

    // dump_state() yields exactly the R-12.3 order (best-to-worst, FIFO), so the
    // cancel sequence is deterministic and testable.
    const book::BookState snap = book_.dump_state();
    for (const auto& level : snap.bids) {
      for (const auto& resting : level.orders) {
        emit_session_cancel(resting);
      }
    }
    for (const auto& level : snap.asks) {
      for (const auto& resting : level.orders) {
        emit_session_cancel(resting);
      }
    }
    book_.clear();
    state_ = SessionState::Closed;
    return std::move(out_);
  }

  /// Dispatch any inbound message to its handler (MATCHING_ENGINE_SPEC top-level
  /// dispatch).
  std::vector<Outbound> process(const core::Inbound& msg) {
    return std::visit([this](const auto& m) -> std::vector<Outbound> { return this->process(m); },
                      msg);
  }

  // ----- read-only views for the CLI, tests, and (later) MD --------------------

  [[nodiscard]] const Book& book() const noexcept { return book_; }

  [[nodiscard]] const OrderRegistry& registry() const noexcept { return registry_; }

  [[nodiscard]] const RiskEngine& risk() const noexcept { return risk_; }

  [[nodiscard]] SessionState session_state() const noexcept { return state_; }

  /// The session-end mark price in half-ticks (R-12.4, `mark_ht = 2 × mid`).
  /// Valid only after the session has closed; 0 while OPEN. Kept in half-ticks
  /// because a two-sided mid can fall on a half-tick (POSITION_AND_PNL.md); full
  /// P&L marking off this hook is accounting (E11).
  [[nodiscard]] std::int64_t mark_half_ticks() const noexcept { return mark_half_ticks_; }

  [[nodiscard]] const core::InstrumentConfig& instrument() const noexcept { return instr_; }

 private:
  /// Build the single OrderRejected for a failed cancel/modify: the target
  /// order_id is set, client_order_id is unset (events.hpp OrderRejected).
  [[nodiscard]] static core::OrderRejected reject_cancel(const core::CancelOrder& m,
                                                         core::RejectReason reason) {
    return core::OrderRejected{.participant = m.participant,
                               .client_order_id = core::ClientOrderId{},
                               .order_id = m.order_id,
                               .reason = reason};
  }

  /// The single OrderRejected for a failed modify: same shape as a failed cancel
  /// (order_id set, client_order_id unset).
  [[nodiscard]] static core::OrderRejected reject_modify(const core::ModifyOrder& m,
                                                         core::RejectReason reason) {
    return core::OrderRejected{.participant = m.participant,
                               .client_order_id = core::ClientOrderId{},
                               .order_id = m.order_id,
                               .reason = reason};
  }

  /// The OrderModified acknowledging a successful modify (R-7.5): carries the new
  /// total quantity and price the request asked for.
  [[nodiscard]] static core::OrderModified modified_event(const core::ModifyOrder& m,
                                                          const OrderRecord& o) {
    return core::OrderModified{.order_id = m.order_id,
                               .participant = o.participant,
                               .new_qty = m.new_qty,
                               .new_price = m.new_price};
  }

  /// Emit the SESSION_END cancel for one resting order and finalize it (R-12.3).
  void emit_session_cancel(const book::RestingOrder& resting) {
    out_.push_back(core::OrderCanceled{.order_id = resting.id,
                                       .participant = resting.participant,
                                       .remaining_qty = resting.remaining,
                                       .reason = core::CancelReason::SessionEnd});
    registry_.finalize(resting.id, OrderState::Canceled);
  }

  /// R-12.4 mark price in half-ticks: 2×mid if both sides are non-empty, else
  /// 2×last-trade price, else 2×the configured initial reference.
  [[nodiscard]] std::int64_t compute_mark_half_ticks() const {
    const auto bid = book_.best(core::Side::Buy);
    const auto ask = book_.best(core::Side::Sell);
    if (bid && ask) {
      return bid->ticks() + ask->ticks();  // = 2 × mid
    }
    if (last_trade_price_) {
      return 2 * last_trade_price_->ticks();
    }
    return 2 * initial_reference_.ticks();
  }

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
    last_trade_price_ = px;  // R-12.4 mark fallback

    registry_.apply_fill(maker_id, q);
    registry_.apply_fill(taker.id, q);

    // R-9 position tally: the maker rests on the side opposite the aggressor.
    risk_.on_fill(maker_party, core::opposite(taker.side), q);
    risk_.on_fill(taker.participant, taker.side, q);

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
  core::Price initial_reference_;  ///< R-12.4 mark fallback (see constructor)
  Book book_;
  OrderRegistry registry_;
  RiskEngine risk_;
  TradeId next_trade_id_{TradeId::first()};
  SessionState state_{SessionState::Open};       ///< R-12.1
  std::optional<core::Price> last_trade_price_;  ///< R-12.4 mark fallback
  std::int64_t mark_half_ticks_{0};              ///< R-12.4, set at close
  std::vector<Outbound> out_;
};

}  // namespace microsim::engine
