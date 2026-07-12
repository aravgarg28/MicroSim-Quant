# Benchmark Plan

What gets measured, on what workloads, reporting what. All numbers collected under
METHODOLOGY.md rules; anything else is not a number, it's an anecdote.

## Microbenchmarks (Google Benchmark, per-operation)

| Benchmark | Workload knobs | Primary outputs |
|---|---|---|
| `bm_book_insert` | book depth (10/100/1k/4k levels), at-best vs random-level placement, Fast vs Reference | ns/op p50/p95/p99/p99.9, allocs/op |
| `bm_book_cancel` | depth, front/middle/back of queue | same |
| `bm_book_modify` | priority-keeping (qty↓) vs re-queue (price Δ) | same |
| `bm_match_market` | sweep depth 1/5/20 levels, partial vs full clears | same |
| `bm_match_limit_marketable` | same + remainder-rests path | same |
| `bm_event_queue` | queue depth 10³–10⁶, schedule+pop cycle | ns/op |
| `bm_md_publish` | deltas per message batch 1/5/20 | ns/msg |
| `bm_consumer_apply` | batch sizes; snapshot rebuild | ns/msg |
| `bm_accounting_fill` | opening/reducing/flipping fills | ns/fill |
| `bm_rng_streams` | per-distribution draw cost | ns/draw |
| `bm_latency_model` | base-only vs jitter+spike composition | ns/msg |

## Macrobenchmarks (end-to-end, `microsim_run` on canned configs)

| Benchmark | Scenario | Primary outputs |
|---|---|---|
| `macro_throughput` | `active` flow, no strategies, no latency | engine messages/sec (wall), CPU util |
| `macro_full_stack` | `active` + 2 MMs + latency model + metrics | messages/sec; breakdown share per stage (from built-in cycle counters around pipeline stages, enabled by a compile flag) |
| `macro_replay` | replay recorded log of the above | events/sec vs live-generation ratio |
| `macro_pyboundary` | run via Python; results export | run-setup overhead ms; export MB/s; % overhead vs native CLI |
| `macro_scaling` [R5] | N instruments × {1, N} threads | THREADING_MODEL adoption-bar table |

Deep/shallow book, many-price-level, and multi-instrument coverage come from the workload
knobs above rather than separate benchmark names.

## Hardware counters

macOS ARM lacks `perf`; the plan uses tiered sources honestly (D10):
- **All platforms:** wall-time percentiles, allocs/op (counting hook), RSS, throughput.
- **Dev Mac:** Instruments (os_signpost regions around pipeline stages) for time profile +
  allocation profile; `dtrace`-based counters where entitlements allow.
- **Linux CI (labeled unstable, regression-only):** `perf stat` cache-misses, branch-misses,
  IPC on the microbenchmark suite — trends only, never headline numbers (shared-runner noise).

## Regression protocol

`scripts/bench_compare.py old.json new.json`: flags any benchmark whose p50 regresses > 5%
or p99 > 10% (thresholds per METHODOLOGY.md noise floors). CI smoke job runs a 3-benchmark
subset for gross breakage; the real gate is a pre-release full run on the dev Mac compared
against the last release's stored JSON (`results/benchmarks/`).

## Reporting

README table carries: benchmark, workload, p50/p99, allocs/op, and links the raw JSON +
manifest (hardware, flags, date, git). Every number reproducible via
`scripts/run_benchmarks.sh --suite release`.
