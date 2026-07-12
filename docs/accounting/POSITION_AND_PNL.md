# Position and P&L Accounting

Exact-integer accounting per participant. Everything here is testable arithmetic; the worked
examples below are the unit-test fixtures.

## State per participant (all int64)

| Field | Meaning |
|---|---|
| `position` | net lots, signed (+long / −short) |
| `cash` | minor units, signed; starts at 0 (relative accounting); **includes** fee flows |
| `basis` | signed cost of the open position in minor units (what was paid for it; negative = proceeds held against a short) |
| `fees_paid`, `rebates_received` | gross fee ledgers (reporting; already reflected in `cash`) |
| `realized_pnl` | closed-trade P&L, excluding fees (fees reported separately) |
| `equity_peak` | running max of marked equity (for drawdown) |

## Fill application (the only mutating operation)

For a fill: side s (+1 buy / −1 sell), quantity q lots, notional n = `Notional(px, q)` minor
units (exact per NUMERIC_REPRESENTATION.md), fee f (taker) or rebate r (maker):

```text
cash -= s·n                     # buys pay, sells receive
cash -= f  (taker)  |  cash += r  (maker);  ledgers updated

if position and s have the same sign or position == 0:      # increasing / opening
    basis += s·n
    position += s·q
else:                                                        # reducing / closing / flipping
    close_q = min(q, |position|)
    alloc   = trunc_toward_zero(basis × close_q / |position|)   # proportional cost release
    realized_pnl += s_close·(alloc_side_math)   # concretely:
        # closing longs  (s = −1): realized += n_close − alloc
        # closing shorts (s = +1): realized += −alloc − n_close   (alloc is negative for shorts)
    basis    -= alloc
    position += s·close_q
    if q > close_q:                              # flip: remainder opens the other way
        open the remaining (q − close_q) as a fresh position (recurse into the first branch
        with n apportioned exactly: n_open = n − n_close, both integer by construction:
        n_close = Notional(px, close_q), n_open = Notional(px, q − close_q))
```

The proportional-release division truncates toward zero; the un-released remainder simply
stays in `basis` and comes out on the final closing fill — so cumulative realized P&L over a
full round trip is **exact**, and interim splits are conservative by at most a few minor units
(documented, tested).

## Marking and unrealized P&L

Mark price rule (R-12.4): mid if two-sided, else last trade, else initial reference. Mids can
be half-ticks, so marking uses **half-tick integers**: `mark_ht = bid_ticks + ask_ticks`
(= 2×mid). Marked value of the position:

`mark_value = round_half_away_from_zero(position × mark_ht × tick_size × lot_size / 2)`

— the **only rounding in the system** (D15 note), affecting reported values only, never cash.

`unrealized_pnl = mark_value − basis`
`equity = cash + mark_value`
`total_pnl = equity − initial_cash (= equity, since cash starts at 0)`

**Identity (tested as INV-11d):** `total_pnl = realized_pnl + unrealized_pnl − fees_paid +
rebates_received` — exactly, at every mark point, by construction (see derivation in the
appendix of this doc's test fixtures).

`drawdown = equity_peak − equity`; `max_drawdown` = session max of that.
`inventory_exposure = |mark_value|` (reporting), `|position|` in lots for risk checks.

## Worked example 1 — long round trip with fees (unit-test fixture `acct_example_1`)

Instrument: tick_size = 1¢, lot_size = 1. Taker fee 2¢/lot, maker rebate 1¢/lot.

| # | Event | position | cash (¢) | basis (¢) | realized (¢) | notes |
|---|---|---|---|---|---|---|
| 0 | start | 0 | 0 | 0 | 0 | |
| 1 | BUY 10 @ $10.03 (taker) | +10 | −10 050 | +10 030 | 0 | n=10 030, fee 20 |
| 2 | SELL 6 @ $10.10 (maker) | +4 | −3 984 | +4 012 | **+42** | proceeds 6 060, rebate 6; alloc = ⌊10 030·6/10⌋ = 6 018 |
| 3 | mark @ mid $10.085 (bid 10.08/ask 10.09) | +4 | −3 984 | +4 012 | +42 | mark_ht = 2 017; mark_value = 4·2 017/2·1·1 = 4 034 |

Check: unrealized = 4 034 − 4 012 = **+22**. equity = −3 984 + 4 034 = **+50**.
Identity: realized 42 + unrealized 22 − fees 20 + rebates 6 = **50** ✓.

## Worked example 2 — short then flip (fixture `acct_example_2`)

Same instrument, all fills taker (fee 2¢/lot).

| # | Event | position | cash (¢) | basis (¢) | realized (¢) |
|---|---|---|---|---|---|
| 1 | SELL 5 @ $10.00 | −5 | +4 990 | −5 000 | 0 |
| 2 | BUY 8 @ $9.90 → closes 5, opens +3 | +3 | −2 946 | +2 970 | **+50** |
| 3 | mark @ last trade $9.90 | +3 | −2 946 | +2 970 | +50 |

Row 1 detail: n = 5 000, fee 10 → cash +4 990; basis −5 000 (proceeds held against short).
Row 2 detail: n = 7 920, fee 16 → cash = 4 990 − 7 920 − 16 = −2 946. Close 5:
n_close = 4 950, alloc = trunc(−5 000·5/5) = −5 000, realized += −alloc − n_close =
5 000 − 4 950 = **+50**. Basis −= alloc → 0; position → 0. Open 3: n_open = 2 970 →
basis +2 970, position +3.
Row 3 check: mark_ht = 2·990 = 1 980; mark_value = 3·1 980/2 = 2 970.
unrealized = 2 970 − 2 970 = **0** (position flat at its own cost). equity = −2 946 + 2 970 =
**+24**. Identity: realized 50 + unrealized 0 − fees 26 + rebates 0 = **24** ✓.

## Venue accounting and global reconciliation (INV-11)

- Σ `position` over participants = 0 at all times (every lot bought was sold).
- `venue_take = Σ fees_paid − Σ rebates_received ≥ 0` (config default keeps fee > rebate).
- Σ `cash` over participants + `venue_take` = 0 — cash is conserved to the minor unit.
- Σ `realized + unrealized` over participants + `venue_take` = 0 at any common mark price
  (one participant's gain is another's loss plus the venue's take).

These four are asserted continuously in property builds and at session end always (INV-17).

## Strategy-level vs participant-level

MVP: one strategy = one participant; the terms are interchangeable. Multi-strategy
participants (shared risk, separate books) are out of scope until someone needs them — noted
as an extension, not designed speculatively.
