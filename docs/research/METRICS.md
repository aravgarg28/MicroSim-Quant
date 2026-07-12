# Metrics

Precise definitions of every research metric. Computed by the C++ `metrics` collectors
(streaming, allocation-free steady state) and cross-validated by independent pandas
recomputation in Python tests. Notation: fills indexed i with side sᵢ (+1 buy), price pᵢ,
qty qᵢ; mid(t) from the *experimenter's* zero-latency view (metrics see truth; participants
don't).

## Execution quality

| Metric | Definition |
|---|---|
| **Fill rate** | filled resting orders / posted resting orders (count and lot-weighted variants); takers: filled qty / submitted qty |
| **Spread captured** (per round trip) | for matched buy-sell lot pairs (FIFO pairing): `(sell_px − buy_px)` in ticks; session aggregate = 2×(Σᵢ sᵢ·(mid(tᵢ) − pᵢ)·qᵢ)/Σqᵢ — average edge received vs mid at fill |
| **Markout / adverse selection** at Δ | `AS(Δ) = E[ sᵢ·(mid(tᵢ+Δ) − pᵢ) ]` per lot, Δ ∈ {1ms, 10ms, 100ms, 1s}; negative = fills are toxic. Reported as a curve; "adverse-selection cost" = −AS(Δ*) at the pre-registered horizon Δ*=100ms |
| **Slippage** (takers) | `sᵢ·(vwap_fill − mid(t_decision))` per lot |
| **Queue waiting time** | join → first fill (censored at cancel/session end; survival-reported) |
| **Time-to-fill** | RQ3 primary; Kaplan–Meier |

## Inventory and P&L

| Metric | Definition |
|---|---|
| **Net P&L** | `equity` at session end (POSITION_AND_PNL.md), fees included |
| **Realized / unrealized P&L** | per accounting doc; both at session end and as time series sampled at `metric_interval` |
| **Inventory mean / variance / max** | over the position time series (time-weighted) |
| **Max drawdown** | max over t of (peak equity − equity), fees included |
| **Sharpe-like ratio** | mean/std of per-interval equity changes × √(intervals per session). **Caveats attached wherever reported:** single simulated instrument, no funding/capital model, interval choice matters, and cross-session i.i.d. is by construction (seeds) — the number is comparable *between arms*, meaningless as an absolute. |
| **Market impact** (takers) | mid(t+Δ) − mid(t⁻) signed by trade direction, per lot bucket |

## Market/operational hygiene

| Metric | Definition |
|---|---|
| **Order-to-trade ratio** | messages sent (new+cancel+modify) / fills received |
| **Cancel rate** | cancels / new orders |
| **Race losses** | `TOO_LATE_TO_CANCEL` rejects per session (RQ1) |
| **Time in PASSIVE / KILLED** | harness state durations |
| **Reject counts by reason** | full R-14 histogram |
| **Share of volume** | subject fills / total market volume (reflexivity guard, RQ1/RQ3) |

## Latency-adjusted performance

Any metric above conditioned on the subject's latency tier (RQ1's tables are exactly this);
plus **decision staleness**: age of the view (now − ts_event of last applied batch) sampled
at decision times — the mechanism variable linking latency to outcomes.

## Market-state features (context variables, also streamed)

Spread (time-weighted mean), depth at k levels, imbalance, realized volatility (per-interval
std of mid returns), trade rate, mid drift. Used as covariates (RQ3 regression) and for
scenario-validation checks (ORDER_FLOW_MODELS.md).

## Reporting rules

1. Every mean carries a bootstrap 95% CI over seeds (never over within-run samples alone —
   runs are the independent unit).
2. Units: ticks and lots preferred; currency only with the instrument's scale stated.
3. Curves (markout, K–M) plotted with per-point CIs; tables state n_seeds and n_fills.
4. No metric is reported for arms whose share-of-volume guard exceeded threshold without a
   flag on the table.
5. Confidence intervals that include zero are *shown*, not dropped — absence of effect is a
   result.
