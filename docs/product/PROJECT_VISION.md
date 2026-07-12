# MicroSim — Project Vision

## One-sentence description

MicroSim is a deterministic, high-performance simulated electronic exchange and
market-microstructure research platform: a C++20 price-time-priority matching engine with a
configurable latency laboratory, agent-based order flow, and a Python research interface for
running reproducible market-making experiments.

## Target roles

Built as a portfolio project for new-grad roles at trading firms such as Citadel Securities,
Jane Street, Optiver, IMC, Hudson River Trading, DRW, SIG, and Jump Trading:

1. **Quantitative developer** (primary)
2. **Trading-systems / low-latency software engineer** (primary)
3. **Quantitative research engineer** (secondary)

The emphasis is roughly 60% systems engineering, 40% quantitative research (decision D1).

## Core technical value

What the project demonstrates on the engineering side:

- **Correct-by-construction exchange semantics.** Every matching rule is specified before
  implementation (`docs/domain/EXCHANGE_RULES.md`), and a set of engine invariants
  (`docs/domain/ENGINE_INVARIANTS.md`) is enforced by property-based tests and differential
  testing against a deliberately slow reference implementation.
- **Determinism as a design constraint.** Same configuration + same seed ⇒ byte-identical event
  stream, fills, and P&L. Replay is a first-class feature, not a debugging afterthought.
- **Data structures and memory under measurement.** Order-book layouts, price-level storage,
  allocation behavior, and cache effects are chosen via documented tradeoffs and validated with
  Google Benchmark on disclosed hardware — never asserted.
- **A staged concurrency story.** The engine is single-threaded and deterministic first;
  multithreading is introduced only where measurements justify it, with ThreadSanitizer coverage.
- **Honest benchmarking.** Methodology (warm-up, repetitions, percentiles, hardware disclosure,
  noise control) is defined before any number is collected. No benchmark result is ever invented
  or compared across incompatible conditions.

## Quantitative value

What the project demonstrates on the research side:

- Three research questions answered rigorously (decision D9) instead of many shallow demos:
  latency vs. market-maker performance, inventory-aware vs. fixed-spread quoting, and queue
  position vs. fill probability.
- Experiments with explicit hypotheses, controls, seed management, repetitions, confidence
  intervals, and stated limitations — including what conclusions the simulation *cannot* support.
- Realistic execution modeling: partial fills, queue priority, maker-taker fees, configurable
  market-data and order-entry latency, and no look-ahead: strategies observe the market only
  through the delayed feed they would actually receive.

## Differentiation

Most student order-book projects are a matching loop with a README. MicroSim differs by:

1. **Specification-first development** — exchange rules and invariants are written, reviewed, and
   frozen before implementation, mirroring how real trading infrastructure is built.
2. **A latency laboratory** — latency is not a constant to mention but an experimental variable:
   participants have configurable market-data, order-entry, and processing delays, enabling
   queue-position and adverse-selection experiments most portfolio projects cannot express.
3. **Differential testing against a reference model** — the optimized engine is continuously
   checked against an obviously-correct slow implementation on generated scenarios.
4. **Research honesty** — simulated results are framed as properties of the simulated market;
   the project never claims live-market profitability.

## Intended audience

- **Recruiters and hiring managers** skimming the README: architecture diagram, measured
  benchmark table, research findings, limitations.
- **Interviewing engineers** opening the source: clean module boundaries, tests that encode the
  spec, commit history that shows disciplined increments.
- **The owner himself**: the docs teach market microstructure well enough to defend every
  decision in interviews (decision D2).

## Success criteria

The project succeeds when all of the following hold:

1. The engine passes 100% of its invariant property tests and differential tests, and fuzzing
   finds no crashes over a sustained run.
2. Replay determinism holds: identical seed and config reproduce identical event streams across
   runs (and across builds on the same platform).
3. Benchmarks exist for every hot operation (insert, cancel, modify, match, market-data publish)
   with p50/p95/p99/p99.9 on disclosed hardware, collected per the written methodology.
4. All three research questions have written answers with confidence intervals, sensitivity
   checks, and explicit limitations.
5. A stranger can clone the repo, build it with documented commands, and reproduce a named
   experiment to identical results.
6. The owner can explain every architectural decision, data-structure choice, and statistical
   method without notes.

## Explicit non-goals

- **No real-money trading, brokerage connectivity, or live market data.** MicroSim is a research
  simulator.
- **No claim of real-world strategy profitability.** Results characterize the simulated market
  only.
- **No distributed infrastructure.** No Kubernetes, Kafka, or microservices; a modular C++
  library with Python bindings and file-based experiment storage is the architecture.
- **No paid services anywhere** — build, CI, storage, and tooling are all free.
- **No exchange-feature completeness.** Auctions, halts, icebergs, odd lots, and multi-venue
  routing are out of scope unless a research question requires them (none does).
- **No machine-learning strategies in the core plan.** Deterministic baselines come first;
  ML/RL is a possible post-Release-6 extension at most.
