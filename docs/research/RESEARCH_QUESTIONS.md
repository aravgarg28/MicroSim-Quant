# Research Questions

Three questions, pre-registered here before any experiment runs. Each states: hypothesis,
variables, controls, design, metrics, statistical tests, confounders, repetitions, and —
critically — what conclusions are and are not justified. Findings that contradict the
hypotheses are reported exactly as findings that confirm them.

**Scope disclaimer applying to all three:** conclusions are statements about MicroSim's
simulated market under the named scenario configs. They demonstrate methodology and mechanism,
not live-market alpha. (Decision D9; see also PROJECT_VISION non-goals.)

---

## RQ1 — How does latency degrade market-maker performance?

**Hypothesis (H1):** Increasing a market maker's round-trip latency monotonically (a) worsens
its markout (more negative post-fill mid drift), (b) raises its cancel-race loss rate
(`TOO_LATE_TO_CANCEL` frequency), and (c) lowers net P&L; effects accelerate when latency
crosses the scale of the market's quote-update tempo.

- **Independent variable:** the subject MM's latency tier — `oe = md = base` swept over
  {5µs, 50µs, 100µs, 500µs, 1ms, 5ms, 10ms} (constant, no jitter, for the headline; jitter
  and tail-spike arms in the R4 extension).
- **Dependent variables:** markout curves at Δ ∈ {1ms, 10ms, 100ms, 1s}; net P&L; fill rate;
  `TOO_LATE_TO_CANCEL` rate; time-in-PASSIVE; inventory variance.
- **Controls:** identical flow realization across latency arms (common random numbers — same
  `flow/*` streams); S2 strategy with parameters fixed at the RQ2-optimal point; `active`
  scenario frozen by config hash; all other participants' latency fixed at `colo`.
- **Design:** paired within-seed comparison across the latency grid; `n_seeds = 200`
  independent seeds × 7 latency arms (1,400 runs, embarrassingly parallel), each a full
  session of configured length (target: ≥ 5×10⁵ flow events per run, set so that per-seed
  metric variance stabilizes — pilot study fixes the final length).
- **Statistical tests:** per-arm means with bootstrap 95% CIs (10⁴ resamples over seeds);
  paired differences vs the 5µs baseline (Wilcoxon signed-rank as primary — no normality
  assumption; paired t as sensitivity check); trend across arms (Page's trend test or
  Jonckheere–Terpstra); Holm correction across the dependent-variable family.
- **Confounders & handling:** (1) latency changes fill *count*, not just quality — report
  per-fill and per-session metrics separately; (2) the MM's own behavior changes the market it
  measures (reflexivity) — mitigated by MM being small relative to flow (share-of-volume
  reported; sensitivity arm with 2× flow rates); (3) PASSIVE/KILL episodes truncate exposure —
  report with harness limits loosened as a sensitivity arm.
- **Justified conclusion (if H1 holds):** "In this simulated market, latency degrades MM
  performance through measurable channels (markout, cancel races), with effect sizes X ± CI
  per decade of latency." Plus the mechanism evidence (race counts).
- **NOT justified:** any claim about real venue latency economics; extrapolation beyond the
  swept range; "colocation is worth $X".
- **Known validity bound (AR-3):** under stage-2 Poisson flow, adverse selection is purely
  *mechanical* (random bursts run over stale quotes) — there is no informed counterparty.
  H1's markout effects are therefore expected to be real but flat across horizons beyond the
  race timescale; the R5 replication under informed flow (stage-3 + informed trader) tests
  whether the latency effect *amplifies* with informational toxicity. The MVP finding is
  reported with this bound stated in its headline paragraph, not a footnote.

## RQ2 — When does inventory-skewed quoting beat fixed-spread quoting?

**Hypothesis (H2):** Inventory skew (S2) reduces inventory variance and max drawdown vs S1 at
equal `half_spread`; net P&L improves in trending/volatile conditions and is ~neutral in calm
ones; there is an interior optimum `k_skew` (U-shape: too little fails to control inventory,
too much donates edge).

