# MicroSim-Quant

> Deterministic simulated electronic exchange and market-microstructure research platform.
> A C++20 price-time-priority matching engine with a latency laboratory, agent-based order
> flow, and a Python research interface for reproducible market-making experiments.

**Status:** early construction (Release 1). The full specification suite lives under
[`docs/`](docs/); the vision is in [`docs/product/PROJECT_VISION.md`](docs/product/PROJECT_VISION.md)
and every design decision is logged in [`docs/DECISIONS.md`](docs/DECISIONS.md). A
recruiter-facing README (architecture diagram, benchmarks, findings) is assembled at the
Release 1 milestone; until then this file is developer-facing.

## Development

Requirements: CMake ≥ 3.27, Ninja, a C++20 compiler (AppleClang 16+ / Clang 16+ / GCC 13+).
Build tooling and rationale are documented in
[`docs/build/BUILD_SYSTEM.md`](docs/build/BUILD_SYSTEM.md).

```bash
# Configure, build, and test (development default)
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Sanitizers (AddressSanitizer + UndefinedBehaviorSanitizer, findings are hard failures)
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan

# Optimized build (benchmarks and experiments)
cmake --preset release && cmake --build --preset release && ctest --preset release
```

Format all C++ sources (or `--check` to verify, as CI does):

```bash
./scripts/format.sh          # rewrite in place
./scripts/format.sh --check  # verify only
```

**Canonical `clang-format` version: 20.1.7.** CI pins it via `pip install clang-format==20.1.7`
so formatting is reproducible across machines; use the same version locally to avoid spurious
diffs (`pipx install clang-format==20.1.7`, or Homebrew's is close enough for the current code).

### Contributing workflow

- Work happens on `task/<TASK-ID>-<slug>` branches, one implementation task per pull request
  (see [`docs/execution/OPUS_HANDOFF.md`](docs/execution/OPUS_HANDOFF.md) and
  [`docs/execution/IMPLEMENTATION_TASKS.md`](docs/execution/IMPLEMENTATION_TASKS.md)).
- CI ([`.github/workflows/pr.yml`](.github/workflows/pr.yml)) must be green before merge:
  formatting, plus build + test on Linux (`debug`, `asan-ubsan`, `release`) and macOS
  (`debug`). Warnings are errors on CI (`-Werror`); locally they are warnings so iteration
  stays unblocked.
- **Recommended branch protection for `main`** (set once by the repo owner in GitHub
  Settings → Branches): require the `CI` status checks to pass, require a pull request before
  merging, and disallow force-pushes. This keeps `main` always-green, which the roadmap
  depends on.

## License

MIT — see [`LICENSE`](LICENSE).
