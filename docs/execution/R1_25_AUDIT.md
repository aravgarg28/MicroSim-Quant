# R1-25: Release 1 Assembly — Completion Report

**Status:** complete
**Branch/PR:** direct to `main` (per the repo's current workflow — see `DECISIONS.md`; no
branch protection is configured on `main` yet)

## What was built

- `README.md` rewritten to README_PLAN v1: sections 1–6 and 10 filled (title/one-liner,
  what-this-is/is-not, architecture diagram, feature table, market rules in 30 seconds, quick
  start, correctness story), plus 11 (limitations) and 12 (roadmap/docs index) since both were
  trivial to fill honestly at this scale and materially help a first-time reader. Sections
  7–9 (example experiment, benchmarks table, research findings) are explicitly left for
  Release 2+, per README_PLAN — they depend on capabilities (Python interface, optimized
  benchmarks, research write-ups) that don't exist yet; filling them now would be aspirational,
  which README_PLAN's maintenance rules forbid.
- Demo replay polish: `microsim_run`, when given an output directory, now prints the exact
  `microsim_replay` invocation to run next (`apps/microsim_run.cpp`). Found while verifying the
  README's quick-start commands — the two CLIs have different argument conventions
  (`microsim_run <dir>` vs. `microsim_replay <input.log> <events.log>`), which is easy to get
  wrong by analogy; printing the paired command removes the trap without changing either CLI's
  interface (no behavior change, no test change needed).
- This audit: the R1 success-criteria walk below, with evidence for each criterion.

## Acceptance criteria (from the R1-25 task block)

- [x] README v1, skeleton sections 1–6, 10 filled per README_PLAN — see `README.md`.
- [x] Demo replay polish — see `apps/microsim_run.cpp` change above.
- [x] ROADMAP R1 success-criteria audit, each criterion → evidence link — see below.
- [x] Tag `v0.1.0` — tagged at this commit once this report is reviewed and pushed.

## ROADMAP R1 success-criteria audit

`docs/product/ROADMAP.md`'s Release 1 section states four success criteria. Auditing each
against what is actually running in CI today, not what is designed:

### 1. "All invariants hold over ≥1M generated events"

**Not yet met at the stated scale — flagged, not fixed, by design.** The property-test suite
(`tests/prop/test_property_suite.cpp`) checks all 17 invariants in `ENGINE_INVARIANTS.md` after
every message, across all 7 generator profiles, but the per-PR budget wired into CI is
**4 seeds × 2,000 messages per profile** (~56,000 events total across 7 profiles), kept
deliberately small so CI stays fast (comment in the test file: "Per-PR budget (seconds).
Nightly scales seeds and N up per PROPERTY_TESTS.md."). The 1M-event nightly sweep (100 seeds,
per `docs/testing/PROPERTY_TESTS.md`) is designed and documented but **not wired into any CI
job** — this is R1-24 (spec-coverage/nightly), which was explicitly deferred in the lean-MVP
pivot (D19, `docs/DECISIONS.md`) alongside R1-22 (TOML config) and heavy fuzz polish.

Evidence for what *is* running: `tests/prop/test_property_suite.cpp` (13 tests, all profiles ×
seeds clean at 2,000 msgs/profile), CI job `pr.yml` → `debug` and `asan-ubsan` presets, both
green on `main` at commit `f9bffff`.

**Recommendation:** either wire R1-24's nightly job before calling R1 fully done against its
own stated criterion, or amend the ROADMAP criterion to state the actual CI-verified scale and
move the 1M-event bar to R1-24 explicitly. Left open for the owner's call — not resolved
silently here per the "code never leads spec" rule.

### 2. "Sanitizers clean"

**Met.** `asan-ubsan` preset (AddressSanitizer + UndefinedBehaviorSanitizer, findings are hard
CI failures per `pr.yml`) is green on every test target, verified locally on this commit via a
genuine clean clone (`cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest
--preset asan-ubsan`) and continuously in CI on Linux.

### 3. "Replay determinism proven in CI"