- **Independent variables:** strategy ∈ {S1, S2(k_skew grid: 0.5, 1, 2, 4, 8)}; scenario ∈
  {calm, active, thin}; (R5 replication adds trending/informed).
- **Dependent variables:** net P&L, inventory variance, max |position|, max drawdown, spread
  capture, fill rate.
- **Controls:** common random numbers across all arms per seed; identical shared params
  (`half_spread` fixed at the value making S1 breakeven-ish in `active` — pilot-calibrated,
  then frozen); zero latency for all (isolate the inventory mechanism from RQ1's).
- **Design:** full factorial (6 strategy arms × 3 scenarios) × `n_seeds = 200`, paired within
  seed.
- **Tests:** paired Wilcoxon S2(k) vs S1 per scenario; bootstrap CIs on paired differences;
  across-k trend for the U-shape (quadratic contrast + report the full curve); Holm
  correction per scenario family.
- **Confounders:** spread choice favors one arm (handled: sensitivity sweep of `half_spread`
  ±50%); session-end flattening cost attribution (handled: report P&L with and without
  terminal mark, and with forced flatten-at-end arm); harness limits binding differently per
  arm (report bind rates).
- **Justified conclusion:** "Skew k reduces inventory risk by X% with P&L cost/benefit Y ± CI
  per scenario; the variance-reduction/P&L tradeoff curve looks like Z *under this flow
  model*."
- **NOT justified:** "inventory skew is optimal" in any general sense; any specific k as a
  universal constant; profitability claims outside the simulator.

## RQ3 — How does queue position affect fill probability and time-to-fill?

**Hypothesis (H3):** For orders at the best level, P(fill before cancel/level-clear) decreases
monotonically in depth-ahead-at-join; expected time-to-fill increases; the relationship's
shape (roughly exponential decay in depth for memoryless flow) is measurably different under
stage-3 flow than stage-2 (clustering shifts it).

- **Independent variable:** depth ahead at join (lots), *observationally* varied — probe
  orders join naturally at varying queue depths; plus a designed arm: passive probe
  participant places 1-lot orders at best bid at scheduled times, never cancels before a
  fixed horizon H.
- **Dependent variables:** fill indicator within H; time-to-fill (censored at H — survival
  analysis); fill-vs-adverse outcome (was the level swept through?).
- **Controls:** probe participant is minuscule (1 lot, low rate — measured share of volume
  < 0.5%); zero latency for the probe (join-time depth is then exactly the decision-time
  depth — the *observational* mechanical question); `active` scenario frozen.
- **Design:** ~10⁴ probe placements per run × `n_seeds = 100`; depth-ahead binned; both
  stage-2 and (R5) stage-3 flow.
- **Tests:** Kaplan–Meier time-to-fill curves per depth bin with log-rank tests; logistic
  regression of fill-within-H on depth (+ spread, near-side depth as covariates) with
  cluster-robust (per-seed) standard errors; goodness-of-fit of the exponential-decay shape.
- **Confounders:** probe orders add depth (excluded from own depth count; 1-lot minimizes);
  depth-at-join correlates with market state (covariates + within-state stratification);
  censoring at H (survival methods handle; H swept as sensitivity).
- **Justified conclusion:** "In this simulated market, each additional lot ahead reduces
  fill probability within H by X ± CI; time-to-fill scales as f(depth); clustering changes
  the curve by Y" — a *mechanical* result, the most defensible of the three.
- **NOT justified:** real-venue queue-value estimates ($ value of queue position on any real
  exchange); anything about strategies *using* this (that would be a new experiment).

---

## Why exactly these three (interview-ready rationale)

RQ3 is purely mechanical (no contested assumptions — strongest internal validity). RQ2 is a
controlled strategy A/B (methodology showcase: CRN, paired tests, pre-registration). RQ1 ties
the systems work (latency lab) to economic outcomes (the project's thesis in one experiment).
The dropped imbalance-prediction question (D9) is the honesty exhibit: in a simulator, flow
assumptions can bake in the answer — pre-registering *that* refusal is itself a credential.
