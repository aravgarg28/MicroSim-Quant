# Implementation Tasks

PR-sized tasks for the implementer. **Release 1 is fully specified below.** Later releases are listed at
title+objective level and get full expansion when their release starts (rolling-wave, D16) —
expanding R4 tasks before R1 code exists would fossilize guesses.

## Task template (fields and their defaults)

Every task block carries: **ID · Title · Objective · Scope · Excludes · Prereqs · Files ·
Interfaces/Data · Tests · Verify · DoD extras · Risk · Size · Model · the maintainer-review**.
Conventions that apply to ALL tasks (stated once, not repeated):

- *Background* = the spec sections cited in the block; read them first.
- *Numerical requirements* = NUMERIC_REPRESENTATION.md everywhere, always.
- *Determinism requirements* = R-10.3 everywhere: no wall clock, no unordered iteration into
  outputs, no unseeded randomness. Tasks with extra determinism needs say so.
- *Concurrency assumptions* = single-threaded (THREADING_MODEL) until R5, no exceptions.
- *Complexity expectations* = per ORDER_BOOK_DESIGN/spec tables where relevant, else "obvious
  implementation is fine".
- *Documentation updates* = if behavior differs from any doc, STOP and follow the deviation
  protocol (OPUS_HANDOFF) — docs are fixed by the maintainer, not drive-by edited.
- *Definition of Done* (all tasks): named tests cite their rule/INV IDs; suite green under
  `debug` and `asan-ubsan` presets locally; CI green; `clang-format`/`tidy` clean; commit
  message references the task ID; completion report filed (OPUS_HANDOFF format).
- *Verify* commands assume: `cmake --preset <p> && cmake --build --preset <p>` then
  `ctest --preset <p> [-R <filter>]`. Abbreviated below as `build+test(<p>) [filter]`.
- Sizes: S ≈ half-day PR, M ≈ 1-day, L ≈ 2-day (the implementer-days; split anything trending past L).

---

## Release 1 — fully specified

### R1-01 · Repository scaffold and build skeleton
- **Objective:** Buildable empty skeleton of the whole layout.
- **Scope:** Directory tree per REPOSITORY_STRUCTURE.md; root CMake + per-module CMakeLists
  with empty `microsim_<module>` targets (one placeholder header+cpp each); CMakePresets.json
  (`debug`, `release`, `asan-ubsan` — `tsan`/`bench`/`stagecount` stubs commented);
  `cmake/warnings.cmake`, `cmake/deps.cmake` (GoogleTest+Benchmark pinned, fetched, linked to
  one smoke test/benchmark); `.clang-format`, `.clang-tidy`, `.gitignore`, `.gitattributes`,
  `.editorconfig`; `scripts/format.sh`.
- **Excludes:** CI (R1-02); any real types; pybind; tomlplusplus/fmt (added when first used).
- **Prereqs:** none.
- **Files:** everything top-level; `src/*/`.
- **Tests:** one trivial gtest per module target proving link; one trivial benchmark runs.
- **Verify:** `build+test(debug)`, `build+test(asan-ubsan)`, `./scripts/format.sh --check`.
- **Risk:** low · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-02 · CI pipeline v1
- **Objective:** `pr.yml` jobs: format, linux-debug, linux-asan, linux-release, macos-debug
  (per CI_PLAN, minus tidy/deps/python/bench-smoke which arrive with their subjects). ccache
  + FetchContent caching; branch protection notes in README-dev section.
- **Prereqs:** R1-01.
- **Verify:** green run on a PR touching a placeholder.
- **Risk:** low · **Size:** S · **Model:** the implementer · **the maintainer-review:** no.

