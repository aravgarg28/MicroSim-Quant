# Model Allocation

Which model does what, chosen by leverage: the expensive model where judgment compounds, the
capable model where implementation is hard, the efficient model where the work is mechanical.

## Fable (architecture & judgment — highest cost, lowest volume)

- Specification authorship and every spec amendment (deviation protocol) + DECISIONS.md.
- The [FABLE] gates in BUILD_SEQUENCE: reference-book line-by-line review; match-loop and
  modify-semantics reviews; RNG/sampler math; property-profile/shrinker design; accounting
  basis algebra; pybind boundary; metrics definitions; latency causality; threading
  adopt/reject; every research pilot + findings review; release audits.
- Mathematical/statistical review of anything with an equation in it.
- Adversarial reviews and the final pre-public-repo review.
- NOT for: writing tests, YAML, fixtures, or any code volume — judgment only.

## Opus (implementation — the workhorse)

- All C++ implementation tasks (engine, books, sim, strategies, metrics, persist).
- pybind11 bindings and the Python experiment framework.
- Property/differential/fuzz *infrastructure* (the generator and harnesses are subtle code).
- Debugging anything, at any layer; performance work with the measurement discipline.
- Complex test suites (the R-7.2 matrix, INV checkers, statistical validation tests).

## Sonnet (mechanical volume — cheapest per token, used deliberately)

- CI YAML, formatting configs, `.gitignore`-class files (R1-02 pattern).
- Utility scripts from precise specs (bench_compare, spec_coverage skeletons — Opus reviews
  plumbing that gates CI).
- Documentation assembly (README section stitching, findings-doc formatting from computed
  tables), docstrings, `.pyi` cleanup.
- Config fixtures, scenario TOML authoring from tables, notebook boilerplate.
- Dashboard (R6) CRUD-grade code — FastAPI routes and React tables are Sonnet-grade;
  Opus only for the replay-cursor sync logic.

## Escalation and de-escalation rules

1. Sonnet task that turns out to have a design question → escalate to Opus; Opus task that
   hits a spec gap → escalate to Fable via the deviation protocol. Never resolve upward
   ambiguity at the cheaper tier.
2. Repetitive remainder of an Opus task (e.g. 30 near-identical rule tests after the pattern
   is set) → hand the pattern + list to Sonnet.
3. Owner review effort follows the same gradient: Sonnet output gets a skim, Opus output gets
   the completion-report read + spot checks, [FABLE]-gated merges wait for the review.

## Cost posture

The plan front-loads Fable (specs, now done) so the long tail is Opus-heavy with Sonnet
offload. If budget tightens: Sonnet's list is the flexible zone (owner can do assembly work
by hand); the [FABLE] gates are not — skipping oracle/statistics review is how the project's
two biggest risks (silent spec drift, invalid research) materialize.
