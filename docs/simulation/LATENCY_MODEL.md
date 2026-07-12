# Latency Model

Latency as a first-class, deterministic, configurable experimental variable. MVP ships
constant delays; Release 4 completes the laboratory.

## The three legs (per participant)

```
participant ──(order-entry latency)──▶ exchange ──(processing delay)──▶ matched
     ▲                                                        │
     └──(market-data latency / private-report latency)◀───────┘
```

| Leg | Config key | Meaning |
|---|---|---|
| Order entry | `oe` | intent creation → gateway arrival (includes the participant's own send stack) |
| Market data | `md` | publication → participant's consumer applies it |
| Private reports | `pr` | acks/fills/rejects back to owner (defaults to `md` tier; separable because real order-entry sessions and feeds differ) |
| Exchange processing | `proc` | gateway arrival → matching (venue-side queueing/processing; one global config, default 0 in MVP) |

Cancels and modifies use the `oe` leg (no separate cancel latency by default; a
`cancel_extra` additive term exists for experiments that want asymmetric cancel cost).

## Delay composition [R4 full model]

Each leg's delay per message:

```
delay = base + jitter + spike
  base:   constant ns (tier table below)
  jitter: draw from {none | uniform(0,J) | exponential(mean J) | lognormal(µ,σ) capped at Jmax}
  spike:  with prob p_spike per message, add draw from Pareto(x_m, α) capped at S_max
```

- All draws from named streams (`latency/<participant>/<leg>`) — deterministic (INV-10), and
  independent across participants/legs by stream construction.
- MVP config = base only (jitter/spike zero). RQ1's MVP slice sweeps `base`; R4 adds the rest.
- **Ordering effects are emergent, not modeled:** delays may reorder deliveries between
  participants (that's the point); within one (participant, leg) FIFO is enforced — messages
  may not overtake on the same leg: `deliver_time = max(computed, last_deliver_time_on_leg + 1ns)`.
  Without this, a jittered feed could apply MD batch n+1 before n, which real sessions
  (TCP/sequenced feeds) don't do; gap machinery is for snapshots, not steady-state reordering.

## Latency tiers

Named tiers in config, assigned per participant:

| Tier | oe base | md base | intent |
|---|---|---|---|
| `colo` | 5 µs | 5 µs | co-located firm |
| `metro` | 100 µs | 100 µs | same-city |
| `retail` | 5 ms | 10 ms | consolidated-feed retail |
| `custom` | any | any | sweeps |

(Values are config defaults with realistic orders of magnitude, not claims about any venue.)

## What the model captures vs. not

**Captures:** relative speed advantages; stale-view decision-making; cancel/replace races
(DATA_FLOW diagram 2); queue-position loss from slow placement; tail events breaking an
otherwise-fast participant; feed/order-path asymmetry.

**Does not capture (stated for research honesty):** bandwidth/congestion coupling between
participants (delays are independent draws, real congestion correlates them); venue matching-
engine queueing dynamics under load (proc is a constant, not a load function); packet loss
and retransmission; clock skew between participant and venue (all read the one logical
clock — participants have *delayed knowledge*, not *wrong clocks*); intra-participant compute
time unless explicitly configured as `decision_latency`.

## Determinism and tests

- Property: identical seeds ⇒ identical delivery schedule (INV-10); per-leg FIFO invariant
  (no overtaking); causality (INV-16: nothing delivered before creation + base).
- Unit: composition arithmetic, caps, tier table parsing; golden-value draws per distribution
  (cross-platform).
- Scenario: the RQ1 race fixture — slow-MM cancel vs fast-taker market order — asserts the
  exact interleaving from DATA_FLOW.md.
- Statistical: empirical delay distributions match configured ones (KS, wide tolerance) over
  large N.

## Interaction with queue-position research (RQ3)

Latency moves *when* an order joins the queue, and queue position at join determines fill
odds. The R4 queue tracker records for every resting order: join time, initial depth ahead
(lots), depth-ahead trajectory, and outcome (filled/canceled, time-to-outcome). RQ3 analyzes
tracker output; RQ1 uses tiers to shift join times. Both use the same apparatus — built once,
in `metrics`.
