# Fuzzing Plan

libFuzzer targets (S4), each a `LLVMFuzzerTestOneInput` linked with ASan+UBSan. Fuzzing
complements property tests: properties explore *plausible* streams intelligently; fuzzing
explores *implausible* bytes relentlessly.

## Targets

| Target | Input mapping | Oracle |
|---|---|---|
| `fuzz_gateway` | bytes → decoded as a packed array of raw inbound message structs (arbitrary field values) → engine | no crash/UB; invariants hold after every message; every message → exactly one ack/reject |
| `fuzz_match_stream` | structured mapper: bytes consumed as (action nibble, params) grammar → valid-ish message stream (higher depth than raw) | invariants + differential vs reference on the same stream |
| `fuzz_modify_cancel` | grammar weighted to modify/cancel edge cases on a pre-seeded book | invariants; R-6/R-7 postconditions |
| `fuzz_md_consumer` | bytes → MD message stream (including corrupt seq, torn batches, bogus snapshots) → ConsumerBook | no crash; GAPPED entered iff gap; recovery converges after valid snapshot |
| `fuzz_persist_reader` | bytes → event-log/manifest parser | no crash; corrupt input → clean error (never partial silent success) |
| `fuzz_config` | bytes → TOML string → config validator (C++ side) | no crash; reject with field path or accept-and-revalidate idempotently |
| `fuzz_python_boundary` (pytest-side, atheris optional/skippable) | dict/string mutations → SimulationConfig | ConfigError or success; no segfault in the extension |

The two stream fuzzers reuse the property suite's invariant checkers wholesale — fuzzing
inherits every INV oracle for free.

## Corpora and regression

- Seed corpora: scripted scenarios + shrunk property-test failures, checked into
  `tests/fuzz/corpus/<target>/`.
- Every fuzz finding is minimized (`-minimize_crash`), fixed, and committed as both a corpus
  entry and (for stream targets) a scripted regression test. Corpus-only bugs are not "fixed"
  until a named test exists.
- Dictionaries: message-type bytes, boundary prices/qtys (band edges, INT64 extremes near
  the overflow analysis bounds).

## Schedule

- **CI (per PR):** each target 60s on the merge commit (smoke — catches gross regressions).
- **Nightly:** 30 min/target, corpus-merged and committed back weekly.
- **Local deep runs:** before each release tag, ≥ 4h on `fuzz_gateway` +
  `fuzz_match_stream` on the dev Mac (`-jobs` = cores).
- Coverage-guided sanity: `llvm-cov` on corpus replay must show the matching loop, all reject
  paths, and both modify branches covered — gaps get dictionary/mapper attention.

## Out of scope

Differential fuzzing across compilers, structure-aware protobuf mutators, OSS-Fuzz
onboarding — noted as extensions; the seven targets above are the credible core.
