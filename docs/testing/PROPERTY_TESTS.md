# Property Tests

Every ENGINE_INVARIANTS.md entry as an executable property, driven by an in-repo scenario
generator (decision S3: custom deterministic generators over RapidCheck).

## The generator

`ScenarioGen(seed, profile)` produces a stream of inbound messages via a weighted state-aware
grammar:

- **Actions:** new limit (marketable/non-marketable steered by profile), new market, cancel
  (existing own / random id / already-terminal), modify (price up/down, qty up/down,
  qty→≤filled, no-op values), duplicate client_order_id, malformed enum, out-of-band price,
  session-end injection.
- **State-aware:** the generator tracks live order ids so "cancel existing" is generatable on
  purpose (uniform random ids alone almost never hit a live order — the classic naive-fuzzing
  gap); profiles weight toward interesting mixes.
- **Profiles:** `uniform`, `cancel_heavy` (realistic), `crossing_heavy` (deep matching),
  `adversarial` (duplicates/invalid/boundary values), `boundary` (prices pinned to band
  edges, qtys at max, near-overflow notionals), `empty_book` (repeatedly drains), `gap_book`
  (price gaps then markets through them).
- Deterministic: (seed, profile, N) fully determines the stream. Failures print the triple;
  shrinking per TEST_STRATEGY.md; shrunk cases graduate to committed fixtures.

## Property suite (invariant → check)

Each runs the engine (Fast and Reference) over generated streams, asserting after every
message (`assert_invariants_if_test_build` hook):

| Property | Assertion sketch |
|---|---|
| prop_INV_1_no_cross | both sides non-empty ⇒ best_bid < best_ask |
| prop_INV_2_ordering | walk levels: strictly monotone prices, no empty level objects |
| prop_INV_3_fifo | per level: queue_tokens strictly increasing front→back |
| prop_INV_4_no_overfill | per order: 0 ≤ filled ≤ total; remaining consistent |
| prop_INV_5_trade_symmetry | per trade: one maker + one taker, buy lots = sell lots; running aggregates equal |
| prop_INV_6_conservation | per order: total = filled + canceled + resting; Σ resting = book depth |
| prop_INV_7_absorbing | shadow registry: no event references an order after its terminal event |
| prop_INV_8_idempotent_dupes | duplicate client ids / re-cancels: exactly one live effect ever |
| prop_INV_9_monotonic_seq | seq, seq_out, md_seq, order_id, trade_id strictly increasing; md_seq gap-free |
| prop_INV_10_determinism | run twice (fresh engines): byte-compare full event streams |
| prop_INV_11_reconciliation | Σ position = 0; Σ cash + venue_take = 0; per-participant identity (see accounting doc) |
| prop_INV_12_trade_price_sanity | every trade px = maker's resting px ∧ within bands |
| prop_INV_13_exhaustion | no marketable pair remains post-message |
| prop_INV_14_risk_compliance | recompute worst-case exposure from scratch; ≤ limits for all |
| prop_INV_15_differential | FastBook state dump ≡ ReferenceBook state dump; event streams identical |
| prop_INV_16_no_lookahead [R2+] | consumer book at md_seq n ≡ engine book at n; delivery times ≥ creation + base latency; per-leg FIFO |
| prop_INV_17_session_end | after CLOSED: empty book, all orders terminal, INV-11 with mark price |

Scenario-generation requirements from the task spec, mapped: random submissions/cancels/
modifies (all profiles), invalid messages (`adversarial`), duplicates (`adversarial`), empty
books (`empty_book`), large price gaps (`gap_book`), full/partial fills (`crossing_heavy`),
extreme quantities (`boundary`), sequence gaps (fault-injection variant of the MD consumer
tests — the engine itself cannot produce them, INV-9).

## Budgets

CI per-PR: 4 profiles × 3 seeds × 10⁴ messages (seconds). Nightly: all profiles × 100 seeds ×
10⁶ messages + differential every-message checking. Both books, ASan+UBSan on.
