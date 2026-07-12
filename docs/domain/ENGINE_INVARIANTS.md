# Engine Invariants

These properties must hold at all times (or at the stated checkpoint). They are the contract
the property-based test suite enforces: every invariant below maps to at least one generated
scenario test in `docs/testing/PROPERTY_TESTS.md`, and most are also checked by differential
testing against the reference order book.

**Checking points:** unless stated otherwise, an invariant is asserted **after each inbound
message is fully processed** (the atomic unit of R-5.1). "Book" means the optimized engine's
book; "reference" means the slow reference implementation fed the same input.

| ID | Invariant | Rule basis |
|----|-----------|------------|
| INV-1 | **No crossed or locked book:** if both sides non-empty, `best_bid < best_ask`. | R-5.8 |
| INV-2 | **Side ordering:** bid level prices strictly decreasing from best; ask level prices strictly increasing from best; no empty price level objects persist. | R-5.2 |
| INV-3 | **FIFO within level:** orders at a price level are ordered by strictly-increasing `order_id`, except orders re-queued by modify (R-7.2), whose queue entry ranks by re-queue time — formally, by a strictly-increasing `queue_token` assigned at each (re)queue event. | R-5.2, R-7.2 |
| INV-4 | **No overfill:** for every order, `0 ≤ cumulative_filled ≤ original_qty` (with modifies: ≤ latest total qty per R-7.3), and remaining = total − filled at all times. | R-4.2 |
| INV-5 | **Trade symmetry:** every trade has exactly one maker and one taker; executed buy lots = executed sell lots per trade and in aggregate. | R-5.7 |
| INV-6 | **Quantity conservation:** for every order, `original_qty = filled_qty + canceled_qty + resting_qty` (canceled_qty > 0 only in terminal CANCELED state; resting_qty > 0 only in RESTING). Summed over all orders, book depth equals total resting quantity exactly. | R-4.2/3 |
| INV-7 | **Terminal states are absorbing:** no fill, cancel, or modify event ever references an order after its terminal event. Canceled orders never trade afterward. | R-4.2, R-6.3 |
| INV-8 | **Idempotent rejection of duplicates:** a duplicate `client_order_id` never creates a second order; re-sent cancels/modifies of terminal orders are rejected, never applied twice. | R-3.3(8), R-6.3 |
| INV-9 | **Monotonic sequencing:** inbound `seq` and outbound `seq_out` are strictly increasing and gap-free; per-instrument `md_seq` is gap-free; `order_id` and `trade_id` are strictly increasing. | R-10.1/2 |
| INV-10 | **Determinism:** identical sequenced input stream (same config, same seed) ⇒ byte-identical outbound event stream, on every run. | R-10.3 |
| INV-11 | **Accounting reconciliation** (checked continuously and at session end): (a) Σ position over all participants = 0; (b) Σ cash + Σ fees retained by venue = 0 (cash is conserved; the venue's fee take = Σ taker fees − Σ maker rebates); (c) each participant's position = Σ signed filled lots; (d) each participant's realized + unrealized P&L is consistent with its fills and the mark price per `docs/accounting/POSITION_AND_PNL.md`. | R-11, §12 |
| INV-12 | **Price sanity of trades:** every trade price equals the maker's resting price (R-5.4) and lies within the instrument's price bands. | R-5.4 |
| INV-13 | **Marketability exhaustion:** after processing completes, no resting order on one side is marketable against the other (equivalent restatement of INV-1 at order granularity — catches partially-processed matching loops). | R-5.3 |
| INV-14 | **Risk-limit compliance:** at every checkpoint, every participant satisfies `open_orders ≤ max_open_orders` and worst-case position ≤ `max_position_lots` (no accepted order can have breached R-9.3 at acceptance time). | §9 |
| INV-15 | **Reference agreement:** after every message, the optimized book's full state (levels, quantities, queue order) and cumulative event stream are identical to the reference implementation's. | all |
| INV-16 | **No look-ahead / causality** (R2+, when feeds and latency exist): no participant decision at logical time *t* depends on any event whose delivery time to that participant is > *t*; a consumer's reconstructed book at `md_seq = n` equals the engine book as of `md_seq = n`. | R-13, latency spec |
| INV-17 | **Session-end cleanliness:** after CLOSED transition, the book is empty, every order is terminal, and INV-11 reconciliation holds with the R-12.4 mark price. | §12 |

## Notes for implementers and test writers

- INV-15 (differential testing) is the highest-leverage check: it turns "the optimized book is
  correct" into "two independent implementations agree on millions of generated scenarios."
  The reference implementation must therefore be written for obviousness, never optimized, and
  reviewed against EXCHANGE_RULES.md line by line.
- INV-10 is tested at three strengths: (1) two in-process runs, (2) two separate process
  invocations, (3) replay from a recorded event log. All must be byte-identical.
- INV-3's `queue_token` exists precisely so FIFO is *testable* after modifies; it is engine
  state, never exposed on the public feed.
- INV-11(b) fails loudly if fees are ever rounded, double-applied, or dropped — this is why
  fees are exact integers (R-11.1).
- Continuous checking is O(book) per message in test builds; property tests run with checks at
  every message, differential tests may batch to every N messages for throughput, but any
  failure re-runs with per-message checking to localize the offending message.
