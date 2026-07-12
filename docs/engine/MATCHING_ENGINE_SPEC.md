# Matching Engine Specification

Pseudocode-level specification of the engine pipeline (gateway → sequencer → risk → matcher).
This is the implementation contract: Opus translates *this*, citing rule IDs from
`EXCHANGE_RULES.md`. Pseudocode favors clarity; the C++ may restructure freely as long as the
observable event stream is identical.

## Input events (from the event loop, post-latency)

`NewOrder`, `CancelOrder`, `ModifyOrder` (fields per R-3.2, R-6.1, R-7.1), plus the internal
`SessionEnd` control event (R-12).

## Output events

Private (to owner, via latency): `OrderAccepted`, `OrderRejected{reason}`,
`OrderCanceled{reason, remaining_qty}`, `OrderModified`, `Fill{trade_id, price, qty, fee,
liquidity_flag}` (one per counterparty per trade).
Public (to feed): `TradeMD{trade_id, price, qty, aggressor_side}`, book deltas
(`MARKET_DATA_PROTOCOL.md`).
Audit: every inbound message + every outbound event, sequenced, to the log (R-10.2).

## Top-level dispatch

```text
process(msg):                          # R-5.1: atomic, one at a time
    seq = sequencer.next(msg)          # stamp seq, ts_event; tee to input log
    if session.state == CLOSED: emit OrderRejected(MARKET_CLOSED); return   # R-12.2
    switch msg.kind:
        NewOrder    -> handle_new(msg)
        CancelOrder -> handle_cancel(msg)
        ModifyOrder -> handle_modify(msg)
        SessionEnd  -> handle_session_end()
    assert_invariants_if_test_build()  # INV checkpoint
```

## New order

```text
handle_new(m):
    reason = validate(m)               # R-3.3 items 1-8, in order
    if reason: emit OrderRejected(reason); return
    reason = risk.check_new(m)         # R-9.1..9.3, in order
    if reason: emit OrderRejected(reason); return

    o = registry.create(m)             # assign order_id (R-4.1), slab slot
    emit OrderAccepted(o)
    match_loop(o)                      # may fully fill
    if o.remaining > 0:
        if o.type == MARKET:
            emit OrderCanceled(o, NO_LIQUIDITY)      # R-5.5
            registry.finalize(o)
        else:
            book.add(o)                              # R-5.6, back of level queue
            md.publish_level_delta(...)
    else:
        registry.finalize(o)                         # FILLED terminal
```

## Matching loop

```text
match_loop(o):                         # R-5.2..5.4
    while o.remaining > 0 and marketable(o):
        r = book.front(opposite(o.side))             # best price, FIFO front
        q = min(o.remaining, r.remaining)
        px = r.price                                 # R-5.4: maker's price
        execute_trade(maker=r, taker=o, px, q)
        if r.remaining == 0:
            book.remove(r); registry.finalize(r)     # FILLED

marketable(o):
    opp_best = book.best(opposite(o.side))
    if opp_best is empty: return false
    if o.type == MARKET: return true
    return (o.side == BUY)  ? opp_best.price <= o.price
                            : opp_best.price >= o.price

execute_trade(maker, taker, px, q):
    trade_id = next_trade_id()
    maker.remaining -= q; taker.remaining -= q
    fees = (maker: -rebate_per_lot*q, taker: +fee_per_lot*q)      # R-11
    accounting.apply_fill(maker, px, q, MAKER, fees.maker)
    accounting.apply_fill(taker, px, q, TAKER, fees.taker)
    emit Fill(maker...), Fill(taker...)              # private, both sides
    md.publish_trade(trade_id, px, q, aggressor=taker.side)
    md.publish_level_delta(maker.level)              # qty reduction / removal
```

Self-trade prevention **[R2]** inserts before `execute_trade` in the loop:

```text
    if stp_applies(o.participant, r):                # R-8.2 CANCEL_NEWEST
        emit OrderCanceled(o, SELF_TRADE_PREVENTED); registry.finalize(o); return
```

## Cancel

```text
handle_cancel(m):                      # R-6
    o = registry.lookup(m.order_id)
    if o is missing:        emit OrderRejected(UNKNOWN_ORDER); return
    if o.owner != m.participant: emit OrderRejected(NOT_ORDER_OWNER); return
    if o.terminal:          emit OrderRejected(TOO_LATE_TO_CANCEL); return
    book.remove(o); md.publish_level_delta(...)
    emit OrderCanceled(o, BY_REQUEST, remaining=o.remaining)
    registry.finalize(o)
```

## Modify (cancel/replace, R-7)

```text
handle_modify(m):
    o = registry.lookup(m.order_id)
    ... ownership/terminal checks as cancel (reasons TOO_LATE_TO_MODIFY) ...
    reason = validate_modify(m)        # qty/price bands per R-7.1
    if reason: emit OrderRejected(reason); return
    reason = risk.check_modify(o, m)   # R-9.5: delta-based; failure leaves o untouched
    if reason: emit OrderRejected(reason); return

    if m.new_qty <= o.filled:                        # R-7.3
        book.remove(o); emit OrderModified(o, m); emit OrderCanceled(o, MODIFY_TO_DONE)
        registry.finalize(o); return

    keep_priority = (m.new_price == o.price) and (m.new_qty < o.total_qty)   # R-7.2
    o.total_qty = m.new_qty; o.remaining = m.new_qty - o.filled
    emit OrderModified(o, m)
    if keep_priority:
        book.reduce_in_place(o); md.publish_level_delta(...)
    else:
        book.remove(o); o.price = m.new_price
        match_loop(o)                                # R-7.4: may execute immediately
        if o.remaining > 0: book.add(o)              # fresh queue position
        else: registry.finalize(o)
        md.publish_level_delta(...)
```

## Session end (R-12.3)

```text
handle_session_end():
    for side in [BID, ASK]:
        for level in book.levels_best_to_worst(side):
            for o in level.fifo_order():
                emit OrderCanceled(o, SESSION_END, remaining=o.remaining)
                registry.finalize(o)
    book.clear(); md.publish_snapshot(empty)
    accounting.mark_session_end(mark_price())        # R-12.4
    session.state = CLOSED
```

## Error responses

Every failure path emits exactly one event with exactly one reason (first failure wins —
R-3.3). The engine never throws on bad input; exceptions/asserts are reserved for broken
invariants (architecture rule 3).

## Determinism requirements on the implementation

- No iteration over unordered containers anywhere an order or event sequence is derived.
- No pointer-value-dependent comparisons or hashing that reaches output.
- All tie-breaking already resolved by the sequencer; the matcher itself is deterministic by
  construction (FIFO + price arrays).
- Event emission order within one message is fixed by this spec: private before public per
  trade batch, deltas after their causing trades, all per the pseudocode line order.

## State-transition summary (order registry)

```text
            accept           fills reach total
  (new) ────────────▶ LIVE ────────────────────▶ FILLED
                       │  cancel/STP/NO_LIQUIDITY/session-end/modify-to-done
                       └──────────────────────▶ CANCELED
  LIVE splits into: in-flight (matching) and RESTING (in book); both may reach either terminal.
  Terminal states absorbing (INV-7); slot recycled after terminal event emitted.
```

## Audit and test hooks

- `assert_invariants_if_test_build()` runs INV-1..14 checks (O(book)) after every message in
  property-test builds; compiled out of benchmark/release builds.
- The engine exposes `dump_state()` (delegating to the book + registry) solely for
  differential testing (INV-15); the public feed never uses it.
