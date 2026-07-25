# Build System

CMake ≥ 3.27, C++20 (S1), FetchContent-pinned dependencies (S2). One-command builds on macOS
(AppleClang ≥ 15) and Linux (Clang ≥ 16, GCC ≥ 13 as the second compiler for warning
diversity).

## Presets (`CMakePresets.json` — the only supported way to configure)

| Preset | Flags core | Purpose |
|---|---|---|
| `debug` | `-O0 -g`, invariant hooks ON, checked arithmetic ON, slot generations ON | development default |
| `release` | `-O2 -DNDEBUG -flto=thin`, hooks OFF | benchmarks, experiments, ship |
| `asan-ubsan` | debug-O1 + `-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer` | SANITIZER_PLAN |
| `tsan` | as above with `thread` | dormant until R5 |
| `bench` | release + benchmark targets + counting-allocator hook | BENCHMARK_PLAN |
| `stagecount` | release + pipeline stage counters | macro breakdowns (labeled builds) |

## Warnings (`cmake/warnings.cmake`, applied to all first-party targets)

`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor
-Wold-style-cast -Woverloaded-virtual -Wdouble-promotion -Wimplicit-fallthrough
-Wno-unused-parameter` … **`-Werror` on CI, off locally** (locally you may iterate with
warnings visible; CI is the gate — avoids the "can't build at HEAD" trap while keeping main
warning-clean). Third-party code via FetchContent is `SYSTEM`-included: zero warnings from
dependencies, zero suppressions of our own.

## Dependencies (all FetchContent, exact tags pinned in `cmake/deps.cmake`)

GoogleTest, Google Benchmark, pybind11, tomlplusplus (config parsing), fmt (logging/format —
also practice for `std::format` migration). That is the complete C++ dependency list; adding
one requires a DECISIONS.md entry. Python deps in `pyproject.toml` (numpy, pandas, pyarrow,
duckdb, scipy, statsmodels, matplotlib, pytest, hypothesis), locked via `uv.lock` (uv as the
documented workflow; pip works too).

## Targets and layering

Per REPOSITORY_STRUCTURE; `target_link_libraries` PRIVATE by default, PUBLIC only where a
type crosses an interface. `check_deps.py` (CI) re-derives the include graph and fails on any
edge not in COMPONENT_BOUNDARIES' allowed list.

## Python extension build

scikit-build-core drives CMake for `pip install -e .` / wheel builds; the extension links the
static libs (release preset). Wheels built in CI for Linux x86_64 + macOS arm64 (artifact
only — no PyPI publishing planned; install-from-source is the documented path).

## Tooling

- `clang-format` (file committed; LLVM-based style, 100 cols) — CI checks, `scripts/format.sh`
  fixes. **Pinned to version 20.1.7**, installed in CI via `pip install clang-format==20.1.7`
  so formatting is reproducible across machines and does not drift with a contributor's local
  LLVM version.
- `clang-tidy` (curated check list: bugprone-*, performance-*, modernize-* minus noisy ones,
  cppcoreguidelines subset) — CI on changed files; full run nightly.
- `.editorconfig`, `.gitattributes` (LF everywhere) — cross-platform hygiene.

## Installation / packaging

`cmake --install` lays out headers+libs+CLIs (mostly a correctness exercise); Docker
dev-container (`.devcontainer/`) gives Linux tooling on the Mac (S8, optional). No system
package targets (deb/rpm/homebrew) — out of scope, noted.

## Platform support statement

Tier 1: macOS arm64 (dev + authoritative benchmarks), Ubuntu LTS x86_64 (CI). Tier 2 (should
work, untested): other Linux, macOS x86_64. Not supported: Windows/MSVC (would cost real
effort in a project that never runs there — documented tradeoff, revisit only if a
contributor appears).
