# Risk Engine

Two distinct layers, deliberately separated because they exist in reality at different places:

1. **Exchange-side pre-trade risk** (part of the engine pipeline, §9 of EXCHANGE_RULES.md) —
   what the venue enforces on everyone.
2. **Strategy-side risk controls** (part of the strategy engine) — what a trading firm
   enforces on itself.

Both are deterministic: same state + same message ⇒ same outcome, always.

## Layer 1 — Exchange-side pre-trade checks (MVP)

Configured per participant at registration; checked at acceptance in the fixed order of R-9
(after R-3.3 validation; first failure wins; one reject, one reason):

| Check | Config | Reject | Rule |
|---|---|---|---|
| Open-order count | `max_open_orders` | `MAX_OPEN_ORDERS` | R-9.1 |
| Order size | `max_order_qty_lots` (≤ instrument's) | `RISK_ORDER_TOO_LARGE` | R-9.2 |
| Worst-case position | `max_position_lots` | `MAX_POSITION` | R-9.3 |

Design notes:
- **Worst-case position** counts same-side open quantity as if fully filled (R-9.3) — the
  exchange cannot know fill probabilities, so it assumes the worst. This deliberately makes
  the *strategy's* job harder (its own open orders consume limit headroom), which is realistic.
- Notional-exposure caps (`max_notional`) are a documented R2 extension of the same check
  shape (`Notional` of worst-case position vs cap); excluded from MVP to keep the check list
  short, included in the config schema as reserved fields from day one.
- State updates are atomic with message processing (R-9.4): risk state is simply a read of
  accounting + registry state, so there is no separate risk bookkeeping to drift out of sync.
- Modifies are delta-checked (R-9.5): a reject leaves the original order untouched.

## Layer 2 — Strategy-side controls (Release 3, spec'd now)

Every strategy runs inside a `StrategyRiskHarness` that intercepts its intents and its market
view. The harness is engine-agnostic (lives in `strategy` module) and enforces, in order:

1. **Loss limit:** if `total_pnl ≤ −loss_limit` → KILL.
2. **Drawdown limit:** if `max_drawdown ≥ drawdown_limit` → KILL.
3. **Stale-market protection:** if `now − last_md_update > staleness_limit` → PASSIVE
   (cancel all resting quotes, place nothing new, keep position) until data resumes. A market
   maker quoting into a feed it hasn't heard from is the classic self-inflicted wound.
4. **Position guard:** strategy-level `max_position_lots` (≤ its exchange limit, so strategy
   hits its own guard first and the exchange reject path stays exceptional).
5. **Order-rate guard:** max intents per simulated second (runaway-loop protection;
   deterministic token bucket on logical time).
6. **Price-sanity guard:** intents priced further than `max_ticks_from_mid` from the current
   view's mid are suppressed and counted (catches sign errors and unit bugs — the
   strategy-side equivalent of fat-finger protection).

**KILL semantics (kill switch):** cancel all resting orders, then flatten the position with
marketable orders in `flatten_clip_lots` slices (bounded impact), then halt — no further
intents for the session. All transitions (`ACTIVE → PASSIVE → ACTIVE`, `* → KILLED`) are
events in the run log, so experiments can count and condition on them.

Rejected-order behavior: any exchange reject to a strategy increments a counter; `N` rejects
within a window (config) → KILL. A strategy that keeps getting rejected has a bug or a stale
view of its own limits; either way, stop.

## Determinism and testing

- All checks are pure functions of (config, accounting state, registry state, logical time) —
  property tests replay identical runs and assert identical reject/kill sequences.
- Unit tests: boundary cases for each check (exactly at limit = pass; one lot/order/tick over
  = fail), modify deltas, flip-side position math with open orders on both sides (the
  worst-case formula counts only the same-side increase — test pins this).
- Scenario tests: scripted market crash triggers loss-limit KILL; feed-gap injection triggers
  PASSIVE and recovery; kill-switch flatten leaves position exactly 0 and is itself
  risk-checked (flatten orders still respect `max_order_qty_lots`).
- Metrics: rejects by reason, kills by cause, time-in-PASSIVE are first-class experiment
  outputs (`docs/research/METRICS.md`) — RQ1 predicts stale-market PASSIVE time rises with
  latency.
