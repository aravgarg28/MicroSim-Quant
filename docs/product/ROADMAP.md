# MicroSim — Roadmap

Seven releases. Each is independently demo-able and leaves `main` in a presentable state.
Release 3 is the first resume-worthy milestone (decision D11). The MVP defined in
[MVP_DEFINITION.md](MVP_DEFINITION.md) spans Releases 0–3 at reduced scope.

Legend: every release lists **Outcome · Features · Technical foundations · Tests · Performance
work · Risks · Success criteria · Exclusions**.

---

## Release 0 — Rules, invariants, and experimental design (specification release)

**Outcome:** A frozen, reviewable specification: someone could implement MicroSim from the docs
alone without inventing behavior. (This is the current spec-writing effort, CP1–CP8.)

**Features:** No code. All ~40 specification documents: product, domain rules, invariants,
architecture, engine design, numerics, simulation, strategies, research methodology, testing,
benchmarking, build/CI, presentation plans, and the ordered implementation task list.

**Technical foundations:** Decision log (`docs/DECISIONS.md`); repository structure defined.

**Tests:** N/A (test *plans* are the deliverable).

**Performance work:** Benchmark methodology written before any measurement exists.

**Risks:** Over-specification that Opus contradicts in practice → mitigated by a documented
deviation protocol in `docs/execution/OPUS_HANDOFF.md`.

**Success criteria:** Owner has approved all eight checkpoints; adversarial review (5 personas)
completed and revisions applied; every implementation task traceable to a spec section.

**Exclusions:** Any implementation code, including scaffolding.

---

## Release 1 — Correct single-instrument exchange simulator

**Outcome:** A provably correct, deterministic exchange core. Nothing user-facing yet beyond
tests and a demo executable that replays a scripted scenario and prints the book.

**Features:**
- Numeric foundation: integer tick prices, integer lot quantities, fixed-point cash.
- Event model: order submit/cancel/modify, acks, rejects, fills, book updates — all timestamped
  and sequence-numbered.
- Reference order book (slow, obviously correct) implemented **first**.
- Optimized order book and matching engine: price-time priority, partial fills, tick/lot
  validation, deterministic tie-breaking.
- Order gateway with duplicate/invalid message rejection.
- Discrete-event simulation clock; deterministic replay from an event log.
- Scripted scenario runner.

**Technical foundations:** CMake superstructure, FetchContent deps, strict warning set,
clang-format/clang-tidy config, CI skeleton (build + test on Linux and macOS), sanitizer builds.

**Tests:** Unit tests per exchange rule; property tests for all invariants on generated
scenarios; differential tests optimized-vs-reference; replay determinism test (byte-identical
logs); first libFuzzer target (gateway message handling).

**Performance work:** None beyond avoiding gratuitous allocation. Establish the benchmark
harness and record a *baseline* (unoptimized) measurement set for later comparison.

**Risks:** Matching-rule ambiguity discovered mid-implementation (mitigation: EXCHANGE_RULES.md
is the arbiter; gaps go back into the doc, not into ad-hoc code decisions). Owner review
bandwidth (mitigation: small PR-sized tasks).

**Success criteria:** All invariants hold over ≥1M generated events; sanitizers clean; replay
determinism proven in CI; reference and optimized books agree on 100% of generated scenarios.

**Exclusions:** Strategies, agents, latency, market-data consumer views, Python. Participant
accounting arrives in R2 (fill events carry fee fields from day one so the event schema never
changes, but nothing aggregates them yet).

---

## Release 2 — Market-data pipeline, accounting, and Python research interface

**Outcome:** A researcher can configure a simulation in Python, run seeded Poisson flow with
noise agents, and analyze fills, book states, and P&L in pandas.

**Features:**
- Market-data publisher: incremental updates + trades + snapshots, gap-detectable sequence
  numbers; consumer-side book reconstruction (strategies will read only this view).
- Participant accounting: position, cash, average entry price, realized/unrealized P&L,
  maker-taker fees and rebates.
- Pre-trade risk engine: max position, max order size, max open orders; deterministic
  check order; self-trade prevention (cancel-newest policy).
