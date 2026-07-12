# Opus Handoff

The master prompt for implementation sessions, and the completion-report format. Paste the
prompt (with the task ID filled) to start any implementation session.

---

## Master prompt

> You are implementing **MicroSim** task **<TASK-ID>** in the repository
> `MicroSim-Quant`. This project is specification-driven: every behavior you need is already
> decided and documented. Your job is faithful translation, not design.
>
> **Before writing any code:**
> 1. Read `docs/execution/IMPLEMENTATION_TASKS.md` — your task block — and every spec
>    document it cites. The conventions block at the top of that file applies to you.
> 2. Verify prerequisites: each prerequisite task's Definition of Done is actually met in the
>    tree (its tests exist and pass). If not, STOP and report.
> 3. Re-read `docs/domain/EXCHANGE_RULES.md` sections and `docs/domain/ENGINE_INVARIANTS.md`
>    entries relevant to your task.
>
> **Rules:**
> 4. Work on exactly this one task, on branch `task/<TASK-ID>-<slug>`. Nothing outside its
>    Scope; everything in its Excludes stays untouched. No opportunistic refactoring, no
>    drive-by fixes (file an issue note in the report instead).
> 5. Follow the documented public interfaces, naming conventions
>    (REPOSITORY_STRUCTURE.md), and invariants exactly. If the spec is ambiguous or two docs
>    conflict, STOP: record the question in the completion report under **Deviations/
>    Questions** and do not resolve it by choosing silently. Specs are amended by Fable +
>    owner (DECISIONS.md), then you resume.
> 6. Write tests before or alongside implementation. Test names cite rule/INV IDs. Never
>    weaken, skip, or delete an existing test to make anything pass; never mark tests
>    disabled. If a test seems wrong, that's a Deviation — STOP.
> 7. Run every command in the task's Verify line, plus `debug` AND `asan-ubsan` presets of
>    the affected test filters, plus `./scripts/format.sh --check` (and after R1-24, the
>    spec-coverage and layering scripts). Paste real output in the report — never summarize
>    a command you did not run, never invent numbers (benchmarks especially: report exactly
>    what the harness printed, with its manifest, or report "not run").
> 8. Determinism is law: no wall-clock reads, no unordered-container iteration reaching any
>    output, no unseeded randomness, no platform-dependent numerics (see SIMULATION_CLOCK
>    RNG rules).
> 9. Commit in small logical steps, messages referencing <TASK-ID>, co-author trailer per
>    repo convention. One PR per task.
> 10. When acceptance criteria are met and everything is green: STOP. Write the completion
>     report. Do not begin the next task — the sequence has review gates
>     (BUILD_SEQUENCE.md) and the next task starts only after review.
>
> **Definition of done** is in your task block plus the global DoD in
> IMPLEMENTATION_TASKS.md's conventions. Meeting it is necessary AND sufficient — gold-plate
> nothing.

---

## Completion-report format (posted as the PR description)

```markdown
## <TASK-ID>: <title> — Completion Report

**Status:** complete | blocked (reason)
**Branch/PR:** task/<TASK-ID>-<slug>

### What was built
2–6 bullets, plain language, citing the rule/INV IDs implemented.

### Acceptance criteria
- [x] each criterion from the task block, checked, with the evidence (test name / command)

### Verification output
<details> — pasted output of every Verify command (trimmed to the result lines, failures
never trimmed). Presets run: debug ✓ asan-ubsan ✓ (+ others per task)

### Tests added
table: test name → rule/INV covered → type (unit/property/fuzz/bench)

### Deviations / Questions
"None" or numbered items: doc section, the ambiguity/conflict, what is blocked on it.
(An item here means status: blocked unless explicitly scoped out by Fable.)

### Notes for reviewer
anything a reviewer should look at first; known non-blocking observations; issue notes for
out-of-scope problems found.
```

## Review loop (who does what)

1. Opus posts the report → owner reads it (plain-language section is for him).
2. [FABLE]-gated tasks: Fable reviews per BUILD_SEQUENCE before merge; other tasks: owner
   merges on green CI + report sanity.
3. Deviations: Fable proposes the spec amendment + DECISIONS.md entry → owner approves →
   docs updated → Opus resumes with the amended spec. **Code never leads spec.**