### R1-03 · Core strong types
- **Objective:** `Price`, `Qty`, `Cash`, `SimTime`, `Duration`, `OrderId`, `ClientOrderId`,
  `ParticipantId`, `InstrumentId`, `Seq`, `TradeId`, `Side` with the exact arithmetic rules
  of NUMERIC_REPRESENTATION §strong-types (allowed ops compile, banned ops don't).
- **Scope:** `src/core/include/microsim/core/types.hpp` (+ minimal .cpp); `static_assert`
  suite for type rules; `std::format`/fmt formatters printing via integer scale math.
- **Excludes:** Notional (needs instrument — R1-05); conversions from strings (R1-06).
- **Prereqs:** R1-01.
- **Tests:** unit: ops tables; compile-fail tests (CMake `try_compile` negative cases) for
  banned ops (Price+Qty etc.).
- **Verify:** `build+test(debug) core`.
- **Risk:** low · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-04 · Messages, events, reasons
- **Objective:** All inbound message and outbound event structs + `RejectReason`/
  `CancelReason` enums exactly matching EXCHANGE_RULES R-3.2/R-4.3/R-14 and
  MATCHING_ENGINE_SPEC I/O lists.
- **Scope:** `core/messages.hpp`, `core/events.hpp`; trivially-copyable + `sizeof ≤ 64`
  static_asserts; enum↔string tables (for logs/tests).
- **Excludes:** MD messages (R2/E10); serialization (R1-16 does the log format).
- **Prereqs:** R1-03.
- **Tests:** unit: layout asserts, enum table round-trips, exhaustive-switch coverage helpers.
- **Verify:** `build+test(debug) core`.
- **Risk:** low · **Size:** S · **Model:** the implementer · **the maintainer-review:** no.

### R1-05 · Instrument & participant config + validation
- **Objective:** `InstrumentConfig`, `ParticipantConfig` (risk fields incl. reserved
  notional cap), `SessionConfig`; validation per R-1.1/R-1.3 incl. the overflow-headroom
  check; `Notional()`.
- **Prereqs:** R1-03.
- **Tests:** unit: acceptance/rejection tables incl. overflow bounds at the edge; Notional
  exactness cases.
- **Verify:** `build+test(debug) core`.
- **Risk:** low · **Size:** S · **Model:** the implementer · **the maintainer-review:** no.

### R1-06 · Boundary conversions + TOML config parsing
- **Objective:** exact decimal-string ↔ ticks/lots/minor-units conversions (reject
  non-representable, `INVALID_TICK` semantics); tomlplusplus-based loading of instrument/
  session/participant config files; canonicalization + SHA-256 config_hash.
- **Prereqs:** R1-05. **Adds dep:** tomlplusplus (deps.cmake pin; DECISIONS D-listed already).
- **Tests:** unit: conversion tables ("10.03"→1003 with 0.01 tick; "10.031"→reject; negative,
  overflow, trailing-zero forms); hash stability goldens.
- **Fuzz:** `fuzz_config` target skeleton (full corpus work in R1-22).
- **Verify:** `build+test(debug) core`.
- **Risk:** medium (parsing edge cases) · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-07 · RNG streams and samplers
- **Objective:** master-seed → named-stream derivation (`hash(master, name)` → PCG/SplitMix64
  state); samplers: uniform int/real, Bernoulli, exponential (inverse CDF), geometric,
  discrete table, normal (rational-approx inverse CDF) — all platform-stable.
- **Scope:** `sim/rng.hpp`; golden-value tests (fixed seed → first 64 draws per sampler,
  identical on macOS and Linux CI).
- **Excludes:** Poisson process scheduling (E13).
- **Prereqs:** R1-03.
- **Tests:** unit: goldens; stream-independence (drop stream A ⇒ B unchanged); basic
  statistical sanity (mean/var within wide bands, seeded — never flaky).
- **Verify:** `build+test(debug) sim` on both platforms (CI proves cross-platform).
- **Risk:** medium (sampler numerics) · **Size:** M · **Model:** the implementer · **the maintainer-review:**
  yes (sampler math + stream-derivation review).

### R1-08 · Simulation clock and event queue
- **Objective:** the (fire_time, priority_class, insertion_seq) min-heap loop per
  SIMULATION_CLOCK.md: schedule/pop/dispatch, past-scheduling assert, session-end drain.
- **Prereqs:** R1-03, R1-04.
- **Interfaces:** `Scheduler::schedule(SimTime, PriorityClass, Event)`, `run_until_empty()`,
  handler registration per priority-class owner.
- **Tests:** unit: full ordering table (every class pair, equal/unequal times, insertion
  ties); property: random event soups pop in exact tuple order; determinism: two loops, same
  schedule → identical dispatch log.
- **Benchmark:** `bm_event_queue` (schedule+pop at depths 10³–10⁶) — recorded, not optimized.
- **Verify:** `build+test(debug) sim`.
- **Risk:** medium · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-09 · ReferenceBook
- **Objective:** the oracle, exactly per REFERENCE_MODEL.md: `std::map`+`std::list`,
  rule-citing comments, `dump_state()`, recompute-on-demand aggregates, `OrderBookLike`
  concept definition (the shared interface both books implement).
- **Prereqs:** R1-04, R1-05.
- **Tests:** unit: every book operation vs hand-computed cases from the rules-doc worked
  example; the concept's compile-time checks.
- **Verify:** `build+test(debug) book`.
- **Risk:** medium (it's the oracle — bugs here poison everything) · **Size:** L ·
  **Model:** the implementer · **the maintainer-review:** **yes, mandatory line-by-line vs EXCHANGE_RULES.md
  before any dependent task starts** (REFERENCE_MODEL §verification).

### R1-10 · Order registry and gateway validation
- **Objective:** order state machine (R-4.2, absorbing terminals, slot recycling with debug
  generation counters), `order_id` assignment, client-order-id dedup, validation chain
  R-3.3 items 1–8 with first-failure-wins.
- **Excludes:** risk checks (R1-15); matching.
- **Prereqs:** R1-04, R1-05.
- **Tests:** unit: validation order proven by multi-defect messages (each defect pair →
  earlier reason wins); dedup; state-machine transition table incl. illegal-transition
  asserts; slot-recycle + generation guard.
- **Verify:** `build+test(debug) engine`.
- **Risk:** medium · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-11 · Sequencer and input event log
- **Objective:** seq/seq_out/ts_event stamping (R-10); persist writer v1: length-prefixed
  binary with version header + CRC per record; input-log tee; reader with corrupt-input
  rejection.
- **Prereqs:** R1-04; R1-10.
- **Tests:** unit: monotonicity, gap-free; round-trip write→read→byte-compare; truncated/
  corrupt file → clean error. **Fuzz:** `fuzz_persist_reader` target.
- **Verify:** `build+test(debug) 'engine|persist'`.
- **Risk:** low · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-12 · Matching: new orders (limit + market)
- **Objective:** `handle_new` + `match_loop` + `execute_trade` pseudocode
  (MATCHING_ENGINE_SPEC) against `OrderBookLike`, instantiated with ReferenceBook; fills,
  trades, NO_LIQUIDITY cancels, fee fields on fills (R-11 values from instrument config);
  emission order exactly per spec.
- **Excludes:** cancel/modify (next tasks), accounting (fee fields carried, not aggregated),
  MD (R2).
- **Prereqs:** R1-09, R1-10, R1-11.
- **Tests:** unit per rule: R-5.2..5.8, R-11.2 fee arithmetic, the §15 worked example
  end-to-end as a fixture.
- **Verify:** `build+test(debug) engine`.
- **Risk:** high (the core algorithm) · **Size:** L · **Model:** the implementer · **the maintainer-review:**
  yes (review vs pseudocode before R1-13 proceeds).

### R1-13 · Matching: cancel
- **Objective:** `handle_cancel` per R-6 (reason distinctions UNKNOWN_ORDER vs
  NOT_ORDER_OWNER vs TOO_LATE_TO_CANCEL).
- **Prereqs:** R1-12.
- **Tests:** unit: each reason path; cancel-then-anything absorbing checks.
- **Verify:** `build+test(debug) engine`. **Risk:** low · **Size:** S · **Model:** the implementer ·
  **the maintainer-review:** no.

### R1-14 · Matching: modify
- **Objective:** `handle_modify` per R-7 — priority table (price Δ / qty↑ requeue, qty↓ keep),
  modify-to-done, immediate-execution-on-marketable, single OrderModified before fills.
- **Prereqs:** R1-12, R1-13.
- **Tests:** unit: the full R-7.2 priority matrix (each cell a named test); R-7.3 edge
  (new_qty == filled); marketable-after-modify walk.
- **Verify:** `build+test(debug) engine`. **Risk:** high (most fiddly rules) · **Size:** M ·
  **Model:** the implementer · **the maintainer-review:** yes (queue-token/FIFO semantics).

### R1-15 · Minimal risk checks + position tally
- **Objective:** R-9.1/9.2/9.3 with a minimal signed-fill position tally (full accounting is
  E11; the tally is its seed) + worst-case open-qty computation from the registry; R-9.5
  delta checks on modify.
- **Prereqs:** R1-12..14.
- **Tests:** unit: boundary (at-limit pass / one-over fail) per check; both-sides-open
  worst-case cases; modify-delta cases; INV-14 recompute-from-scratch checker.
- **Verify:** `build+test(debug) engine`. **Risk:** medium · **Size:** M · **Model:** the implementer ·
  **the maintainer-review:** no.

### R1-16 · Session end
- **Objective:** R-12: OPEN→CLOSED, deterministic cancel-all order, MARKET_CLOSED rejects,
  half-tick mark computation (accounting hook stubbed to the tally).
- **Prereqs:** R1-15.
- **Tests:** unit: cancel ordering fixture; post-CLOSED reject; INV-17 skeleton check.
- **Verify:** `build+test(debug) engine`. **Risk:** low · **Size:** S · **Model:** the implementer ·
  **the maintainer-review:** no.

### R1-17 · Invariant checker harness
- **Objective:** `assert_invariants(engine, book)` implementing INV-1..9, 12, 13, 14 checks
  (11/16/17 arrive with their subjects) + the per-message test-build hook; shadow registry
  for INV-7.
- **Prereqs:** R1-16.
- **Tests:** self-test: hand-broken book states (crossed, misordered, over-filled…) each trip
  their checker (a checker that can't fail is decoration).
- **Verify:** `build+test(debug) engine`. **Risk:** medium · **Size:** M · **Model:** the implementer ·
  **the maintainer-review:** no.

### R1-18 · Scenario generator + property suite
- **Objective:** `ScenarioGen(seed, profile)` with all PROPERTY_TESTS.md profiles +
  state-aware id tracking; property suite wiring every implemented INV over CI budgets;
  shrinker (prefix bisection + deletion passes) emitting committed fixtures.
- **Prereqs:** R1-17.
- **Tests:** the suite itself + generator determinism goldens + shrinker unit tests.
- **Verify:** `build+test(debug) prop`, `build+test(asan-ubsan) prop`.
- **Risk:** high (test-infra quality gates everything) · **Size:** L · **Model:** the implementer ·
  **the maintainer-review:** yes (profile coverage + shrinker design).

### R1-19 · FastBook stage 1
- **Objective:** tick-indexed level array + `std::deque` FIFOs + `std::unordered_map` id
  lookup + cached best (linear next-non-empty scan; bitmap is E18), per ORDER_BOOK_DESIGN
  "MVP rollout"; implements `OrderBookLike`.
- **Prereqs:** R1-09 (concept), R1-18 (suite to run against).
- **Tests:** entire unit+property suite instantiated on FastBook.
- **Benchmark:** `bm_book_insert/cancel/modify` Fast vs Reference recorded (baseline).
- **Verify:** `build+test(debug) book`. **Risk:** medium · **Size:** L · **Model:** the implementer ·
  **the maintainer-review:** no (differential gate next task is the review).

### R1-20 · Differential harness
- **Objective:** engine<Fast> vs engine<Reference> over generated scenarios: event-stream
  equality + state-dump equality per message (INV-15); auto-save mismatch scenarios; CI +
  nightly budgets per PROPERTY_TESTS.
- **Prereqs:** R1-19.
- **Tests:** the harness + a deliberately-buggy book (mutation) proving it catches.
- **Verify:** `build+test(debug) diff`, then nightly-budget local run documented in the
  completion report.
- **Risk:** medium · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-21 · Replay engine and CLIs
- **Objective:** replay from input log (bypass generation, original timestamps) +
  `microsim_replay` (verify byte-identical, print ladders) + `microsim_run` v1 (run scripted
  scenario file → event log + book printout); INV-10 strengths 2 and 3 as tests (fresh
  process via CTest fixture).
- **Prereqs:** R1-11, R1-16.
- **Tests:** replay determinism (in-process, cross-process, from-log); CLI golden outputs.
- **Verify:** `build+test(release) replay` (+ debug).
- **Risk:** medium · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-22 · Scripted scenarios + fuzz targets v1
- **Objective:** TOML scenario-script schema (time, participant, action rows) + loader;
  fixture scripts for every worked example in the domain docs; `fuzz_gateway` +
  `fuzz_match_stream` per FUZZING_PLAN (invariant oracles wired), seed corpora from fixtures,
  60s CI smoke job.
- **Prereqs:** R1-18, R1-21.
- **Tests:** script loader units; fuzz targets build+run (CI smoke); corpus replay coverage
  note in completion report.
- **Verify:** `build+test(asan-ubsan)`; `./fuzz_gateway -runs=100000 corpus/`.
- **Risk:** medium · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-23 · Benchmark suite v1 + counting allocator + baseline
- **Objective:** `bench` preset; counting `operator new` hook; `bm_match_*`,
  `bm_accounting_fill` placeholder-free set per BENCHMARK_PLAN R1 scope; `bench_compare.py`;
  `scripts/run_benchmarks.sh`; record the **unoptimized baseline JSON** on the dev Mac
  (owner runs the script; the implementer provides it — CI cannot produce authoritative numbers).
- **Prereqs:** R1-19, R1-21.
- **Tests:** compare-script unit tests (fixture JSONs); allocs/op column present.
- **Verify:** `cmake --preset bench && ./scripts/run_benchmarks.sh --suite r1 --dry-run`.
- **Risk:** low · **Size:** M · **Model:** the implementer · **the maintainer-review:** no.

### R1-24 · Spec-coverage + layering enforcement, nightly CI
- **Objective:** `spec_coverage.py` (every R-x.y/INV-n referenced by ≥1 test name/comment —
  fail listing orphans), `check_deps.py` (include-graph vs COMPONENT_BOUNDARIES allowlist);
  `nightly.yml` (extended property, 30min fuzz, Valgrind on Linux, coverage ratchet).
- **Prereqs:** R1-22.
- **Verify:** both scripts pass on the tree; deliberate violations (temp branch) fail.
- **Risk:** low · **Size:** M · **Model:** the implementer (scripts) + the implementer (coverage-ratchet
  plumbing) · **the maintainer-review:** no.

### R1-25 · Release 1 assembly
- **Objective:** README v1 (skeleton sections 1–6, 10 filled per README_PLAN); demo replay
  polish; ROADMAP R1 success-criteria audit (each criterion → evidence link); tag `v0.1.0`.
- **Prereqs:** all R1 tasks.
- **Verify:** clean-clone quick-start on both platforms; R1 exit checklist in completion
  report.
- **Risk:** low · **Size:** S · **Model:** the implementer (assembly) · **the maintainer-review:** yes
  (release review — the maintainer audits criteria evidence).

---

## Release 2 — title level (expand at R2 start)

R2-01 MD publisher: level deltas + batches · R2-02 snapshots + SessionStatus ·
R2-03 ConsumerBook + gap/recovery + fault injection · R2-04 consumer≡engine differential ·
R2-05 accounting engine (basis algebra, worked-example fixtures) · R2-06 INV-11
reconciliation properties · R2-07 mark-price + INV-17 completion · R2-08 risk on real
accounting + notional cap · R2-09 STP CANCEL_NEWEST · R2-10 Poisson flow generator ·
R2-11 noise LP/taker agents + Participant interface · R2-12 scenario configs + statistical
validation tests · R2-13 pybind skeleton + SimulationConfig · R2-14 run + results copy-out ·
R2-15 wheels + pytest suite + `fuzz_md_consumer` · R2-16 E2E determinism-through-Python ·
R2-17 R2 assembly/tag.

## Release 3 — title level

R3-01 QuoteEngine · R3-02 StrategyRiskHarness (kill/PASSIVE) · R3-03 S1 · R3-04 S2 ·
R3-05 metrics collectors (fills/markouts/inventory/hygiene) — *design note: markout columns
require deferred emission at horizon expiry; collector buffers fills until mid(t+Δ) known* ·
R3-06 experiment TOML schema + hash locks · R3-07 runner (process pool, resume) ·
R3-08 storage writers + DuckDB views · R3-09 analyze/reproduce CLIs · R3-10 RQ2 pilot ·
R3-11 optimization phases 1–2 (profile, alloc removal) · R3-12 phases 3–4 (layout, FastBook
stage 2: intrusive/open-addressing/bitmap as three PRs) · R3-13 RQ2 production + findings ·
R3-14 S3/S4 (+S5 stretch) · R3-15 R3 assembly/tag + README benchmarks/findings sections.

## Release 4–6 — epic level only (see EPICS E19–E24; expanded at release start)
