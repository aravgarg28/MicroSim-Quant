# Participant Models

Agents that populate the market. All implement the shared Participant interface
(SYSTEM_ARCHITECTURE §6): observe via delayed feed + private reports, act via order intents,
draw randomness from named streams. Strategies (fixed-spread MM, inventory MM, …) are
specified separately in `docs/strategies/STRATEGY_SPECIFICATIONS.md` — this file covers the
*environment* agents.

For each: Observations · State · Decision rule · Order behavior · Risk limits · Parameters ·
Evaluation metrics.

## 1. Noise liquidity provider [R2]

The stage-2 Poisson flow generator's limit-order half, packaged as an agent.

- **Observes:** own resting orders; current book view (for placement anchoring only).
- **State:** list of own open orders.
- **Decision rule:** on each exponential-timer wakeup (stream `flow/lp`): with configured
  probabilities place a new limit order (side ~ Bernoulli(0.5), price = same-side best ±
  geometric offset, qty ~ q_dist) or cancel a uniformly-chosen own resting order.
- **Risk limits:** `max_open_orders` high (it *is* the book's depth); no position limit —
  position is intentionally unbounded noise (documented; its P&L is not a research metric).
- **Parameters:** λ_place, λ_cancel, p_geo, q_dist, side bias.
- **Evaluation:** produces configured book-shape statistics (validation metrics of
  ORDER_FLOW_MODELS stage 2).

## 2. Noise liquidity taker [R2]

- **Observes:** nothing but its timer (pure noise).
- **Decision rule:** exponential-timer wakeups (stream `flow/taker`); submit MARKET order,
  side Bernoulli (configurable bias for directional-drift scenarios), qty ~ q_dist.
- **Risk limits:** none beyond exchange-side (its purpose is to hit quotes).
- **Parameters:** λ_take, side bias, q_dist.
- **Evaluation:** realized trade-arrival rate; interacts with provider to give the configured
  volatility level (measured, not asserted).

## 3. Informed trader [R5]

The agent that creates *informational* adverse selection.

- **Mechanism:** a latent "true value" process V(t) (random walk with occasional jumps, stream
  `flow/value`) that only informed traders observe. The exchange and other agents never see V.
- **Observes:** V(t) and the public book.
- **Decision rule:** when `|V − mid| > threshold_ticks`, submit marketable orders toward V
  (qty scaled by edge, capped), with probability/intensity parameter controlling
  aggressiveness; otherwise idle.
- **State:** none beyond parameters (memoryless w.r.t. own trades in v1).
- **Risk limits:** max position (it accumulates inventory by design); session flatten.
- **Parameters:** V-process (σ, jump intensity/size), threshold, intensity, qty scaling, cap.
- **Evaluation:** markouts of its fills should be *positive* (it wins) and MM markouts against
  it negative — this is the validation that "real" adverse selection now exists (used by the
  R5 replication of RQ1).

## 4. Momentum trader [R5]

- **Observes:** rolling mid-price return over window τ.
- **Decision rule:** if return over τ exceeds +k ticks → market buy (and symmetric down);
  cooldown between trades.
- **Parameters:** τ, k, qty, cooldown, intensity.
- **Evaluation:** amplifies trends (measured autocorrelation of mid returns increases when
  enabled — validation check); stresses inventory-skew strategies.

## 5. Mean-reversion trader [R5]

- Mirror of 4: fades moves beyond k ticks (limit orders inside the move's far side or
  marketable against it, config choice).
- **Evaluation:** dampens trends (return autocorrelation decreases); provides liquidity in
  bursts.

## Composition and scenarios

A simulation config lists agent instances with parameters and latency tiers. Named
compositions live in `configs/scenarios/`:

| Scenario | Composition | Used by |
|---|---|---|
| `calm` | 1 LP + 1 taker, low rates | RQ2/RQ3 base |
| `active` | LP + taker, high rates | RQ1 base, benchmarks |
| `thin` | LP with low replenishment | sensitivity arms |
| `informed` [R5] | active + informed trader | RQ1 replication |
| `trending` [R5] | active + momentum | RQ2 replication |

## What agent P&L means (and doesn't)

Noise agents lose money to fees and to anyone systematic — expected and fine. Only strategy
participants' metrics are research outputs; agent P&L appears solely in conservation checks
(INV-11: someone must hold the other side of MM profits). This prevents the classic
simulation sin: tuning the environment until the strategy wins, then reporting the win as if
the environment were exogenous. Environment parameters are fixed per named scenario *before*
strategy experiments run, and scenario files are frozen by config hash in every result
manifest.
