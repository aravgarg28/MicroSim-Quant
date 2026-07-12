# MicroSim Exchange Rules

This document is the **authoritative specification** of exchange behavior. Every rule has an ID
(`R-x.y`) so tests and implementation tasks can cite it. If code and this document disagree, the
code is wrong; if this document is ambiguous, the gap is fixed *here* first (never resolved
ad-hoc in code) and logged in `docs/DECISIONS.md`.

The model is a simplified continuous-trading electronic exchange in the style of modern futures
/ equities venues (price-time priority, maker-taker fees). Auctions, halts, hidden orders, and
odd lots do not exist here (see MVP exclusions).

Scope tags: **[MVP]** Release 1–3 · **[R2]** Release 2 · **[R4+]** later release.

---

## 1. Instruments and units

- **R-1.1 [MVP]** An instrument is defined by: `instrument_id` (uint32), `symbol` (string),
  `tick_size` and `lot_size` (positive int64 in *minor units*, see R-1.2), `min_price_ticks`
  (int64, ≥ 1), `max_price_ticks` (int64, > min), `max_order_qty_lots` (int64, ≥ 1). Instrument
  definitions are immutable for the lifetime of a simulation.
- **R-1.2 [MVP]** All authoritative prices are **int64 tick counts** (`price_ticks`); all
  authoritative quantities are **int64 lot counts** (`qty_lots`). External representations
  (e.g. $10.03) exist only at configuration and reporting boundaries, converted exactly via
  `tick_size`/`lot_size` in integer minor units (e.g. tick_size = 1 cent). No floating point
  appears anywhere in the matching path. (Full rationale: `docs/numerics/NUMERIC_REPRESENTATION.md`.)
- **R-1.3 [MVP]** Cash amounts (notional, fees, P&L) are int64 in minor units. Trade notional =
  `price_ticks × tick_size × qty_lots × lot_size` — exact integer arithmetic; overflow limits
  documented in the numerics spec and enforced by config validation.

## 2. Participants

- **R-2.1 [MVP]** A participant is identified by `participant_id` (uint32), registered before
  the simulation starts. Orders from unregistered participants are rejected
  (`UNKNOWN_PARTICIPANT`).
- **R-2.2 [MVP]** Each participant has a risk profile (see §9) and, from **[R2]**, a self-trade
  prevention policy.

## 3. Order types and fields

- **R-3.1 [MVP]** Supported order types: **LIMIT** and **MARKET**. No other types exist.
  Time-in-force is implicitly DAY for LIMIT (rests until canceled or session end) and
  immediate for MARKET. IOC/FOK are not supported in the MVP (documented extension).
- **R-3.2 [MVP]** `NewOrder` message fields (all required unless noted):
  `participant_id`, `client_order_id` (uint64, participant-chosen), `instrument_id`,
  `side` (BUY/SELL), `type` (LIMIT/MARKET), `qty_lots`, `price_ticks` (LIMIT only; must be
  **absent/zero** for MARKET).
- **R-3.3 [MVP]** Validation, applied in this exact order, first failure wins (one reject per
  message, deterministic reject reason):
  1. instrument exists → else `UNKNOWN_INSTRUMENT`
  2. participant registered → else `UNKNOWN_PARTICIPANT`
  3. type/side valid enums → else `MALFORMED`
  4. MARKET order has no price → else `PRICE_ON_MARKET_ORDER`
  5. `qty_lots ≥ 1` → else `INVALID_QTY`
  6. `qty_lots ≤ max_order_qty_lots` → else `ORDER_TOO_LARGE`
  7. LIMIT: `min_price_ticks ≤ price_ticks ≤ max_price_ticks` → else `PRICE_OUT_OF_BANDS`
  8. `client_order_id` not previously used by this participant this session → else
     `DUPLICATE_CLIENT_ORDER_ID`
  9. risk checks (§9), in their specified order.
- **R-3.4 [MVP]** Prices and quantities arriving already in ticks/lots means tick/lot-size
  violations are impossible *inside* the engine; conversion at the config/Python boundary
  rejects non-integral prices (e.g. $10.031 with 1-cent tick) with `INVALID_TICK` before a
  message is ever created.

