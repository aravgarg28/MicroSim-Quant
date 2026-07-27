# Implementation Guide

How each task is executed and reviewed. Follow this for every task in
`IMPLEMENTATION_TASKS.md`. The project is specification-driven: every behavior is already
decided in the docs, so implementation is faithful translation, not design.

## Per-task checklist

**Before writing any code:**

1. Read the task block in `IMPLEMENTATION_TASKS.md` and every spec document it cites. The
   conventions block at the top of that file applies to every task.
2. Verify prerequisites: each prerequisite task's Definition of Done is actually met in the
   tree (its tests exist and pass). If not, stop and report.
3. Re-read the relevant sections of `docs/domain/EXCHANGE_RULES.md` and
   `docs/domain/ENGINE_INVARIANTS.md`.

**While implementing:**

4. Work on exactly one task, on branch `task/<TASK-ID>-<slug>`. Nothing outside its Scope;
   everything in its Excludes stays untouched. No opportunistic refactoring — note unrelated
   issues in the completion report instead.
5. Follow the documented public interfaces, naming conventions
   (`REPOSITORY_STRUCTURE.md`), and invariants exactly. If the spec is ambiguous or two docs
   conflict, **stop**: record the question under *Deviations/Questions* and do not resolve it
   by choosing silently. Specs are amended first (with a `DECISIONS.md` entry), then work
   resumes — code never leads the spec.
6. Write tests before or alongside implementation. Test names cite rule/INV IDs. Never weaken,
   skip, or disable an existing test to make something pass; if a test seems wrong, that is a
   deviation — stop.
7. Run every command in the task's Verify line, plus the `debug` and `asan-ubsan` presets of
   the affected test filters, plus `./scripts/format.sh --check` (and, after R1-24, the
   spec-coverage and layering scripts). Paste real output in the report — never summarize a
   command that was not run, and never invent benchmark numbers (report exactly what the
   harness printed, with its manifest, or report "not run").
8. Determinism is law: no wall-clock reads, no unordered-container iteration reaching any
   output, no unseeded randomness, no platform-dependent numerics.
9. Commit in small logical steps, messages referencing `<TASK-ID>`. One PR per task.
10. When the acceptance criteria are met and everything is green, **stop** and write the
    completion report. Do not begin the next task — the build sequence has review gates
    (`BUILD_SEQUENCE.md`), and the next task starts only after review.

**Definition of Done** is the task block plus the global DoD in `IMPLEMENTATION_TASKS.md`'s
conventions. Meeting it is necessary and sufficient — gold-plate nothing.

## Completion-report format (the PR description)

```markdown
## <TASK-ID>: <title> — Completion Report

**Status:** complete | blocked (reason)
**Branch/PR:** task/<TASK-ID>-<slug>

### What was built
2–6 bullets, plain language, citing the rule/INV IDs implemented.

### Acceptance criteria
- [x] each criterion from the task block, with the evidence (test name / command)

### Verification output
Pasted output of every Verify command (trimmed to result lines; failures never trimmed).
Presets run: debug, asan-ubsan (+ others per task).

### Tests added
table: test name → rule/INV covered → type (unit/property/fuzz/bench)

### Deviations / Questions
"None" or numbered items: doc section, the ambiguity/conflict, what is blocked on it.
(An item here means status: blocked unless explicitly scoped out.)

### Notes for reviewer
what to look at first; known non-blocking observations; issue notes for out-of-scope problems.
```

## Review loop

1. The completion report is posted as the PR description; the plain-language section is for a
   quick read.
2. Deep-review-gated tasks (see `BUILD_SEQUENCE.md`) get a thorough review before merge; other
   tasks merge on green CI plus a report sanity check.
3. Deviations: propose the spec amendment plus a `DECISIONS.md` entry, get it approved, update
   the docs, then resume with the amended spec. **Code never leads spec.**
