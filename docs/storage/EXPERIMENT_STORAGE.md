# Experiment Storage

File-based, free, queryable: Parquet files organized by experiment/run, queried through DuckDB
views. No services, no databases to run (S7).

## Layout

```
results/
  <experiment>/                       # e.g. rq2/
    experiment.json                   # resolved config + hash + run-matrix summary
    runs/
      <run_id>/                       # e.g. S2_k2__seed20017/
        manifest.json
        metrics_session.parquet       # 1..k rows: session-level metrics per participant
        metrics_timeseries.parquet    # sampled series (equity, position, spread, ...)
        fills.parquet                 # per-fill rows for subject participants
        probes.parquet                # RQ3 probe outcomes (when enabled)
        input_log.bin                 # sequenced input log (sampled seeds only)
    analysis/                         # analyze-command outputs (tables, figures)
```

Run directories are written to `<run_id>.tmp/` and atomically renamed on success — a present
directory is always complete.

## Schemas (columns; all Parquet, snappy)

**manifest.json** — run_id, experiment, arm, seed, config_hash, scenario_hash,
strategy_hashes, git_commit, git_dirty (must be false for frozen), microsim_version,
python_version, numpy_version, compiler_flags, platform, hostname, started_utc, duration_s,
exit_status.

**metrics_session** — run_id, arm, seed, participant, metric (string), value (double),
unit (string). Long/tidy format: new metrics never change the schema.

**metrics_timeseries** — run_id, participant, ts_ns (int64), series (string), value (double).
Sampled at `metric_interval` (config).

**fills** — run_id, participant, ts_ns, order_id, side (int8), px_ticks (int64),
qty_lots (int64), liquidity (M/T), fee_minor (int64), markout_1ms/10ms/100ms/1s (double,
nullable — computed in-engine at horizon expiry).

**probes** (RQ3) — run_id, ts_join_ns, depth_ahead_lots, side, outcome (FILLED/CANCELED/
CENSORED/SWEPT), t_outcome_ns, spread_at_join, near_depth_at_join.

**benchmarks** (separate tree, `results/benchmarks/<date>_<git>/`) — benchmark name, params,
iterations, wall stats (p50/p95/p99/p999/max ns), allocs_per_op, bytes_per_op, plus a
hardware/flags manifest identical in spirit to run manifests. Never mixed with research
tables.

## DuckDB access

No persistent DB file required — views over globs, created by `microsim.experiments`:

```sql
CREATE VIEW session AS SELECT * FROM read_parquet('results/*/runs/*/metrics_session.parquet');
CREATE VIEW fills   AS SELECT * FROM read_parquet('results/*/runs/*/fills.parquet');
-- manifests: read_json_auto('results/*/runs/*/manifest.json')
```

An optional `results/index.duckdb` materializes these for speed on large result sets;
it is always regenerable (cache, not source of truth — git-ignored, like all of `results/`).

## What is committed to git

Only: hash-lock files for frozen experiments, the `analysis/` tables+figures that findings
docs embed (small, versioned with the text they support), and micro-experiment golden outputs
used by CI reproducibility checks. Raw run data stays local (disk budget documented per
experiment; RQ1's full matrix ≈ a few GB with sampled event logs).

## Retention & provenance rules

- A findings doc may only cite numbers whose generating run directories carry frozen
  config hashes matching the committed lock files and a clean git_commit that is an ancestor
  of the doc's commit. `analyze` enforces this — provenance is checked, not promised.
- Re-running an experiment appends under a new experiment version suffix (`rq2_v2/`) rather
  than overwriting; findings docs state which version they cite.
