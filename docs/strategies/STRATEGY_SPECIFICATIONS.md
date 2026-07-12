# Strategy Specifications

Market-making strategies, specified to be implementable without invention. All run inside the
`StrategyRiskHarness` (RISK_ENGINE.md layer 2), observe only their delayed feed view, and
share a common **quote-management core** so that A/B differences are *only* the quoting rule.

## Shared quote-management core (`QuoteEngine`)

Every MM strategy computes a *target quote pair* `{bid_px, bid_qty, ask_px, ask_qty}` (either
side may be "none") on each decision event; the QuoteEngine reconciles reality to target:

- If a live quote's price differs from target → modify (price change = lose priority,
  accepted) or cancel+new when qty also changes; if only qty shrinks → in-place reduce
  (keeps priority, R-7.2); if qty grows → full replace (priority lost anyway).
- **Re-quote threshold:** targets within `requote_tolerance_ticks` of live quotes are left
  alone (avoids cancel churn; tolerance 0 = always chase).
- One live quote per side, always (MVP simplification; laddered quoting is an extension).
- Decision events: every own-relevant feed update (batch-end), private report, or timer at
  `decision_interval` — whichever the strategy subscribes to; all strategies here use
  batch-end + timer.

Shared parameters: `quote_qty` (lots per side), `requote_tolerance_ticks`,
`decision_interval`, harness risk params (loss/drawdown limits, staleness, position guard,
rate guard, price-sanity band).

Failure modes shared by all: quoting through a stale view (mitigated by staleness → PASSIVE);
cancel-churn feedback (mitigated by tolerance + rate guard); one-sided fill accumulation
(strategy-specific inventory handling below).

---

## S1 — Fixed-spread market maker [R3, MVP]

The baseline. Symmetric quotes around mid, indifferent to inventory.

- **Inputs:** consumer book view (mid), own fills.
- **Quoting logic:** `bid = round_down_to_tick(mid − half_spread)`,
  `ask = round_up_to_tick(mid + half_spread)`; skip a side if the book view lacks a mid
  (one-sided/empty book → PASSIVE behavior).
- **Parameters:** `half_spread_ticks`, shared set.
- **Inventory behavior:** none (that's the point of the baseline) — position drifts as a
  random walk until a harness limit stops it.
- **Cancellation behavior:** via QuoteEngine reconciliation only.
- **Benchmark comparison:** this *is* the benchmark for RQ2; its own baseline is "no
  strategy" (zero P&L, zero risk).
- **Failure modes:** inventory blow-up in trending scenarios (expected, measured — RQ2's
  whole story); adverse selection at wide spreads in quiet markets (fills only when run over).

## S2 — Inventory-skewed market maker [R3, MVP]

S1 plus linear inventory skew (the RQ2 treatment).

- **Quoting logic:** `skew_ticks = clamp(round(k_skew × position / max_position), −max_skew,
  +max_skew)`; quote `bid = mid − half_spread − skew_ticks`, `ask = mid + half_spread −
  skew_ticks` (long ⇒ both quotes shift down: buy less attractively, sell more attractively).
  Optional `qty_skew`: reduce quote qty on the position-increasing side by
  `position/max_position` fraction (config flag, default off — price skew only, to keep RQ2
  one-mechanism).
- **Parameters:** S1 set + `k_skew`, `max_skew_ticks`, `qty_skew_enabled`.
- **Inventory behavior:** mean-reverting by construction; the skew *is* the inventory model.
- **Failure modes:** over-skewing (k too high) donates spread to takers — the RQ2 parameter
  sweep exhibits the U-shape; skew chasing in trends (skew lags a persistent move).

## S3 — Volatility-adaptive market maker [R3]

S2 with spread scaled by realized volatility.

- **Inputs:** + streaming volatility estimator: EWMA of squared mid returns over
  `vol_halflife` (computed strategy-side from its own view — not a metrics feed; the strategy
  can only know what it can see).
- **Quoting logic:** `half_spread = base_half_spread × max(1, c_vol × σ̂/σ_ref)`, then S2 skew.
- **Parameters:** S2 set + `vol_halflife`, `c_vol`, `σ_ref` (reference vol from the scenario's
  calibration run), `min/max_half_spread_ticks`.
- **Benchmark comparison:** vs S2 at fixed spreads across calm/active/thin scenarios —
  demonstrates the volatility–spread relationship (secondary analysis, not a headline RQ).
- **Failure modes:** vol estimator startup transient (warm-up period quotes PASSIVE);
  vol-of-vol whipsaw (spread flapping — bounded by min/max and requote tolerance).

## S4 — Order-book-imbalance strategy [R3]

S2 with a fair-price adjustment from top-of-book imbalance.

- **Inputs:** + imbalance `I = (bid_qty − ask_qty)/(bid_qty + ask_qty)` over top `k_lvls`
  levels of its view.
- **Quoting logic:** `fair = mid + c_imb × I × tick`; quote around `fair` instead of mid, then
  S2 skew. (Microprice-style shading: lean toward the pressure side.)
- **Parameters:** S2 set + `k_lvls`, `c_imb`.
- **Benchmark comparison:** vs S2 under stage-3 (state-dependent) flow where imbalance
  actually predicts — under pure stage-2 flow the honest expected result is ~no improvement,
  and *saying so* is part of the research-integrity story (relates to dropped RQ, D9).
- **Failure modes:** reflexivity — its own quotes enter the imbalance it reads (mitigated by
  excluding own resting qty from I, which the strategy can do because it knows its own
  orders); noise-chasing at small k_lvls.

## S5 — Avellaneda–Stoikov-inspired quoter [R3 stretch]

Closed-form inventory quoting from the A–S (2008) framework, adapted to discrete ticks and
finite session.

- **Quoting logic:** reservation price `r = mid − q σ̂² γ (T−t)`; optimal total spread
  `δ = γ σ̂² (T−t) + (2/γ) ln(1 + γ/κ)`; quote `bid = r − δ/2`, `ask = r + δ/2` (rounded to
  ticks, clamped to sanity bands). `q` = current position, `κ` = fill-intensity decay
  estimated from the scenario calibration run (documented procedure: fit exponential to
  fill-rate-vs-depth from a calibration simulation).
- **Parameters:** γ (risk aversion), κ (or its calibration recipe), σ̂ estimator (as S3),
  T (session length), sanity clamps.
- **Benchmark comparison:** vs S2 — the research question is whether the "optimal" structure
  beats a well-tuned linear skew *in this simulator*; either answer is honest and interesting.
- **Failure modes:** parameter sensitivity (γ, κ orders of magnitude matter — sweep
  documented); end-of-session spread collapse (δ→small as T−t→0 is by design but interacts
  with fees; clamps bound it); the model's assumptions (continuous prices, exponential fill
  intensity) visibly break at 1-tick spreads — a documented discussion point, and the reason
  this is a stretch goal rather than a headline strategy.

---

## Evaluation protocol (all strategies)

Common metrics (METRICS.md): total/realized P&L net of fees, inventory mean/variance/max,
max drawdown, fill rate, spread capture, markout curves (1ms/10ms/100ms/1s), order-to-trade
ratio, cancel rate, time in PASSIVE/KILLED. Every strategy comparison runs A/B on **identical
flow realizations** (shared `flow` streams, per EXPERIMENT_PLAN.md) with ≥ `n_seeds`
independent seeds, reporting paired statistics.
