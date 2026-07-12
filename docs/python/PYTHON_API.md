# Python API

The `microsim` package: pure-Python research layer wrapping the `_microsim` pybind11
extension. Binding philosophy: **coarse-grained and copy-out**. Python configures, launches,
and analyzes; C++ simulates. No Python code runs inside the event loop, ever (a Python
callback per event would be both slow and a determinism hazard).

## The boundary (what crosses, in each direction)

**In (construction time):** one `SimulationConfig` — plain data (instrument, participants,
strategies with parameters, flow model, latency tiers, session length, seeds, metric options).
Validated in C++; errors raise `microsim.ConfigError` with the offending field path.

**Out (after `run()`):** immutable result bundles — NumPy arrays / Arrow record batches
**copied** out of C++ (decision: lifetime safety over zero-copy; results cross once per run).
No live references into engine state are ever exposed; there is deliberately no
`sim.get_order_book()` handle — mid-run state exists only inside C++.

## Core API sketch

```python
import microsim as ms

cfg = ms.SimulationConfig.from_toml("configs/experiments/rq2_arm3.toml")   # or built in code:
cfg = ms.SimulationConfig(
    instrument=ms.Instrument(symbol="SIM", tick_size="0.01", lot_size=1,
                             price_band=("5.00", "15.00"), initial_reference="10.00"),
    session_ns=600_000_000_000,
    master_seed=12345,
    flow=ms.PoissonFlow(lam_limit_buy=..., ...),          # stage-2 params
    agents=[ms.NoiseProvider(...), ms.NoiseTaker(...)],
    strategies=[ms.InventoryMM(half_spread_ticks=2, k_skew=2.0, ...,
                               risk=ms.HarnessRisk(loss_limit="50.00", ...),
                               latency=ms.LatencyTier.COLO)],
    metrics=ms.MetricOptions(markout_horizons_ms=[1,10,100,1000], event_log=False),
)

result = ms.run(cfg)                       # releases the GIL; one C++ simulation
result.manifest                            # seed, config_hash, git_hash, versions, duration
result.metrics                             # dict[str, np.ndarray] session-level rows
result.timeseries("equity", participant="mm0")   # (t_ns, value) arrays
result.fills(participant="mm0")            # struct arrays: ts, side, px_ticks, qty, fee, ...
result.to_parquet("results/run_000/")      # manifest.json + *.parquet

ms.replay("results/run_000/input_log.bin") # replay verification, returns match report
```

Notes pinned by this sketch:
- **Prices cross the boundary as strings/ints/Decimal — never binary floats**
  (NUMERIC_REPRESENTATION rule 1). `tick_size="0.01"` parses exactly; `tick_size=0.01` raises
  `TypeError`.
- `ms.run` releases the GIL → thread-based fan-out works, though the experiment runner uses
  processes anyway (memory isolation, true parallelism).
- Determinism through the boundary is tested: same cfg → `result_a.metrics == result_b.metrics`
  bit-exactly, and `to_parquet` output byte-identical.

## Error model

| C++ | Python |
|---|---|
| Config validation failure | `microsim.ConfigError` (field path + reason) |
| Engine invariant assert | process-fatal by design — documented loudly; Python sees a crashed worker, the experiment runner reports which run/seed and preserves its input log for replay debugging |
| Per-message rejects | not exceptions — they are data (reject events in results) |

## Binding rules for the implementer (pybind11 specifics)

- All bound types are value types or owned by Python via `unique_ptr` returns; no
  `return_value_policy::reference` anywhere; no keep-alive webs.
- Config structs bound with `py::kw_only()` args, defaults mirroring C++ defaults
  (single source: C++ headers define defaults; bindings read them — never re-typed in Python).
- Result arrays: allocate NumPy arrays via `py::array_t`, memcpy column data in, hand
  ownership to Python. Arrow optionality kept behind a small adapter (start NumPy-only; Arrow
  when DuckDB integration wants it).
- No global state in the extension; two simulations in one process must not share anything
  (architecture rule 4) — tested.
- Stub files (`.pyi`) generated (pybind11-stubgen) and shipped so IDEs/type-checkers see the
  API; `mypy --strict` passes on the pure-Python layer.

## Versioning

`microsim.__version__` = C++ project version (single source: CMake project version, injected).
Result manifests refuse to load into analysis tooling from a different major version without
`allow_version_skew=True`.
