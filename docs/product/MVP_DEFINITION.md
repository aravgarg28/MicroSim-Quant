# MicroSim — MVP Definition

The MVP is the smallest version that is *credible to a trading-firm engineer*: a provably
correct single-instrument exchange, honest benchmarks, and one rigorous research result. It
corresponds to Releases 0–3 of the [roadmap](ROADMAP.md) at reduced scope; anything not listed
under "In scope" is excluded until a later release.

Every feature below survived the test: **"If this were missing, would the project stop being
credible, or would a research question become unanswerable?"** Features that failed that test
are listed under Exclusions with the reason.

## In scope

### Exchange core
| Feature | Why it must be in the MVP |
|---|---|
| One instrument, one order book | Decision D5. Everything else composes on top of a correct book. |
| Price-time (FIFO) priority | The queue-position research question (RQ3) is meaningless without it. |
| Limit orders | The book cannot exist without them. |
| Market orders | Needed for liquidity-taking flow and adverse-selection measurement (RQ1). |
| Cancellations | Market making is quote management; cancels dominate real message flow. |
| Modifications (cancel/replace semantics, priority loss on price change or size increase) | Needed for realistic quoting and for queue-priority rules to be testable. |
| Partial fills | Excluding them would falsify fill statistics in all three research questions. |
| Tick size & lot size validation | Cheap; without it "price" and "quantity" are ill-defined. |
| Maker-taker fees & rebates | Decision D6. Market-making P&L without fees is misleading. |
| Deterministic sequencing & timestamps | Determinism is a core success criterion. |
| Reject handling for invalid/duplicate messages | Required by the invariants; also the fuzzing surface. |

### Risk & accounting
| Feature | Why |
|---|---|
| Pre-trade risk checks: max position, max order size, max open orders | Decision D6; small effort, high credibility. |
| Per-participant position, cash, realized/unrealized P&L, fees paid/received | RQ1 and RQ2 are measured in these units. Reconciliation invariants (buy qty = sell qty, cash conservation) are property-tested. |

### Simulation
| Feature | Why |
|---|---|
| Discrete-event simulation clock (logical time) | Foundation for determinism and latency modeling. |
| Scripted deterministic scenarios | Drive unit/property tests and worked examples. |
| Seeded Poisson order-flow generator | Decision D7 — simplest defensible stochastic flow. |
| Noise liquidity-provider and liquidity-taker agents | Populate the book so market makers have someone to trade with. |
| Basic latency configuration: per-participant constant market-data delay and order-entry delay | RQ1 needs latency as an independent variable. Jitter and tail events arrive in Release 4. |
| Deterministic replay from seed + config | Core success criterion. |

### Strategies
| Feature | Why |
|---|---|
| Fixed-spread market maker | Baseline for RQ1 and RQ2. |
| Inventory-skewed market maker | Treatment arm for RQ2. |

### Market data
| Feature | Why |
|---|---|
| In-process incremental book updates (add/modify/delete level or order) + trade messages, with sequence numbers | Strategies must observe the market through a feed (with latency), not by reading engine internals — otherwise experiments have look-ahead. |
| Book snapshots | Needed for consumer initialization and gap recovery testing. |

### Research & Python
| Feature | Why |
|---|---|
| pybind11 module: configure simulation, run, retrieve event/metric tables | Research workflow is Python (owner's and industry's). |
| Experiment results to Parquet with run metadata (seed, config hash, git hash) | Reproducibility criterion. |
| **One** rigorous research experiment completed end-to-end (RQ2: inventory-aware vs fixed-spread) | Proves the whole pipeline; RQ2 is chosen because it needs no latency lab. RQ1/RQ3 complete in Release 4. |

### Quality
| Feature | Why |
|---|---|
| Unit tests (GoogleTest) for every rule in EXCHANGE_RULES.md | Spec-driven testing is the project's signature. |
| Property tests: generated scenarios checked against all engine invariants | Invariants doc exists to drive these. |
| Differential tests vs. slow reference order book | Highest-leverage correctness tool for an optimized engine. |
| ASan/UBSan clean; libFuzzer target for the order gateway | Table stakes for C++ credibility. |
| Google Benchmark suite for insert/cancel/modify/match with documented methodology | "High-performance" claims require measurements. |
| GitHub Actions CI: build + tests + sanitizers on Linux, build + tests on macOS | Green CI badge is the first thing reviewers see. |

## Exclusions (challenged out of the MVP)

| Excluded feature | Reason | Returns in |
|---|---|---|
| Self-trade prevention | Realistic but no MVP experiment produces self-trades that matter; adds matching-rule surface | Release 2 |
| Opening/closing auctions, trading halts, sessions | Large spec surface, zero research payoff for RQ1–RQ3 | Deferred indefinitely |
| Latency jitter, tail spikes, latency tiers, cancel races | RQ1's MVP slice uses constant latency; full lab is Release 4 | Release 4 |
| State-dependent / Hawkes order flow, informed/momentum/mean-reversion agents | Decision D7; Poisson + noise agents suffice for MVP experiments | Release 5 |
| Historical data replay | Data licensing/format work crowds out the engine | Out of plan |
| Multi-instrument | Decision D5 | Release 5 |
| Volatility-adaptive, imbalance, Avellaneda–Stoikov strategies | Decision D8 | Release 3 |
| Multithreading | Correctness and determinism first; threads only where measurement justifies | Release 5 (evaluation) |
| Web dashboard, FastAPI service | Decision D13 — strictly last | Release 6 |
| Network protocols (UDP multicast-style feed, binary wire format) | In-process message structs are sufficient for all research; wire format is resume garnish | Release 6 (optional) |
| ML/RL strategies | No deterministic baseline exists yet; against operating rules | Post-plan extension at most |

## MVP acceptance checklist

The MVP is done when:

1. All EXCHANGE_RULES.md rules have at least one passing test each.
2. All ENGINE_INVARIANTS.md invariants hold over ≥1M generated events (property + differential
   tests), sanitizer-clean.
3. Two identical seeded runs produce byte-identical event logs.
4. Benchmark suite runs on the disclosed Mac hardware and results are recorded in the README
   per the benchmark methodology.
5. The RQ2 experiment (inventory-skew vs fixed-spread) runs from a Python notebook/CLI,
   produces Parquet results with confidence intervals, and its writeup states limitations.
6. A clean clone builds and reproduces the RQ2 experiment with documented commands.