## 4. Order identity and lifecycle

- **R-4.1 [MVP]** On acceptance the exchange assigns `order_id` (uint64), strictly increasing
  across all orders in arrival order. `order_id` is the sole key for cancel/modify.
- **R-4.2 [MVP]** Order states: `RESTING` (has unfilled quantity in the book),
  `FILLED` (remaining qty 0 via trades), `CANCELED` (removed with remaining qty > 0 — by
  request, by market-order remainder cancel, by STP, by session end, or by modify-to-done).
  MARKET orders and marketable LIMIT quantity execute in-flight without ever being `RESTING`.
  Terminal states (`FILLED`, `CANCELED`) are absorbing: no event may act on a terminal order.
- **R-4.3 [MVP]** Every accepted `NewOrder` produces exactly one `OrderAccepted` event, then
  zero or more `Fill` events, then at most one of `OrderCanceled` (with reason) — and produces
  `OrderFilled`-terminal implicitly when cumulative fills reach the original quantity. Every
  rejected message produces exactly one `OrderRejected` event with a single reason code.

## 5. Matching — continuous price-time priority

- **R-5.1 [MVP]** The engine processes exactly one inbound message at a time, to completion
  (all resulting fills, book updates, and events emitted) before the next message. There is no
  interleaving. This is the atomicity foundation for every invariant.
- **R-5.2 [MVP]** Priority: for matching, the opposite side's orders rank by (a) **price** —
  best first (highest bid / lowest ask), then (b) **time** — FIFO by `order_id` within a price
  level. Rank is total and deterministic because `order_id` is unique and increasing.
- **R-5.3 [MVP]** An incoming BUY LIMIT at price *p* is *marketable* while
  `best_ask ≤ p`; symmetric for SELL. Matching loop: repeatedly take the front order of the
  best opposite level, trade `min(incoming_remaining, resting_remaining)`, until the incoming
  order is exhausted or no longer marketable.
- **R-5.4 [MVP]** **Execution price is always the resting order's price** (the aggressor gets
  price improvement). One incoming order may fill at multiple prices while walking the book.
- **R-5.5 [MVP]** MARKET orders are marketable against any opposite liquidity. Remaining
  quantity after the opposite side empties is canceled with reason `NO_LIQUIDITY` (a MARKET
  order never rests). A MARKET order arriving to an empty opposite side is accepted and then
  immediately canceled in full (`OrderAccepted` → `OrderCanceled(NO_LIQUIDITY)`, zero fills).
- **R-5.6 [MVP]** Unfilled remainder of a LIMIT order joins the book at its limit price, at the
  back of that price level's queue.
- **R-5.7 [MVP]** Each trade produces exactly one `Trade` event with strictly-increasing
  `trade_id`, executed `price_ticks`, `qty_lots`, maker `order_id`, taker `order_id`, maker and
  taker `participant_id`s, and the aggressor side.
- **R-5.8 [MVP]** After processing any message completes, `best_bid < best_ask` holds whenever
  both sides are non-empty (no locked or crossed book — invariant INV-1).

## 6. Cancellation

- **R-6.1 [MVP]** `CancelOrder` fields: `participant_id`, `order_id`. A participant may cancel
  only its own orders → else `NOT_ORDER_OWNER`.
- **R-6.2 [MVP]** Canceling a `RESTING` order removes it immediately from the book and emits
  `OrderCanceled(BY_REQUEST)` with the remaining quantity.
- **R-6.3 [MVP]** Canceling an unknown `order_id` → reject `UNKNOWN_ORDER`. Canceling a
  terminal order → reject `TOO_LATE_TO_CANCEL`. (These are distinct codes: one is a client bug,
  the other a race the client can legitimately lose — the distinction matters in latency
  experiments.)

## 7. Modification (cancel/replace)

- **R-7.1 [MVP]** `ModifyOrder` fields: `participant_id`, `order_id`, `new_qty_lots`,
  `new_price_ticks`. Both fields are required (send current values to leave one unchanged).
  Validation follows R-3.3 items 5–7 semantics plus ownership (R-6.1) and terminal-state rules
  (R-6.3, reason `TOO_LATE_TO_MODIFY`).
