# CI Plan

GitHub Actions, free tier only. Fast feedback per PR; heavy work nightly; nothing flaky.

## Workflows

### `pr.yml` — every PR + push to main (~10–15 min target, jobs parallel)

| Job | Runner | Does |
|---|---|---|
| `format` | ubuntu | clang-format --dry-run; ruff/black on Python; markdown link check on changed docs |
| `tidy` | ubuntu | clang-tidy on changed C++ files |
| `deps` | ubuntu | `check_deps.py` (layering) + `spec_coverage.py` (every R/INV cited) |
| `linux-debug` | ubuntu | debug preset: unit + property(CI budget) + differential + replay tests |
| `linux-asan` | ubuntu | asan-ubsan preset: same suite |
| `linux-release` | ubuntu | release preset: full test suite + 60s/target fuzz smoke |
| `macos` | macos-15 (arm64, Xcode 16) | debug: full C++ suite (platform diversity catches real bugs; Xcode 16 for complete std::format, see BUILD_SYSTEM.md) |
| `python` | ubuntu + macos | build wheel, pytest (binding + micro-E2E: run→store→analyze→reproduce on 2-seed micro configs), nbconvert-execute notebooks on micro outputs |
| `bench-smoke` | ubuntu | 3-benchmark subset vs stored runner baseline, **labeled unstable**, regression >25% fails (gross breakage only — real gates run on the dev Mac per METHODOLOGY) |

Merge requirement: all jobs green. No flaky-test retry policy — a flaky test is a bug
(TEST_STRATEGY's determinism rules make this achievable).

### `nightly.yml` — scheduled

Extended property suite (all profiles × 100 seeds × 10⁶ msgs) · 30 min/target fuzz with
corpus merge (weekly corpus commit PR, automated) · Valgrind memcheck suite · full clang-tidy
· coverage report + ratchet check (llvm-cov; fails if engine/book < 95% line or drop vs main)
· linux perf-counter benchmark trends (stored as unstable series).

### `release.yml` — on tag `v*`

Full everything (nightly suite + long fuzz + reproducibility contract on all frozen
experiments' micro versions), wheel artifacts (linux x86_64 + macos arm64), GitHub Release
with changelog; blocks unless the tag commit's nightly already passed.

### `docs.yml` — on docs changes

Mermaid render check (mmdc compile), full-repo link check, spellcheck (codespell, curated
dictionary).

## Policies

- **Caching:** FetchContent deps + ccache keyed on compiler+preset+lockfiles; target ≤ 5 min
  warm `linux-debug`.
- **Concurrency:** per-branch cancel-in-progress (free-tier minutes discipline).
- **No performance comparisons across runners** (METHODOLOGY rule 2) — bench-smoke compares
  only to baselines recorded on the *same runner class* and is advisory-labeled in the PR
  summary, hard-failing only at the 25% gross threshold.
- **Badge honesty:** README badges = pr.yml + nightly status. No "coverage 100%" theater —
  the ratchet is the story.
- **Artifacts:** failed property/differential runs upload the shrunk scenario + seeds;
  failed fuzz uploads minimized crasher (these feed the fixture corpus per PROPERTY_TESTS).
- Dependabot: monthly, grouped (GitHub Actions versions + Python lock refresh); C++ dep bumps
  are manual (pinned tags are part of reproducibility).
