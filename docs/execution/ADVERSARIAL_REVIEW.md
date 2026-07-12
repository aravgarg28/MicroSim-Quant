# Adversarial Review (Phase 17)

Five personas attacked the completed plan before it was declared final. Findings are numbered
AR-n; each states the attack, the verdict, and the revision applied (or why none was needed).
Revisions were applied to the docs in the same commit series as this file.

## Persona 1 — Quantitative-developer interviewer

- **Attack: "The owner can talk about this but can he code it live?"** Valid — the plan
  optimized for explainability, but interviews demand live coding of book operations.
  **AR-1 (revised):** INTERVIEW_GUIDE study plan gains a mandatory week-4 exercise:
  hand-implement a toy order book from memory, diff against ReferenceBook.
- **Attack: "Too broad — 40 docs, 6 releases, one new grad."** Partially valid. Defense
  already in the plan: R3 is a complete, presentable stopping point (engine + benchmarks +
  one rigorous experiment); everything after is additive. Verdict: acceptable **because**
  the roadmap's release gates are real exits, and D11 (no deadline) removes schedule
  pressure. No revision; risk logged in the final summary.
- **Attack: "Vague tasks hiding in the list?"** Audited R1-01..25 against the banned-vagueness
  rule: each has scope/excludes/verify. Weakest: R1-25 "assembly" — acceptable as a
  checklist-audit task with named criteria. No revision.

## Persona 2 — Low-latency C++ engineer

- **Attack: "Zero-alloc and latency numbers on macOS without core pinning or perf — theater?"**
  Countered by design: METHODOLOGY discloses the limitation, measures run-to-run spread
  instead of pretending control, bans cross-machine comparison; allocation claims are
  enforced by a counting hook, not vibes. Verdict: honest within stated bounds. No revision.
- **Attack: "Fill markouts computed in-engine (fills.parquet markout columns) require future
  mids — where's that design?"** Correct catch: naive implementation would either look ahead
  (invalid) or block. **AR-2 (revised):** R3-05 task carries the design note — collectors
  buffer fill records until each horizon's mid is known, emitting complete rows at horizon
  expiry; session end flushes with censoring flags.
- **Attack: "SPSC/bitmap/intrusive lists are listed — resume-driven?"** Each is gated behind
  a measured phase (OPTIMIZATION_ROADMAP) with a tried-didn't-pay table for failures.
  Verdict: discipline present. No revision.

## Persona 3 — Market-microstructure researcher

- **Attack: "RQ1 measures 'adverse selection' in a market with no informed traders — the
  headline would overclaim."** Valid and important. **AR-3 (revised):** RQ1 gains an explicit
  validity-bound paragraph (mechanical-only adverse selection under Poisson flow; horizon-
  flatness prediction; R5 informed-flow replication as the amplification test), required in
  the findings headline, not a footnote.
- **Attack: "Flow calibration is invented — 'internal plausibility' is doing heavy lifting."**
  True and unavoidable without licensed data; already disclosed in ORDER_FLOW_MODELS +
  mandatory sensitivity analysis + named frozen scenarios. Verdict: honest; the plan never
  claims external calibration. No revision.
- **Attack: "Self-trades allowed in R1 pollute early metrics."** Checked: no research metrics
  are produced in R1 (experiments start R3, STP lands R2). No revision; noted so nobody
  backports R1 runs into findings.

## Persona 4 — Statistical reviewer

- **Attack: "n_seeds=200 is asserted, not justified."** Countered: EXPERIMENT_PLAN's pilot
  phase sizes n via variance stabilization check before freezing; 200 is a budget ceiling,
  not a claim. No revision.
- **Attack: "Bootstrap CIs + Wilcoxon + Holm — fine, but summary stats can hide bimodality
  (e.g. KILL-switch bifurcation in RQ1's slow arms)."** Valid. **AR-4 (revised):**
  EXPERIMENT_PLAN now requires seed-level scatter/violin alongside every headline comparison;
  kill/PASSIVE episode rates were already dependent variables, which handles the mechanism.
- **Attack: "CRN pairing breaks if strategies alter the flow arms differently (flow is
  state-anchored!)."** Sharp: placement anchors to the book, which the strategy affects, so
  arms diverge behaviorally even with identical draws. Verdict: this is inherent to closed-
  loop simulation and is exactly why paired-design validity is *tested* (identical-strategy
  arms ⇒ zero diffs) and why share-of-volume guards exist. The pairing still isolates
  treatment causally (same seed = same exogenous randomness); divergence *is* the treatment
  effect propagating. Documented here; no doc revision needed.

## Persona 5 — Skeptical hiring manager

- **Attack: "AI built it; the candidate is a passenger."** The plan's whole shape answers
  this (decision log, per-checkpoint approvals, interview guide, [LEARN] gates), but the
  gates existed only implicitly. **AR-5 (revised):** BUILD_SEQUENCE adds [LEARN]
  owner-walkthrough checkpoints as scheduled work before each release closes.
- **Attack: "Private repo, no code yet, all docs — where's the proof of momentum?"** Fair;
  answered by process: commits are frequent and structured, and the public-repo decision is
  tied to v0.3.0 evidence ([HUMAN] gate, O1). No revision.
- **Attack: "Dashboard will eat the project."** Hard-gated (D13, R6 timebox, demos must work
  without it). No revision.

## Cross-cutting audit answers (Phase 17 checklist)

Too broad? — bounded by real exit at R3. MVP feasible? — R1 is 25 sized tasks, all chained
gates. Rules ambiguous? — every rule numbered + arbiter clause + deviation protocol.
Simulation realistic enough for stated conclusions? — conclusions pre-bounded per RQ
(and AR-3 tightened RQ1). Look-ahead? — structural (linker-enforced boundaries + INV-16) +
AR-2 fixed the one metrics-side hole. MM conclusions flow-dependent? — disclosed + sensitivity
+ replication. Concurrency premature? — deferred behind a pre-registered bar. Optimization
claims measurable? — manifest-or-it's-false rule. Python/C++ boundary sensible? — coarse,
copy-out, tested. Tests cover invariants? — spec-coverage script makes it auditable. Skills
relevant to quant hiring? — the interview guide maps each doc to a question class. UI
distraction? — gated. Tasks small enough? — S/M/L with split rule. Would another model need
to invent decisions? — the deviation protocol exists precisely so the answer stays no.