- Poisson order-flow generator + noise provider/taker agents (seeded).
- pybind11 module `microsim`: build config → run → result tables (NumPy/Arrow-friendly).
- Event/metric export; Python-side Parquet writing with run metadata (seed, config hash,
  git commit, package versions).

**Technical foundations:** Python packaging (scikit-build-core or setuptools-cmake), pytest
suite, CI job for Python wheel build + binding tests.

**Tests:** Accounting reconciliation properties (cash + inventory conservation, fee symmetry);
feed-consumer book equals engine book at every sequence number (differential); STP policy unit
tests; binding round-trip tests; end-to-end determinism test through Python.

**Performance work:** Measure market-data publication overhead and Python boundary overhead
(events/sec exported); no optimization yet.

**Risks:** Binding-layer lifetime bugs (mitigation: value/copy semantics at the boundary, no
exposed internal references — per PYTHON_API.md). Feed design leaking engine internals
(mitigation: strategies consume only the published feed).

**Success criteria:** RQ-ready pipeline: a notebook produces a book-state and trade DataFrame
from a seeded run; consumer book provably matches engine book; accounting reconciles exactly in
integer units over ≥1M events.

**Exclusions:** Market-making strategies, latency modeling, research experiments, dashboards.

---

## Release 3 — Market-making strategies and the first rigorous experiment ★ first resume-worthy state

**Outcome:** RQ2 answered: "When does inventory-skewed quoting outperform fixed-spread
quoting?" — with confidence intervals, sensitivity analysis, and stated limitations. README
gains real benchmark numbers and a research-findings section.

**Features:**
- Strategy engine: strategies as event-driven participants observing the (currently
  zero-latency) feed, submitting through the gateway with risk checks.
- Fixed-spread market maker; inventory-skewed market maker.
- Metrics collector: fill rate, spread captured, adverse-selection cost, inventory variance,
  P&L curves, drawdown, order-to-trade ratio, cancellation rate.
- Experiment controller: config-file-driven batch runs, seed sweeps, parameter sweeps,
  Parquet result store, DuckDB analysis views.
- RQ2 experiment executed per EXPERIMENT_PLAN.md; findings written in `docs/research/`.
- Volatility-adaptive quoter and order-book-imbalance quoter (stretch: Avellaneda–Stoikov-
  inspired quoter) as additional comparison arms.

**Technical foundations:** Config schema (TOML/JSON) with validation; experiment CLI.

**Tests:** Strategy unit tests (quoting decisions from crafted book states); no-look-ahead test
(strategy cannot observe events newer than its feed cursor); statistical smoke tests (e.g.
Poisson flow empirical rate ≈ configured rate); end-to-end experiment reproducibility test.

**Performance work:** First real optimization pass, driven by profiles: allocation removal in
the hot path, book-level storage tuning. Before/after benchmark comparison recorded per
methodology; README benchmark table updated with measured numbers.

