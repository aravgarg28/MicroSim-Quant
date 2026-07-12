# Repository Structure

```
MicroSim-Quant/
├── CMakeLists.txt              # superbuild: options, presets glue, subdirectories
├── CMakePresets.json           # debug / release / asan-ubsan / tsan / bench presets
├── LICENSE  README.md
├── cmake/                      # toolchain fragments: warnings.cmake, deps.cmake (FetchContent
│                               #   pins), sanitizers.cmake, lto.cmake
├── src/                        # C++ libraries, one directory per module (COMPONENT_BOUNDARIES)
│   ├── core/       include/microsim/core/ + src/
│   ├── book/       include/microsim/book/ + src/       # ReferenceBook + FastBook
│   ├── engine/     include/microsim/engine/ + src/
│   ├── md/         include/microsim/md/ + src/
│   ├── sim/        include/microsim/sim/ + src/
│   ├── accounting/ agents/ strategy/ risk-in-engine… metrics/ persist/   (same pattern)
├── apps/                       # microsim_run, microsim_replay (thin mains)
├── python/
│   ├── bindings/               # pybind11 extension (_microsim)
│   └── microsim/               # pure-Python package: api, experiments/, analysis/
├── experiments/
│   └── notebooks/              # exploration notebooks (CI-executed on micro configs)
├── configs/
│   ├── scenarios/  strategies/  experiments/  flows/    # TOML + .lock hash files
├── tests/
│   ├── unit/  property/  differential/  fixtures/       # C++ (GoogleTest)
│   ├── fuzz/  fuzz/corpus/<target>/                     # libFuzzer targets + corpora
│   └── python/                                          # pytest suites
├── benchmarks/                 # Google Benchmark targets (bm_*, macro_*)
├── scripts/                    # run_benchmarks.sh, bench_compare.py, spec_coverage.py,
│                               #   check_deps.py, format.sh, dev-container helpers
├── docs/                       # everything you are reading
├── results/                    # git-ignored except benchmark manifests/goldens (see
│                               #   EXPERIMENT_STORAGE.md "what is committed")
└── dashboard/                  # [R6] FastAPI + React; zero build coupling to C++ tree
```

## Directory rules

- `src/<module>/include/microsim/<module>/` is the only cross-module include surface
  (COMPONENT_BOUNDARIES rule 8); `src/<module>/src/` is private. Include style:
  `#include "microsim/book/fast_book.hpp"`.
- `apps/` and `python/bindings/` are composition roots — the only places allowed to include
  everything.
- `tests/` mirrors `src/` naming (`tests/unit/book/…`) so navigation is mechanical.
- `configs/` is data, versioned and hashed; `results/` is output, disposable (git-ignored
  with the documented exceptions).
- One top-level `CMakeLists.txt` per directory that builds; no add_subdirectory reaching into
  siblings.
- Generated files (stubs `.pyi`, version headers) go to the build tree, never committed.

## Naming conventions (fixed now so Opus never invents)

- Files: `snake_case.{hpp,cpp}`; one primary class per header, named `PascalCase`.
- Namespaces: `microsim::core`, `microsim::book`, … matching module dirs.
- Tests: `test_<rule-id>_<slug>.cpp` / `prop_INV_<n>_<slug>` per TEST_STRATEGY.
- CMake targets: `microsim_<module>` (libs), `bm_<name>`, `fuzz_<name>`, `test_<module>`.
- Branches: `task/<TASK-ID>-slug` (per IMPLEMENTATION_TASKS); commits reference task IDs.
```
