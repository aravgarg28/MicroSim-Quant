# Order-Book Design

The order book is the data-structure showpiece of the project. This document compares the
realistic designs, fixes the two we build (`ReferenceBook` and `FastBook`), and records the
assumptions that make the fast design valid.

## Workload (what the structure must be fast at)

Realistic exchange message mixes are cancel-dominated. Target mix for benchmarks (basis:
public studies of equities/futures feeds; exact mix is a config of the flow generator):
~50–60% cancels/modifies, ~35–45% new limit orders (mostly non-marketable, at or near the top
of book), ~2–5% marketable orders. Operations by frequency:

| Operation | Frequency | Structure requirement |
|---|---|---|
| Best bid/ask read | every message | O(1), branch-light |
| Insert at/near best | very high | fast level lookup + FIFO append |
| Cancel by order_id | very high | O(1) locate + O(1) unlink |
| Execute at front of best level | high | O(1) pop-front + level maintenance |
| Reduce qty in place | medium | O(1) |
| Insert far from best | low | may be slower without harm |
| Walk many levels (big sweep) | rare | sequential level access should be cache-friendly |

Key empirical fact shaping everything: **activity concentrates within a few ticks of the best
price**, and instruments trade within a bounded price band per session (R-1.1 requires
configured `min/max_price_ticks`).

## Candidate designs

### A. `std::map<Price, Level>` per side (red-black tree)
- Lookup/insert/erase level: O(log L). Best price: `begin()` O(1) amortized.
- Cancel: needs separate id→order map, then O(1) in-level unlink if lists are intrusive.
- **Cache behavior: poor** — node-per-level, pointer chasing, allocator traffic per level.
- Implementation difficulty: trivial. **No price-range assumption.**
- Verdict: perfect for the ReferenceBook (obviousness beats everything there).

### B. Sorted flat vector of levels (`boost::flat_map` style)
- Best: O(1) (back/front). Level lookup: O(log L) binary search but contiguous → cache-friendly.
- Insert/erase of a *level* is O(L) memmove — fine for small L, painful for deep books.
- Difficulty: low. No range assumption.
- Verdict: respectable middle; dominated by D given our banded-price assumption.

### C. Hash map of levels + heap/tracking for best price
- Level lookup O(1), but best-price maintenance is the weak point: heaps hold stale entries
  (lazy deletion) and worst cases get ugly; iteration in price order (sweeps, MD snapshots) is
  awkward.
- Verdict: rejected — complexity without a clear win over D.

### D. **Contiguous array of levels indexed by tick offset** (`levels[(price − min) / tick]`)
- Level lookup: **O(1) arithmetic, no search at all.** Cancel: O(1). Insert: O(1).
- Best-price maintenance: cached best index; on emptying the best level, scan toward worse
  prices to the next non-empty level. Worst case O(price range), but the next non-empty level
  is almost always within a few ticks (see workload); a summary bitmap (one bit per level,
  `std::countr_zero` over 64-level words) bounds the scan at ~R/64 word probes for pathological
  gaps.
- Memory: `(max−min)/tick × sizeof(Level)`. E.g. a 20%-wide band on a $100/0.01-tick
  instrument = 4,000 levels × 32 B = 128 KB per side — fits comfortably in L2.
- **Assumption: bounded, pre-declared price band** (R-1.1). A fat-finger price outside the
  band is rejected (`PRICE_OUT_OF_BANDS`) — the band is a market rule, not a hidden limit.
- Difficulty: moderate (index math, bitmap, careful best tracking).
- Verdict: **chosen for `FastBook`.**

### E. Intrusive doubly-linked FIFO per level + slab order store
Not an alternative to A–D but the *within-level* design, combinable with any of them:
orders live in the slab (`MEMORY_MODEL.md`), each carries `prev/next` OrderIndex; `Level`
holds head/tail + aggregate qty. Append, unlink, pop-front all O(1) with zero allocations.
**Used by both books' interfaces; the ReferenceBook instead uses `std::list` + `std::map`
lookups to stay obvious.**

## The two implementations

### `ReferenceBook` (permanent — the truth oracle)
`std::map<Price, std::list<Order>>` per side + `std::map<OrderId, locator>`. Every operation
written to read like EXCHANGE_RULES.md prose. No performance consideration whatsoever. Full
state dump (levels → ordered order lists) for differential comparison (INV-15).

### `FastBook` (the engine's book)
Design D + E: per side, `std::vector<Level>` indexed by tick offset, 64-bit occupancy bitmap
hierarchy for next-non-empty scans, cached best index; orders in the shared slab with
intrusive FIFO links; open-addressing `order_id → OrderIndex` table (power-of-two capacity,
linear probing, tombstone-free deletion via backward-shift). Zero steady-state allocations
after `reserve()` (order count and level count are config-bounded).

**Shared interface (concept `OrderBookLike`):** `add(order) → level_pos`,
`reduce(idx, qty)`, `remove(idx)`, `requeue(idx, new_price, new_qty)`, `front(side)`,
`best(side)`, `depth(side, price)`, `for_each_level(side, fn)`, `dump_state()`. The matching
engine is templated on the book type, so unit/property/differential tests and benchmarks all
instantiate both books over identical scenarios.

## Complexity summary (FastBook)

| Operation | Complexity | Notes |
|---|---|---|
| best/front | O(1) | cached index |
| insert | O(1) | index math + list append (+ bitmap set) |
| cancel by id | O(1) expected | hash probe + unlink (+ bitmap clear if level empties) |
| execute front | O(1) | pop + aggregate update |
| best-level empties | O(gap/64) | bitmap scan; O(1) in practice |
| full sweep of k levels | O(k) sequential | contiguous — prefetch-friendly |

## MVP vs optimized rollout

Release 1 ships **both books**, but `FastBook`'s fancier pieces land in stages with benchmarks
justifying each (per `OPTIMIZATION_ROADMAP.md`): stage 1 = array-of-levels with
`std::unordered_map` id lookup and `std::deque` FIFOs (simple, correct, already fast); stage 2
(post-R3 optimization pass) = intrusive lists + open addressing + occupancy bitmaps, each step
measured. The interface never changes — this is the memory model's "optimizations are
refactorings" promise kept.
