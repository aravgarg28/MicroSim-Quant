# Benchmark Methodology

The rules that make a number citable. If a measurement violates any rule here, it may guide
work informally but may not appear in the README, findings, resume bullets, or comparisons.

## Environment

1. **Hardware disclosure:** every stored result carries chip model, core config
   (P/E cores), RAM, OS version, compiler + version + flags, build preset, power source
   (Apple Silicon throttles on battery — AC required), and thermal note (fanless machines:
   cold-start required between long suites).
2. **Authoritative platform:** the dev Mac, native (D10). CI numbers exist for regression
   trends only and are labeled `unstable-runner` in all outputs. **Mac and CI numbers are
   never compared to each other**; no cross-machine, cross-compiler, or cross-flag comparison
   is valid, ever.
3. **Quiet-machine protocol (Mac):** AC power, closed apps, Spotlight indexing settled,
   `caffeinate` held, no browser, benchmark process at default QoS (documented; macOS lacks
   practical core pinning — this limitation is stated rather than papered over; run-to-run
   spread is measured and reported instead).

## Execution

4. **Build:** Release preset (`-O2 -DNDEBUG`, LTO on, sanitizers off, invariant hooks
   compiled out). Any deviation (e.g. stage-counter builds for breakdowns) is labeled.
5. **Warm-up:** each benchmark discards until steady state (Google Benchmark auto + explicit
   minimum 3 warm-up iterations for macro runs).
6. **Repetitions:** microbenchmarks: ≥ 20 repetitions (process-internal), plus the whole
   suite run 3× interleaved (A,B,C,A,B,C — catches drift); macro: ≥ 10 runs. Reported spread:
   IQR across repetitions; a result whose p50 IQR exceeds 5% is re-run on a quieter machine
   state or reported with the spread highlighted.
7. **Percentiles need samples:** p99.9 is only reported where iterations ≥ 10⁵ per repetition
   (else the table cell says n/s — not supported — rather than a fabricated tail).
8. **Clock:** `steady_clock` / Google Benchmark's chosen monotonic source; timer overhead
   measured once per suite and stated when it exceeds 1% of the measured op.
9. **Outliers:** never deleted. Tails *are* the product in latency work; "outlier handling"
   means reporting max and p99.9, not trimming them.

## Analysis and comparison

10. **Baselines are stored, not remembered:** comparisons run against committed JSON from the
    same machine + flags (`results/benchmarks/`), via `bench_compare.py` (noise floor: the
    empirical run-to-run IQR of that benchmark; changes within floor are "no change").
11. **Claims match measurements:** "2.1× faster cancel path (p50 41ns → 19ns, N=20 reps,
    IQR 2%)" — never "much faster". Optimization PRs embed the before/after table
    (OPTIMIZATION_ROADMAP.md protocol).
12. **No derived marketing units.** Messages/sec and ns/op only; no "billions of
    instructions", no extrapolated "per day" figures.

## Statistical summary format

Per benchmark: p50, p95, p99 (p99.9 per rule 7), max, mean±IQR across repetitions,
allocs/op, iterations, repetitions. JSON schema in EXPERIMENT_STORAGE.md (benchmarks table).

## The honesty rule

Every performance statement in any project document links (or footnotes) the JSON it came
from. A number without a manifest is treated as false. This is the performance mirror of the
research pre-registration discipline — and the answer to the interview question "how do I
know your benchmarks are real?"
