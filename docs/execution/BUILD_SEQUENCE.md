# Build Sequence

Exact implementation order with checkpoints. Checkpoint types:
**[HUMAN]** owner decision/approval · **[FABLE]** strong-model review required before
proceeding · **[CORRECT]** correctness gate (suites at stated budget) · **[PERF]**
measurement gate · **[RESEARCH]** research-validity gate · **[LEARN]** owner walkthrough —
Arav explains the just-built component back (interview prep is a deliverable, D2; added by
adversarial review AR-5).

## Release 1

```
R1-01 scaffold ──► R1-02 CI
        │
        ▼
R1-03 types ──► R1-04 messages ──► R1-05 config ──► R1-06 conversions/TOML
        │
        ├──────────► R1-07 RNG        [FABLE: sampler math]     ┐ parallel
        ├──────────► R1-08 clock/queue                          │ after R1-04/05
        └──────────► R1-09 ReferenceBook                        ┘
                          │
              [FABLE: line-by-line oracle review — hard gate]
              [LEARN: owner walks the book + rules docs]
                          ▼
R1-10 registry/gateway ──► R1-11 sequencer/log ──► R1-12 match-new
                                                      │
                                        [FABLE: match loop vs pseudocode]
                                                      ▼
                                     R1-13 cancel ──► R1-14 modify
                                                      │
                                        [FABLE: modify/FIFO semantics]
                                                      ▼
                                     R1-15 risk ──► R1-16 session-end
                                                      ▼
                                     R1-17 invariants ──► R1-18 generator/properties
                                                      │        [FABLE: profiles/shrinker]
                                                      ▼
                                     R1-19 FastBook ──► R1-20 differential
                                                      │
                          [CORRECT: nightly-budget property+differential run green]
                          [LEARN: owner explains differential testing]
                                                      ▼
                     R1-21 replay/CLIs ──► R1-22 scripts+fuzz ──► R1-23 benchmarks
                     (R1-21..23 interleavable)         │       [PERF: baseline recorded]
                                                      ▼
                                     R1-24 spec-coverage/nightly ──► R1-25 assembly
                                                              [FABLE: release audit]
                                                              [HUMAN: approve v0.1.0 tag]
                                                              [LEARN: full R1 walkthrough]
```

Parallelizable for a single implementer: {R1-07, R1-08, R1-09} after R1-05; {R1-21, R1-22,
R1-23} after R1-20. Everything else is chain.

## Releases 2–3 (checkpoint skeleton; task detail at release start per D16)

- **R2 gates:** [FABLE] accounting basis-algebra review before R2-06 · [CORRECT]
  consumer≡engine differential + INV-11 over nightly budget · [FABLE] pybind boundary review
  (lifetime/copy rules) · [LEARN] owner explains feed/consumer + P&L identities ·
  [HUMAN] v0.2.0.
- **R3 gates:** [FABLE] metrics definitions vs METRICS.md before collectors merge ·
  [RESEARCH] RQ2 pilot review — the maintainer + owner sign off variance/session-length/freeze
  *before* production seeds run (pre-registration is only real if enforced here) · [PERF]
  optimization PRs each carry before/after per METHODOLOGY · [FABLE+RESEARCH] RQ2 findings
  review vs the statistical-reviewer checklist · [LEARN] owner re-derives one bootstrap CI ·
  [HUMAN] v0.3.0 — **first resume-worthy tag; owner decides whether repo goes public here
  (O1)**.
- **R4 gates:** [FABLE] latency-model determinism + causality review · [RESEARCH] RQ1/RQ3
  pilot + findings reviews · [HUMAN] v0.4.0.
- **R5 gates:** [FABLE] threading evaluation review vs pre-registered bar (adopt/reject) ·
  [RESEARCH] replication-study review · [HUMAN] v0.5.0.
- **R6 gates:** [HUMAN] timebox start/stop decisions · demo-script live rehearsal ·
  [HUMAN] v1.0.0.

## Standing rules

1. One task in flight at a time (OPUS_HANDOFF rule 2); a task starts only when prerequisites'
   DoD is met.
2. A [FABLE] gate blocks its successors — no "provisional continue".
3. [LEARN] checkpoints are scheduled work, not vibes: the owner walkthrough happens before
   the next release starts, using the interview guide's relevant section as the script.
4. Any deviation discovered mid-task (spec ambiguity, doc conflict) stops the task and goes
   to the deviation protocol (OPUS_HANDOFF §deviations) — the sequence resumes after the
   decision is logged.