**Met.** `tests/CMakeLists.txt` wires two CTest fixtures at increasing strength:
`app.replay.two_process_determinism` (two separate `microsim_run` process invocations produce
byte-identical logs, via `cmake -E compare_files`) and `app.replay.reproduces_event_log`
(`microsim_replay` regenerates the event stream from `input.log` and byte-compares it to the
recorded `events.log`, `PASS_REGULAR_EXPRESSION "reproduced byte-identically"`). Both pass in
CI and were re-verified in a fresh clean clone for this report (`ctest --preset debug`, tests
#187–188, `Total Tests: 189`, `100% tests passed`).

### 4. "Reference and optimized books agree on 100% of generated scenarios"

**Met at CI scale; full-scenario-space claim is bounded by the same nightly gap as #1.**
`tests/differential/test_differential.cpp` runs `FastBook` against `ReferenceBook` in lockstep
(full book-state + event-stream equality, INV-15) over all 7 profiles × seeds 1–4 × 2,000
messages, plus one dedicated 8,000-message crossing-heavy run — 100% pass, zero divergence,
every run. A `MutantBook` (deliberately reverses within-level FIFO order) is included in the
same test file and is confirmed to be caught by the harness, proving the differential check
actually detects divergence rather than trivially passing. The comment in the file documents
the same "nightly all-profiles × 100 seeds × 10⁶ messages, ASan+UBSan on" job as a separate,
larger, not-yet-wired job — the identical gap as criterion #1, tracked together.

## Verification output

Ran from a genuine fresh `git clone` of this repository (not the working tree), matching the
README's quick-start section exactly:

```
$ cmake --preset debug          # configure: exit 0
$ cmake --build --preset debug  # build: exit 0, 113/113 targets
$ ctest --preset debug
100% tests passed out of 189
Total Test time (real) =  28.81 sec

$ ./build/debug/bin/microsim_run
  4 trades, 80 lots matched, 7 orders processed.

$ ./build/debug/bin/microsim_run /tmp/microsim_demo
  logs: /tmp/microsim_demo/input.log, /tmp/microsim_demo/events.log

$ ./build/debug/bin/microsim_replay /tmp/microsim_demo/input.log /tmp/microsim_demo/events.log
  replay verified: event log reproduced byte-identically (INV-10).
```

Platform verified locally: macOS (AppleClang). Linux (`debug`, `asan-ubsan`, `release`) is
verified continuously by `pr.yml` CI, green on `main` at `f9bffff` — not re-run locally here
since no Linux host was available in this session; CI's last run against this exact tree is the
evidence.

## Tests added

None — this task is documentation + a CLI print-statement, no behavior change. Existing 189
tests re-verified from a clean clone (see above).

## Deviations / Questions

1. **README sections 11–12 (limitations, roadmap index) were filled**, though the task block
   only names sections 1–6, 10. Both were short, factual, and directly support the "honest
   skeptic" goal the README_PLAN states as its rule zero; no aspirational content was added
   (limitations restate facts already established in this audit and in DECISIONS.md). Flagging
   as a deviation for visibility, not blocking on it.
2. **Criterion #1 and #4's nightly-scale gap is not resolved, only documented.** Wiring R1-24
   is out of this task's scope (R1-25 depends on R1 tasks being *done*, and R1-24 was
   deliberately deferred by D19) but the ROADMAP criterion as literally written is not yet
   true. This is the one item in this report that needs the owner's explicit call: amend the
   ROADMAP wording, or schedule R1-24 before treating Release 1 as fully closed against its own
   criteria. Recorded here rather than resolved silently.

## Notes for reviewer

- Start with `README.md` end-to-end as a first-time reader would — that's the actual
  deliverable a recruiter sees.
- The one thing worth a real decision: what to do about criterion #1/#4's nightly-scale gap
  (see Deviations #2) before tagging `v0.1.0` as "Release 1 done."
- Tag `v0.1.0` and push are not done by this report — held for explicit go-ahead per the
  session's standing push-authorization rule.
