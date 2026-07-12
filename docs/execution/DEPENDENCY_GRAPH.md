# Dependency Graph

## Epic-level graph

```mermaid
flowchart TD
    E01[E01 Foundation] --> E02[E02 Core types]
    E02 --> E03[E03 Determinism kit]
    E02 --> E04[E04 Reference book]
    E02 --> E05[E05 Exchange pipeline]
    E04 --> E05
    E03 --> E05
    E04 --> E06[E06 Correctness harness]
    E05 --> E06
    E06 --> E07[E07 Fast book]
    E05 --> E08[E08 Replay & scripts]
    E07 --> E09[E09 Benchmarks]
    E05 --> E10[E10 Market data]
    E05 --> E11[E11 Accounting]
    E11 --> E12[E12 Risk completion]
    E10 --> E13[E13 Flow & agents]
    E03 --> E13
    E11 --> E14[E14 Python bindings]
    E13 --> E14
    E14 --> E15[E15 Experiment framework]
    E10 --> E16[E16 Strategies]
    E12 --> E16
    E16 --> E17[E17 RQ2 experiment]
    E15 --> E17
    E09 --> E18[E18 Optimization 1]
    E17 --> E18
    E10 --> E19[E19 Latency lab]
    E16 --> E19
    E19 --> E20[E20 RQ1+RQ3]
    E15 --> E20
    E13 --> E21[E21 Advanced flow]
    E20 --> E21
    E18 --> E22[E22 Multi-instrument/threading]
    E21 --> E22
    E20 --> E23[E23 Dashboard]
    E22 --> E23
    E17 -.-> E24[E24 Presentation]
    E20 -.-> E24
    E23 -.-> E24
```

## Parallelizable tracks (single-implementer view: "parallel" = interleavable without rework)

- **After E02:** E03 (RNG/clock), E04 (reference book), and E02's config-parsing tail are
  mutually independent.
- **After E05:** E06 (harness) is the critical path; E08 (replay/scripts) and the persist
  writer can interleave.
- **R2:** E10 (MD) and E11 (accounting) are independent of each other; E13 needs E10; E14
  needs E11+E13.
- **R3:** E16 (strategies) and E15 (experiment framework) proceed in parallel until E17
  joins them. E18 (optimization) must trail E17's pilot (profiles need realistic load) but
  precedes production RQ2 runs only if it lands cleanly — otherwise RQ2 ships on stage-1
  FastBook (results don't care about ns).
- **R4:** E19's latency model and MBO feed are separable subtracks; both precede E20.

## Critical path to first resume-worthy state (R3)

E01 → E02 → E05 → E06 → E07 → E10/E11 → E13/E14 → E16 → E17.
Slack lives in: E08 (replay can trail), E09 (benchmarks can trail until E18), E18 itself
(RQ2 doesn't need it), S3/S4/S5 strategies (S1/S2 suffice for RQ2).

## Rule

A task may start only when its prerequisites' **Definition of Done** is met — not merely
"code exists" (IMPLEMENTATION_TASKS defines DoD per task; OPUS_HANDOFF makes Opus verify
prerequisites before starting).
