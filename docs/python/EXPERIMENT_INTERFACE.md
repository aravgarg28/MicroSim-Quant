# Experiment Interface

The layer that turns "run 1,400 simulations and analyze them" into one command. Pure Python
(`microsim.experiments`), built on PYTHON_API.md and EXPERIMENT_STORAGE.md.

## Configuration schema (TOML)

```toml
# configs/experiments/rq2.toml
[experiment]
name        = "rq2_inventory_vs_fixed"
question    = "RQ2"
frozen      = true                  # pilot configs: false; headline results require true
seed_base   = 20_000
n_seeds     = 200

[scenario]
file = "configs/scenarios/active.toml"    # frozen environment, hashed

[[arms]]                                   # one entry per experimental arm
name = "S1_baseline"
strategy_file = "configs/strategies/s1_fixed.toml"

[[arms]]
name = "S2_k2"
strategy_file = "configs/strategies/s2_skew.toml"
overrides = { k_skew = 2.0 }              # sweep points as overrides

[sweep]                                    # optional cartesian sweeps
"strategy.k_skew" = [0.5, 1.0, 2.0, 4.0, 8.0]

[run]
session_ns   = 600_000_000_000
crn          = true                        # arms share flow/agent streams per seed
store        = "results/rq2/"
event_logs   = "sample:10"                 # keep full logs for 10 seeds only
```

Loader semantics: file references resolved and inlined, then the whole resolved config
canonicalized and hashed (config_hash); `frozen=true` + any hash change vs the committed
hash-lock file (`configs/experiments/rq2.lock`) aborts — frozen means frozen.

## CLI

```
python -m microsim.experiments run configs/experiments/rq2.toml [--workers N] [--resume]
python -m microsim.experiments analyze rq2 [--out docs/research/findings/]
python -m microsim.experiments reproduce rq2 --arm S2_k2 --seed 20_017
python -m microsim.experiments list results/
```

- `run`: expands arms × sweep × seeds into a run matrix; executes on a `ProcessPoolExecutor`;
  writes each run's results + manifest as it completes (crash-safe); `--resume` skips
  completed run IDs (present + manifest-valid).
- **Progress:** tqdm-style bar with runs done/total, ETA from rolling mean run time, and a
  final summary (wall time, core-hours, failures).
- **Failure handling:** a run failing with a Python-visible error is retried once (transient
  OS-level flake allowance); an engine-assert crash is **never retried** — it is preserved
  (input log + manifest + core-dump note) and fails the experiment (EXPERIMENT_PLAN.md rule:
  invariant violations invalidate everything).
- `analyze`: regenerates the pre-registered tables/figures per RQ from stored Parquet into
  the findings doc's asset directory. Deterministic given stored results (bootstrap uses a
  fixed analysis seed recorded in the findings doc).
- `reproduce`: re-runs one (arm, seed) and byte-compares metrics vs stored (the
  EXPERIMENT_PLAN reproducibility contract).

## Notebook workflow

Notebooks (`experiments/notebooks/`) are for exploration and figure prototyping only:
they *read* stored Parquet via DuckDB; they never launch headline runs. Each RQ has one
`rqN_explore.ipynb` kept runnable (executed in CI on the micro-experiment output, nbconvert
--execute) so bit-rot is caught.

## Parallelism model

Process pool, one single-threaded simulation per worker (THREADING_MODEL.md's "parallelism at
the run level"). Workers = `min(cores − 1, config)`. Memory guard: run matrix chunked so peak
RSS stays bounded (each result bundle is dropped after write). No shared state between
workers except the filesystem (append-only run directories, atomic rename on completion).

## Reproducibility metadata (every run, written by the runner)

config_hash (resolved), scenario/strategy file hashes, master seed, git commit + dirty flag
(dirty tree ⇒ frozen runs refuse to start), microsim version, Python/NumPy versions, compiler
+ flags string (baked into the extension at build), platform string, hostname, start time
(wall), duration. Schema in EXPERIMENT_STORAGE.md.