- **R-7.2 [MVP]** Priority rules:
  - **Price change** (any) → order loses time priority: treated as atomic cancel + new arrival
    (keeps the same `order_id`, but is assigned a fresh queue position and may match
    immediately per R-5.3 if now marketable).
  - **Quantity decrease, same price** → order keeps its queue position; remaining quantity is
    reduced.
  - **Quantity increase, same price** → loses time priority (as price change: re-queued at the
    back of its level).
- **R-7.3 [MVP]** `new_qty_lots` is the new **total** quantity. If
  `new_qty_lots ≤ already_filled_qty`, the modify cancels the remainder: emit
  `OrderCanceled(MODIFY_TO_DONE)`. Otherwise remaining = `new_qty_lots − already_filled_qty`.
- **R-7.4 [MVP]** A modify that makes the order marketable executes immediately under §5, as if
  newly arrived.
- **R-7.5 [MVP]** Exactly one `OrderModified` event is emitted on success (before any fills the
  modify triggers).

## 8. Self-trade prevention **[R2]**

- **R-8.1 [MVP]** In Release 1, self-trades are permitted and execute normally (documented
  simplification; noise agents may self-cross harmlessly).
- **R-8.2 [R2]** From Release 2, each participant has an STP policy: `ALLOW` (default for noise
  agents) or `CANCEL_NEWEST` (default for strategies). Under `CANCEL_NEWEST`: when the incoming
  order would next match a resting order of the same participant, the **incoming** order's
  remaining quantity is canceled (`OrderCanceled(SELF_TRADE_PREVENTED)`); the resting order is
  untouched; fills already executed stand.

## 9. Pre-trade risk checks **[MVP — applied at acceptance, deterministic order]**

Applied after R-3.3 items 1–8, in this order; first failure rejects:

- **R-9.1** Open-order count: `open_orders(participant) < max_open_orders` → else
  `MAX_OPEN_ORDERS`.
- **R-9.2** Order size already covered by R-3.3(6) per instrument; participants may configure a
  stricter `max_order_qty_lots` → else `RISK_ORDER_TOO_LARGE`.
- **R-9.3** Position limit uses **worst-case resulting position**:
  `|position + signed_open_qty(side) + qty| ≤ max_position_lots` where `signed_open_qty(side)`
  sums same-side open remaining quantity → else `MAX_POSITION`. (Worst-case, not
  fill-optimistic: an order that *could* breach the limit if everything fills is rejected.)
- **R-9.4** Risk state updates and event emission are part of the same atomic message
  processing (R-5.1): a cancel that frees open-order slots takes effect for the *next* message,
  not retroactively.
- **R-9.5** Modifies re-run risk checks against the *delta* they introduce; a modify that fails
  risk is rejected and leaves the original order untouched.

## 10. Sequencing, time, and determinism

- **R-10.1 [MVP]** The **sequencer** assigns every inbound message a strictly-increasing
  `seq` (uint64) in arrival order; arrival order is fully determined by the simulation clock
  (see `docs/simulation/SIMULATION_CLOCK.md` for tie-breaking). Engine behavior is a pure
  function of the sequenced message stream.
- **R-10.2 [MVP]** Every outbound event carries: `seq_out` (strictly increasing, gap-free per
  simulation), the triggering inbound `seq`, and `ts_event` (int64 nanoseconds, logical
  simulation time at processing).
- **R-10.3 [MVP]** Replaying the same sequenced input stream produces a byte-identical outbound
  event stream. No wall-clock reads, no unseeded randomness, no address-dependent iteration
  order anywhere in the engine.

## 11. Fees **[MVP]**

- **R-11.1** Fee model: flat per-lot maker rebate and taker fee, in cash minor units, set per
  instrument: `taker_fee_per_lot ≥ 0` (charged to taker), `maker_rebate_per_lot ≥ 0` (credited
  to maker). Defaults: taker fee > maker rebate (venue is net-positive). Exact integers — no
  rounding exists in the fee path. (Basis-point fees are a documented extension; rejected for
  MVP to keep arithmetic exact. See decision log.)
