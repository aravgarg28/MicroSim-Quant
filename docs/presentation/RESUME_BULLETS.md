# Resume Bullets

Templates with `[--]` placeholders for measured values. **Rule: a bullet may not appear on a
resume until its placeholders are filled from committed manifests/findings** (benchmark JSON,
findings docs, CI artifacts). Never round up; prefer the conservative end of a CI.

## Long-form (2–3 bullets, quant-developer resume)

- Built **MicroSim**, a deterministic C++20 limit-order-book exchange simulator: price-time
  matching engine with exact integer arithmetic, [--]-allocation steady state, processing
  [--]M messages/sec with p50 [--] ns / p99 [--] ns order-insert latency (Apple M[--],
  methodology + manifests in repo).
- Verified correctness via 17 machine-checked invariants, property-based tests over
  state-aware generated scenarios, and differential testing against an independent reference
  implementation ([--] generated events, [--] fuzz-hours, ASan/UBSan-clean, byte-identical
  seeded replay verified in CI).
- Ran [--] pre-registered market-making experiments (paired designs, common random numbers,
  bootstrap CIs over [--] seeded runs): quantified latency's cost to maker P&L via markout
  decomposition ([--] ticks/fill per latency decade, 95% CI [--]) and the inventory-skew
  risk/return tradeoff ([--]% inventory-variance reduction at [--] P&L cost).

## Short-form (1 bullet, space-constrained)

- Built a deterministic C++20 exchange simulator (price-time LOB, [--]M msgs/sec, p99 [--] ns
  inserts, differential-tested vs reference oracle) + Python research platform; [--]
  pre-registered market-making experiments with bootstrap CIs across [--] seeded runs.

## Variant emphases

- **Systems-role tilt:** lead with zero-alloc + latency percentiles + occupancy-bitmap book
  design + "threading adopted/rejected on a pre-registered measurement bar".
- **Research-role tilt:** lead with pre-registration, CRN paired design, markout adverse-
  selection decomposition, survival analysis of queue position; engine one clause.
- **One-line project list entry:** "MicroSim — deterministic C++20 exchange simulator +
  market-making research lab (differential-tested, [--]M msgs/sec, 3 pre-registered
  experiments)."

## Filling rules

| Placeholder | Source |
|---|---|
| throughput, latency percentiles, allocs | `results/benchmarks/<release>/…json` (release preset, dev Mac manifest) |
| generated events, fuzz-hours | nightly CI artifacts at the cited commit |
| experiment counts, effects, CIs | `docs/research/findings/rq*.md` headline tables |
| hardware | the manifest string, verbatim |

Interview-consistency check: every number on the resume must be re-derivable live from the
repo in under two minutes (know the path to each manifest).