**Risks:** Research conclusions overstated (mitigation: RESEARCH_QUESTIONS.md pre-registers
what conclusions are and aren't justified). P&L plausibility depends on flow assumptions
(mitigation: sensitivity analysis over flow parameters is part of the experiment design).

**Success criteria:** MVP acceptance checklist fully satisfied (see MVP_DEFINITION.md);
RQ2 writeup reviewed against the statistical-reviewer persona checklist.

**Exclusions:** Latency (still zero/constant-minimal), RQ1/RQ3, advanced order flow.

---

## Release 4 — Latency and queue-position laboratory

**Outcome:** RQ1 and RQ3 answered. Latency becomes a controlled experimental variable;
queue-position dynamics are measurable per order.

**Features:**
- Full latency model: per-participant market-data latency, order-entry latency, exchange
  processing delay; constant + seeded jitter distributions + configurable tail-spike events;
  latency tiers (e.g. "colocated" vs "retail"); cancel latency and cancel/replace races.
- Queue-position tracker: for every resting order, position in queue over time, time-to-fill,
  fill-vs-cancel outcome.
- Stale-view experiments: strategy decisions measurably based on delayed book state.
- RQ1 experiment: MM performance vs latency sweep (fill quality, adverse selection, P&L).
- RQ3 experiment: fill probability and time-to-fill vs initial queue position.

**Technical foundations:** Latency events integrated into the discrete-event clock with
deterministic tie-breaking (spec'd in SIMULATION_CLOCK.md and LATENCY_MODEL.md).

**Tests:** Latency determinism (same seed ⇒ same delivery order); causality property (no
participant acts on information before its delivery time); queue-tracker consistency vs
reference book; race-scenario scripted tests (two cancels, cancel-vs-fill, etc.).

**Performance work:** Event-queue (priority queue) performance under high event rates;
benchmark replay speed (events/sec) with latency model on/off.

**Risks:** Subtle look-ahead through shared state (mitigation: feed-cursor no-look-ahead test
extended to latency case; adversarial review item). Interpretation risk: latency results
depend on flow model (mitigation: sensitivity analysis across flow parameter grid).

**Success criteria:** RQ1 and RQ3 writeups complete with CIs and limitations; causality
property tested over generated scenarios; all three research questions now answered.

**Exclusions:** Multi-instrument, state-dependent flow, dashboard.

---

## Release 5 — Advanced order flow and multi-instrument support

**Outcome:** Robustness of earlier findings checked under richer flow; engine scales story
(N instruments) measured.

**Features:**
- State-dependent Poisson flow (arrival rates conditioned on spread/imbalance/volatility).
- Hawkes-process flow (stretch): self-exciting arrivals with documented calibration.
- Informed trader (trades toward a latent future price), momentum and mean-reversion agents.
- Multi-instrument: N independent books, per-instrument config, portfolio-level accounting.
- Replication study: re-run RQ1/RQ2 conclusions under the new flow models; report which
  findings survive.
- Threading evaluation: measure single-thread ceiling; if justified, one-thread-per-instrument
  with SPSC queues; TSan CI job. Determinism preserved (per-instrument streams remain ordered).

**Technical foundations:** Flow-model interface generalization; instrument registry.

**Tests:** Statistical validation of flow models (empirical vs theoretical properties);
cross-instrument isolation properties; TSan-clean concurrent runs; determinism under threading.

**Performance work:** Scaling benchmarks: events/sec vs instrument count vs thread count;
false-sharing audit; documented decision if threading is *rejected* by measurement.

**Risks:** Threading breaks determinism (mitigation: threads only across independent
instruments, never within a book; documented in THREADING_MODEL.md). Hawkes calibration
rabbit hole (mitigation: stretch-goal status, time-boxed).

**Success criteria:** Replication section added to research findings; scaling table in README;
threading decision (adopted or rejected) documented with measurements.

**Exclusions:** Dashboard, network protocols.

---

## Release 6 — Visualization dashboard (optional, strictly last — decision D13)

**Outcome:** A local web demo for interviews: watch a replayed simulation's order book evolve,
inspect strategy inventory/P&L, and browse experiment results.

**Features:**
- FastAPI service reading replay logs and Parquet results (read-only; no engine coupling).
- React/Next.js screens: order-book depth replay with playback controls; trade tape; strategy
  inventory & P&L charts; experiment result browser; benchmark result viewer.
- Latency-tier comparison view (side-by-side P&L of fast vs slow MM from RQ1 data).

**Technical foundations:** Static-file deployment (free); no databases or paid services.

**Tests:** API contract tests; smoke test that the demo renders a canned replay.

**Performance work:** None (out of scope by definition).

**Risks:** Time sink (mitigation: hard gate — only starts after R5 success criteria; feature
list is fixed; anything extra is out).

**Success criteria:** Three-minute demo script (docs/presentation/DEMO_SCRIPT.md) runs entirely
against the dashboard on a clean machine.

**Exclusions:** Live simulation control from the UI (replay-only), auth, hosting/deployment
beyond `localhost`.

---

## Cross-release rules

- `main` is always green; every release ends with tagged version `v0.N.0`.
- Benchmark numbers in the README always carry hardware, build flags, and methodology link.
- Research claims never exceed what RESEARCH_QUESTIONS.md pre-registered as justified.
- Any scope addition requires a DECISIONS.md entry.
