# Test Strategy

The test pyramid, its layers, and what each is responsible for proving. Overriding principle:
**tests encode the spec** — every rule R-x.y and invariant INV-n has named tests citing it
(`test_R_5_4_execution_at_resting_price`, `prop_INV_6_quantity_conservation`), so spec
coverage is grep-auditable.

| Layer | Framework | Proves | Runs |
|---|---|---|---|
| Unit | GoogleTest | each rule/formula in isolation | every build, <5s |
| Property | GoogleTest + in-repo generators (S3) | invariants over generated scenarios | every CI run (bounded), nightly (extended) |
| Differential | same harness | FastBook/engine ≡ ReferenceBook (INV-15) | every CI run |
| Replay/determinism | harness + CLI | INV-10 at three strengths (in-process, cross-process, from-log) | every CI run |
| Accounting reconciliation | property suite | INV-11 identities over random runs | every CI run |
| Fuzz | libFuzzer | no crash/UB/invariant break on arbitrary bytes | nightly + local sessions; corpora in repo |
| Sanitizer matrix | ASan+UBSan (TSan when threaded) | memory/UB hygiene | every CI run (Linux), macOS ASan job |
| Statistical | pytest | flow validation metrics within tolerance (ORDER_FLOW_MODELS) | CI (wide tolerances; seeded — not flaky) |
| Binding | pytest | PYTHON_API contract, determinism through boundary, no leaks | every CI run |
| End-to-end | pytest | micro-experiments run→store→analyze→reproduce | every CI run |
| Performance regression | Google Benchmark + compare script | no silent slowdowns (METHODOLOGY.md rules) | smoke in CI (labeled unstable), authoritative on dev Mac |

## Layer notes

- **Unit:** table-driven where the spec is a table (validation order R-3.3, reject codes,
  tuple ordering). Accounting worked examples 1–2 are fixtures verbatim.
- **Property:** generators + invariant checkers per PROPERTY_TESTS.md. Failures shrink
  (generator supports prefix-minimization: rerun with binary-searched event-count prefix,
  then per-event deletion passes) and the shrunk scenario is saved as a scripted regression
  fixture — the corpus only grows.
- **Differential:** the engine templated on book type runs both books over the same generated
  streams; `dump_state()` compared after every message (CI) / every N (nightly long runs).
  Any mismatch auto-saves the scenario.
- **Concurrency tests:** none until R5 (no threads exist); then TSan runs + determinism-under-
  threading tests per THREADING_MODEL.
- **Statistical tests:** fixed seeds, tolerance bands sized for p < 10⁻⁹ false-failure under
  H0 (they detect broken generators, not sampling noise) — CI must never be flaky by design.
- **Python:** pytest with `hypothesis` for config-validation edges (bad tick strings, band
  violations) — hypothesis is fine Python-side (S3 constrains only the C++ side).
- **Coverage:** line/branch coverage tracked (llvm-cov) with a ratchet (no decrease vs main);
  targets: engine/book ≥ 95% line, overall ≥ 85% — but the real bar is spec-coverage
  (every R/INV cited by ≥1 test), checked by `scripts/spec_coverage.py` in CI.

## What is deliberately not tested

UI (R6 gets its own minimal contract tests), third-party libraries' internals, and
performance *numbers* in CI (only regressions-vs-baseline on the same runner, labeled
unstable; authoritative numbers come from the dev Mac per METHODOLOGY.md).
