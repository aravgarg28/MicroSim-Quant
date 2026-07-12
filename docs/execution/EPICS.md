# Epics

Work grouped into epics, mapped to releases. Task-level detail lives in
IMPLEMENTATION_TASKS.md — fully expanded for the release in progress, title-level for later
releases (rolling-wave planning, decision D16: detailing R4 tasks before R2 exists would be
speculation dressed as rigor).

| Epic | Release | Contents | Spec anchor |
|---|---|---|---|
| E01 Project foundation | R1 | repo scaffold, CMake presets, warnings, deps, format/tidy, CI v1 | BUILD_SYSTEM, CI_PLAN |
| E02 Core types & events | R1 | strong types, enums/reasons, message/event structs, config structs + validation, boundary conversions | NUMERIC_REPRESENTATION, EXCHANGE_RULES §1–4 |
| E03 Determinism kit | R1 | named RNG streams + samplers, simulation clock/event queue | SIMULATION_CLOCK |
| E04 Reference book | R1 | ReferenceBook + line-by-line review gate | REFERENCE_MODEL |
| E05 Exchange pipeline | R1 | gateway/registry, sequencer + input log, matching (new/cancel/modify/session-end), minimal risk | MATCHING_ENGINE_SPEC, EXCHANGE_RULES |
| E06 Correctness harness | R1 | invariant checkers, scenario generator + property suite, differential harness, fuzz targets v1 | PROPERTY_TESTS, FUZZING_PLAN |
| E07 Fast book | R1 | FastBook stage 1 (array levels), differential green | ORDER_BOOK_DESIGN |
| E08 Replay & scripts | R1 | input-log replay + verification, scripted scenarios, microsim_run/replay CLIs | DATA_FLOW replay, ORDER_FLOW_MODELS stage 1 |
| E09 Benchmark harness | R1 | Google Benchmark suite v1, counting allocator, bench_compare, baseline JSON | BENCHMARK_PLAN, METHODOLOGY |
| E10 Market data | R2 | publisher (level feed, snapshots, batches), ConsumerBook, gap/recovery, consumer≡engine differential | MARKET_DATA_PROTOCOL |
| E11 Accounting | R2 | positions/cash/basis/P&L, reconciliation properties, mark price | POSITION_AND_PNL |
| E12 Risk completion | R2 | full exchange-side checks on real accounting, STP | RISK_ENGINE, R-8/9 |
| E13 Flow & agents | R2 | Poisson generator, noise LP/taker, scenario configs + statistical validation | ORDER_FLOW_MODELS 2, PARTICIPANT_MODELS |
| E14 Python bindings | R2 | _microsim extension, SimulationConfig, results copy-out, wheels, pytest | PYTHON_API |
| E15 Experiment framework | R3 | TOML schema + hash locks, runner, storage, analyze/reproduce CLI | EXPERIMENT_INTERFACE, EXPERIMENT_STORAGE |
| E16 Strategies & harness | R3 | QuoteEngine, StrategyRiskHarness, S1/S2 (+S3/S4, S5 stretch), metrics collectors | STRATEGY_SPECIFICATIONS, METRICS, RISK_ENGINE L2 |
| E17 RQ2 experiment | R3 | pilot→freeze→production→findings for RQ2 | RESEARCH_QUESTIONS, EXPERIMENT_PLAN |
| E18 Optimization pass 1 | R3 | profile, allocation removal, layout, FastBook stage 2 | OPTIMIZATION_ROADMAP 1–4 |
| E19 Latency lab | R4 | full latency model (jitter/spikes/tiers), MBO feed, queue tracker, race fixtures | LATENCY_MODEL, MD §MBO |
| E20 RQ1 + RQ3 experiments | R4 | both experiments end-to-end + findings | RESEARCH_QUESTIONS |
| E21 Advanced flow | R5 | state-dependent flow, informed/momentum/mean-reversion agents, Hawkes (stretch), replication study | ORDER_FLOW_MODELS 3–4 |
| E22 Multi-instrument & threading eval | R5 | instrument registry, N books, portfolio accounting, sharding evaluation vs adoption bar | THREADING_MODEL |
| E23 Dashboard | R6 | FastAPI + React, five screens, timeboxed | OPTIONAL_DASHBOARD |
| E24 Presentation assembly | continuous, gates at R3/R4/R6 | README sections, findings docs, resume fill-in, demo scripts + data | README_PLAN, presentation docs |

Epic exit criteria are the corresponding ROADMAP release success criteria; no epic is "done"
while any of its spec anchor's MUST behaviors lacks a passing named test.
