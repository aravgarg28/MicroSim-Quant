# Sanitizer Plan

Which sanitizers, where they run, and the policy that keeps them meaningful (S5).

| Sanitizer | Catches | Where | When |
|---|---|---|---|
| AddressSanitizer (ASan) + LeakSanitizer | heap/stack/global overflows, use-after-free/return, leaks | Linux CI job + macOS CI job (both toolchains find different issues) | every PR: unit+property+differential suites; nightly: extended property + fuzz (fuzzers always ASan) |
| UndefinedBehaviorSanitizer (UBSan) | signed overflow (the overflow-analysis backstop), invalid shifts/casts, misaligned access, div-by-zero | combined into the ASan jobs (`-fsanitize=address,undefined`) | same |
| ThreadSanitizer (TSan) | data races, lock-order issues | own Linux CI job (TSan is incompatible with ASan) | dormant until the first `std::thread` lands (R5 evaluation); then mandatory on every PR touching threaded targets |
| MemorySanitizer | uninitialized reads | **excluded** (S5): requires fully MSan-instrumented libc++ — impractical to maintain. Compensations: UBSan, `-Wuninitialized`/analyzer warnings-as-errors, and debug-build pattern-fill allocator for slabs/pools | — |
| Valgrind (memcheck) | uninit reads (partial MSan substitute), leaks | Linux CI, nightly only (10–50× slowdown) on the unit+worked-example suites | nightly |

## Build configuration

- Dedicated CMake presets: `asan-ubsan`, `tsan` (Debug-O1, frame pointers kept,
  `-fno-omit-frame-pointer`, no LTO).
- UBSan runs with `-fno-sanitize-recover=all` — any finding is a hard failure, no
  "known-issues" suppression file for our own code. Third-party suppressions (if ever needed)
  live in `scripts/sanitizers/suppressions.txt` with a comment per line justifying it and an
  issue link; the file starting empty and staying empty is the goal.
- `ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1`,
  `halt_on_error=1`; `detect_leaks=1` on Linux (LSan unsupported on macOS ARM — noted, Linux
  covers it).
- Slab/pool caveat (MEMORY_MODEL.md): custom slabs can mask use-after-free from ASan (freed
  slots stay mapped). Mitigation: debug/sanitizer builds poison recycled slots
  (`__asan_poison_memory_region`) and use the generation-counter guard; this integration is a
  named implementation task, not an afterthought.

## Policy

1. Sanitizer-clean is a merge requirement — a PR with any finding does not merge, no
   exceptions, no deferral labels.
2. Fuzzers run sanitized always (a fuzzer without ASan is a coverage generator, not a bug
   finder).
3. Release binaries ship without sanitizers (obviously), but the release tag process requires
   the full nightly sanitizer suite green on that commit.
4. Any bug a sanitizer catches post-merge becomes a regression test + a retro note in the
   PR/task log (what test layer should have caught it earlier, and why it didn't).
