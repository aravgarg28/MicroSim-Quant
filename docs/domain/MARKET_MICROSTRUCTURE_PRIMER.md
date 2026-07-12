# Market Microstructure Primer

This document teaches every microstructure concept MicroSim uses, from zero. It is written so
the project owner can explain each term — and how MicroSim models it — in an interview without
notes. Each entry has: a definition, an example, how MicroSim models it, and an *interview
angle* (the question you're likely to get, and the shape of a good answer).

## The running example

One instrument, tick size $0.01, lot size 1. The **order book** currently looks like this:

```
        BIDS (buyers)              |        ASKS (sellers)
  qty @ price                      |   price @ qty
  ----------                       |   ----------
                                   |   $10.05 @ 40   (order E)
                                   |   $10.03 @ 25   (orders C: 10, D: 15)
  $10.01 @ 30   (orders A:20, B:10)|
  $10.00 @ 50   (order F)          |
  $ 9.98 @ 15   (order G)          |
```

- **Best bid** = $10.01 (the highest price any buyer will pay).
- **Best ask** (or *offer*) = $10.03 (the lowest price any seller will accept).

## Core vocabulary

### Bid
An order to **buy** at or below a stated price. The *best bid* is the highest one.
**In MicroSim:** the buy side of the book, price levels sorted descending.
**Interview angle:** "What's at the top of the book?" — the best bid and best ask, together
called the *top of book* or *BBO* (best bid and offer).

### Ask / Offer
An order to **sell** at or above a stated price. The *best ask* is the lowest one.
**In MicroSim:** the sell side, price levels sorted ascending.

### Spread
`spread = best_ask − best_bid`. Here: $10.03 − $10.01 = $0.02 (2 ticks).
The spread is the market maker's gross compensation and the aggressive trader's cost of
immediacy. A *crossed* book (bid > ask) must never persist after matching — that's engine
invariant INV-1. A *locked* book (bid == ask) is likewise impossible after matching completes.
**Interview angle:** "Why can't the book stay crossed?" — because matching is defined to
execute any marketable order immediately; a persisting cross means the matching engine failed.

### Mid-price
`mid = (best_bid + best_ask) / 2` = $10.02. The conventional "fair price" reference. Note it
can land between ticks — MicroSim therefore computes mids in analysis code (floating point),
never as an authoritative engine price.

### Microprice
A quantity-weighted refinement of mid:
`microprice = (best_bid × ask_qty + best_ask × bid_qty) / (bid_qty + ask_qty)`.
With bid qty 30 and ask qty 25: `(10.01×25 + 10.03×30)/55 ≈ $10.0209`. The intuition: if far
more quantity rests on the bid than the ask, the next move is more likely up, so "fair" sits
closer to the ask. **In MicroSim:** a streaming analysis feature, used in research metrics and
(in Release 3) the imbalance-aware quoter.
**Interview angle:** "Why weight by the *opposite* side's quantity?" — the price is pulled
*toward* the thin side, because the thin side is the one about to run out.

### Limit order
"Buy 10 at $10.01 or better." Rests in the book if not immediately matchable — it *provides*
liquidity and pays the waiting cost: it may never fill, and when it does fill, it's often
because the market moved against it (see adverse selection).

### Market order
"Buy 10 at whatever the book charges." Executes immediately against the best available asks —
it *takes* liquidity and pays the spread (and possibly more, see slippage) for immediacy.
**In MicroSim:** market orders never rest; any unfillable remainder is canceled.

### Order book
The complete set of resting limit orders on both sides, organized by price level. The book *is*
the market state. **In MicroSim:** the central data structure; its design tradeoffs are
documented in `docs/engine/ORDER_BOOK_DESIGN.md`.

### Price level
All resting orders at one price, in arrival order. Level $10.03 above holds orders C then D.
Market data is often published per-level (aggregate quantity) rather than per-order.

### Queue priority (price-time priority)
Who fills first? **Better price first; at the same price, first-come-first-served (FIFO).**
A market sell for 20 hits: A (20 @ $10.01) — fully; not B, because A arrived first. Order B is
*second in queue*; its **queue position** is the quantity ahead of it (20 before the trade,
0 after). Queue position drives fill probability — being first at a price is valuable, which is
why speed matters and why research question RQ3 exists.
**Interview angle:** "When does an order lose its queue position?" — in MicroSim (and most real
exchanges): on any price change, and on any quantity *increase*; a quantity *decrease* keeps
priority. Ask yourself why: allowing size increases to keep priority would let traders reserve
a spot with 1 lot and inflate later.

### Partial fill
An order matched for less than its full quantity. A market buy for 15 fills 10 from C and 5
from D; D is left partially filled with 10 remaining, keeping its priority for the remainder.

### Liquidity maker / liquidity taker
The **maker** is the resting order that was already in the book; the **taker** is the incoming
order that triggered the trade. Every trade has exactly one maker side and one taker side.
Exchanges typically charge takers a fee and pay makers a rebate (the *maker-taker* model) to
attract resting liquidity. **In MicroSim:** flat per-lot taker fee and maker rebate,
configurable, applied in integer cash units (no rounding).

### Inventory
A market maker's current net position (long 500, short 200, …). Market makers want to be
*flat*: inventory is risk — if you're long 500 and the price drops, you lose 500× the drop.
**In MicroSim:** inventory-skewed quoting (shading both quotes downward when long, upward when
short, to attract trades that reduce inventory) is the treatment arm of research question RQ2.

### Adverse selection
The market maker's core occupational hazard: **your resting order fills precisely when the
other side knows (or the flow implies) the price is about to move against you.** Your bid at
$10.01 fills seconds before the market drops to $9.95 — you "won" the trade and lost money.
Formally, MicroSim measures it as *markout*: `(mid_{t+Δ} − trade_price) × side` averaged over
fills — negative markouts mean your fills are toxic.
**Interview angle:** "Why does more latency hurt a market maker?" — slower reaction ⇒ stale
quotes stay executable after the fair price moved ⇒ fast traders pick them off ⇒ more negative
markout. That mechanism is exactly research question RQ1.

### Slippage
The difference between the price you expected (e.g., mid or top-of-book at decision time) and
the price you actually got. A market buy for 60 here: 25 @ $10.03 + 35 @ $10.05 = average
$10.0417 vs. a $10.03 top-of-book expectation — 1.17 cents of slippage from *walking the book*.

### Market impact
How your own trading moves the price. The 60-lot buy above consumed the whole $10.03 level; the
new best ask is $10.05 — the market is now worse for the next buyer (and the mid moved up).
Impact is why large orders are split over time in reality. **In MicroSim:** impact is
*mechanical* (book depletion); informed/momentum agents in Release 5 add *informational* impact.

### Volatility
The scale of price fluctuation, measured in MicroSim as the standard deviation of mid-price
changes over a horizon. Volatility raises inventory risk, so optimal spreads widen with it —
the classic result the Release-3 volatility-adaptive quoter demonstrates.

### Fees and rebates
See maker/taker. Economics matter: a strategy that "captures the spread" of 2 ticks but pays a
taker fee both ways can be unprofitable, while a maker rebate can make even zero-spread-capture
quoting viable. This is why fees are in the MVP (decision D6): P&L without them is fiction.

## How the concepts connect (the market maker's problem)

A market maker continuously posts a bid and an ask around its estimate of fair value, hoping to
earn `spread + 2×rebate` per round trip. Its enemies:

1. **Adverse selection** — fills arrive disproportionately on the wrong side at the wrong time.
2. **Inventory risk** — fills are not balanced; the position drifts, and volatility turns the
   drift into P&L variance.
3. **Queue position** — quoting a good price is worthless if you're last in a long queue;
   only orders near the front trade before prices move.
4. **Latency** — every defense above (re-pricing, canceling stale quotes, skewing) is only as
   good as how quickly you can act.

MicroSim's three research questions are these four forces made measurable: RQ1 = latency vs.
adverse selection, RQ2 = inventory skew as a defense, RQ3 = queue position vs. fill
probability.

## What MicroSim deliberately does not model

Be ready to volunteer these in interviews — knowing the model's edges is more impressive than
the model:

- **No informational content in MVP flow.** Poisson noise flow means adverse selection arises
  only mechanically. Release 5's informed traders add the real thing.
- **No real-world calibration.** Parameters are chosen for internal consistency, not fitted to
  live data; conclusions are about the simulated market.
- **Simplified fees, no tiering, no auctions/halts, single venue, no hidden orders/icebergs.**
- **Latency is a model** (constant + jitter + tail events), not a network simulation.
