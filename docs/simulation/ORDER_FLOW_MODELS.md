# Order-Flow Models

Staged models for generating the market's background activity. Stage numbers are release-
mapped; each model states parameters, calibration, assumptions, limitations, validation, and
intended use. The honest framing used throughout the research docs: **order flow is the
simulation's biggest assumption**, so every research result gets a sensitivity analysis across
flow parameters, and no result is claimed beyond the flow model that produced it.

## Stage 1 — Scripted deterministic scenarios [R1]

- **What:** hand-written event sequences (YAML/JSON: time, participant, action) replayed
  exactly.
- **Parameters:** the script itself.
- **Use:** unit/property test fixtures, worked examples (the EXCHANGE_RULES.md example is a
  script), demo replays, regression corpora from fuzz findings.
- **Limitations:** not a market model at all — by design.
- **Validation:** none needed (it *is* the expected output).

## Stage 2 — Independent Poisson flow [R2, MVP research model]

The zero-intelligence baseline (à la Smith–Farmer-style zero-intelligence markets):

- **Arrivals:** independent Poisson processes: limit buys, limit sells, market buys, market
  sells, cancellations, with rates `λ_lb, λ_ls, λ_mb, λ_ms, λ_c`.
- **Limit-order placement:** price offset from the *current same-side best* drawn from a
  geometric distribution over ticks (depth parameter `p_geo`), quantity from a discrete
  distribution (config: point mass / geometric over lots, `q_dist`).
- **Cancellations:** target chosen uniformly among the generator's own resting orders
  (memoryless lifetimes).
- **Reference anchor:** placement anchors to current book state; if a side is empty, anchor
  to `initial_reference_price`.
- **Parameters (full list):** the five λs, `p_geo`, `q_dist`, `initial_reference_price`,
  initial book seeding recipe (levels × qty to pre-populate at t=0).
- **Calibration:** chosen for internal plausibility (spread a few ticks, depth realistic
  relative to trade size, cancel-to-trade ratio ~5–15×), not fitted to real data — stated
  openly. A `configs/flows/` directory pins named parameter sets (`calm`, `active`, `thin`)
  used across all experiments.
- **Assumptions/limitations:** no feedback (rates ignore book state!), no clustering, no
  informed flow, no fat tails; mid-price is a martingale-ish random walk with microstructure
  noise. Consequence: adverse selection exists only mechanically (quotes get run over by
  random bursts), not informationally.
- **Validation metrics (tested statistically in CI, wide tolerances):** empirical arrival
  rates ≈ λ (chi-square / exact binomial on counts); inter-arrival KS test vs exponential;
  spread/depth distributions within configured plausibility bands; positive cancel-to-trade
  ratio matching config intent.
- **Intended use:** MVP experiments (RQ2 baseline environment), engine load generation for
  benchmarks.

## Stage 3 — State-dependent Poisson [R5]

- **What:** rates become functions of observable book state:
  `λ_i(t) = λ_i⁰ · f_i(spread_ticks, imbalance, |mid_return over τ|)`, with `f_i` simple
  parametric forms (e.g. market-order intensity increasing in imbalance toward that side;
  limit-order intensity increasing in spread; cancellation intensity increasing in adverse
  book moves).
- **Extra parameters:** sensitivity coefficients per factor per event type (kept few: ~6–10
  total), factor definitions fixed in code.
- **Calibration:** still plausibility-driven; sensitivities swept in experiments.
- **What it adds:** feedback loops — liquidity replenishes after depletion, spreads mean-
  revert, momentum-ish bursts. What it still lacks: true information, cross-time clustering
  beyond state response.
- **Validation:** conditional-rate tests (measured λ in high- vs low-imbalance states differs
  in configured direction); stationarity sanity (no rate explosion — bounded `f_i`).
- **Use:** robustness re-runs of RQ1/RQ2 conclusions (ROADMAP R5 replication study).

## Stage 4 — Hawkes-process flow [R5 stretch]

- **What:** self- and cross-exciting arrivals: intensity
  `λ_i(t) = μ_i + Σ_j Σ_{t_k^j < t} α_ij · exp(−β_ij (t − t_k^j))` over event types
  (exponential kernels only).
- **Parameters:** baseline `μ`, excitation matrix `α`, decay `β` (restricted: shared β per
  target type to keep the matrix small); stability constraint (spectral radius of α/β < 1)
  validated at config time.
- **Simulation:** Ogata thinning with the named-stream RNG (deterministic).
- **Calibration requirement:** parameters either literature-inspired defaults (cited) or
  fitted to *MicroSim's own* stage-2/3 output as a self-consistency exercise — fitting to real
  data is out of scope (no data licensing).
- **What it adds:** clustering/burstiness (realistic cancel storms, trade cascades) — the
  feature most stressing for market makers and the latency lab.
- **Limitations:** still no strategic informed flow; exponential kernels only; calibration
  honesty is the whole game — flagged as the highest research-validity risk of stage 4.
- **Validation:** empirical intensity autocorrelation positive with configured decay;
  cluster-size distributions vs theory; stability (no runaway).
- **Use:** stress-testing MM strategies; robustness appendix. **No headline conclusion may
  rest on stage 4 alone.**

## Stage 5 — Historical event replay [out of plan]

Documented as an extension only: an adapter mapping normalized L3 event logs (e.g. LOBSTER
format) onto the gateway. Explicitly out of scope: licensing, symbol mapping, corporate
actions, and the deep methodological problem that replayed flow does not *react* to the
simulated strategy (market impact is wrong by construction). Recorded so interviews can
discuss why naive historical replay backtests of market making are unsound.

## Cross-model requirements

- All models draw exclusively from named RNG streams (`flow/*`) — common-random-number
  comparability across strategy arms (EXPERIMENT_PLAN.md).
- All models emit through the same participant interface as any agent: flow generators are
  participants with latency 0 by default (configurable), no special engine access.
- Every experiment's manifest records the flow model + full parameter set + stream seeds.