- **R-11.2** Fees are computed per `Trade` event and carried on the fill: taker pays
  `qty_lots × taker_fee_per_lot`, maker receives `qty_lots × maker_rebate_per_lot`.
  Self-trades (R-8.1) pay/receive both legs.

## 12. Trading states and session end

- **R-12.1 [MVP]** Trading states: `OPEN` → `CLOSED`. One session per simulation run; no
  halts, no auctions, no re-opening.
- **R-12.2 [MVP]** While `CLOSED` (after the configured session-end time), all inbound order
  messages are rejected with `MARKET_CLOSED`.
- **R-12.3 [MVP]** At the `OPEN → CLOSED` transition the engine cancels every resting order,
  in book order (bids best-to-worst then asks best-to-worst; FIFO within level), emitting
  `OrderCanceled(SESSION_END)` for each. Determinism of this order is testable.
- **R-12.4 [MVP]** The session-end **mark price** for unrealized P&L is: mid-price if both
  sides were non-empty immediately before cancellation; else last trade price; else the
  configured initial reference price. (Mid uses round-half-away-from-zero to whole ticks × 2 —
  exact rule in `docs/accounting/POSITION_AND_PNL.md`.)

## 13. Market data (summary — full spec in `docs/engine/MARKET_DATA_PROTOCOL.md`)

- **R-13.1 [R2]** The engine publishes, per processed message: zero or more incremental book
  updates, zero or more trade messages, all stamped with a per-instrument gap-free
  `md_seq`. Snapshots are available on demand and carry the `md_seq` they are consistent with.
- **R-13.2 [R2]** The feed contains only information a real participant could see: no
  participant IDs on book updates, no resting-order ownership. Trade messages carry aggressor
  side but not participant identities. (Fills are reported privately to the two counterparties
  on their own event streams.) This is what makes "no look-ahead" enforceable.

## 14. Reject/cancel reason codes (complete enumeration)

`UNKNOWN_INSTRUMENT`, `UNKNOWN_PARTICIPANT`, `MALFORMED`, `PRICE_ON_MARKET_ORDER`,
`INVALID_QTY`, `ORDER_TOO_LARGE`, `PRICE_OUT_OF_BANDS`, `INVALID_TICK` (boundary only),
`DUPLICATE_CLIENT_ORDER_ID`, `MAX_OPEN_ORDERS`, `RISK_ORDER_TOO_LARGE`, `MAX_POSITION`,
`UNKNOWN_ORDER`, `NOT_ORDER_OWNER`, `TOO_LATE_TO_CANCEL`, `TOO_LATE_TO_MODIFY`,
`MARKET_CLOSED` — rejects.
`BY_REQUEST`, `NO_LIQUIDITY`, `SELF_TRADE_PREVENTED` [R2], `MODIFY_TO_DONE`, `SESSION_END`
— cancel reasons.
Adding a code requires updating this section and the corresponding tests.

---

## Worked example (cites rules)

Book as in the primer. Incoming: MARKET BUY 60, participant P9.

1. Validation R-3.3 passes; risk §9 passes. `OrderAccepted` (order_id 101, seq'd) — R-4.3.
2. Match R-5.3/R-5.5: best ask $10.03 holds C(10) then D(15) — FIFO by R-5.2.
   - Trade 1: 10 @ $10.03 vs C (maker). C → FILLED.
   - Trade 2: 15 @ $10.03 vs D (maker). D → FILLED. Level $10.03 now empty.
   - Best ask now $10.05, E(40). Trade 3: 35 @ $10.05 vs E. E partially filled, 5 remaining,
     E keeps queue priority for its remainder (R-5.6 not triggered — E never left the book).
3. Incoming filled 60/60 → terminal FILLED. Execution prices were the resting prices (R-5.4):
   taker average $10.0417 — the slippage example from the primer.
4. Fees R-11.2: P9 pays 60 × taker_fee; C, D, E's owners receive rebates on their filled lots.
5. Post-state check R-5.8: best bid $10.01 < best ask $10.05. ✓
