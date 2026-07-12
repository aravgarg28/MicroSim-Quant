# Optimization Roadmap

Ordered optimization program. Iron law: **no optimization without (a) a profile naming the
bottleneck, (b) a stored before-benchmark, (c) an after-benchmark on identical conditions,
(d) green differential tests.** Optimizations that don't pay are reverted and the negative
result recorded (a "tried, didn't pay" table is planned in the findings — negative results
are results here too).

## Phase 0 — Correctness baseline [R1, mandatory before anything]
All invariants green, differential green, determinism green. Record the **unoptimized
baseline** benchmark JSON — the denominator for every later claim.

## Phase 1 — Profile [R3 opening]
Instruments time profile + allocation profile on `macro_full_stack` and the top
microbenchmarks. Deliverable: a written profile note (`results/benchmarks/profiles/`)
naming the top-5 time and allocation sites. Hypotheses ranked *by profile share*, not by
what's fun.

## Phase 2 — Allocation removal
Counting-hook report → drive steady-state engine-path allocs/msg to 0 (MEMORY_MODEL.md
targets): pre-reserved slabs, fixed event structs, pre-sized queue storage, log-writer owned
buffers. Gate: `bm_*` allocs/op = 0 columns; CI smoke asserts it stays 0.

## Phase 3 — Memory layout
Order struct hot-first layout + size assert; level array layout; cold-field split (SoA) only
where the profile shows cache misses in book walks (Linux perf trend + Instruments
corroboration, since Mac counters are limited).

## Phase 4 — Price-level lookup
FastBook stage 2 (ORDER_BOOK_DESIGN.md): intrusive FIFO links, open-addressing id table,
occupancy-bitmap best-scan. Each of the three lands as its own PR with its own before/after
(they are separable; if one doesn't pay on our workload, it goes to the tried-didn't-pay
table).

## Phase 5 — Branch behavior
Only with evidence (Linux branch-miss trends): reorder validation early-outs by measured
frequency, `[[likely]]`/`[[unlikely]]` on reject paths, side-templated matching loop to hoist
the buy/sell branch. Expected small; measured anyway — an honest "this bought 2%" is fine.

## Phase 6 — Pooling and reuse review
Event-queue node pooling, log-buffer recycling — whatever Phase 1's *updated* profile (re-run
after 2–5) still shows. Re-profiling before this phase is the point: the first profile's
truths are stale by now.

## Phase 7 — Threading evaluation [R5]
Per THREADING_MODEL.md's pre-registered bar. Either adoption (with TSan + determinism
machinery) or documented rejection with the scaling table.

## Phase 8 — Re-baseline and tell the story
Full suite on the release preset; README performance section updated: baseline → final table
per benchmark, cumulative speedup attribution per phase (honest partials: phases interact;
attribution states method), tried-didn't-pay table, and the profile notes linked.

## Standing candidate list (menu for profiles, never a to-do list)

Arena for consumer books · `mmap` log writer · SIMD level scans (`std::simd`) · huge pages
(Linux-only) · PGO builds · `__builtin_prefetch` in book walks · custom hash (id table) ·
branchless clamp in latency composition. Each stays parked until a profile names it.

## Anti-goals

No optimization of: config parsing, session setup/teardown, Python export (unless
`macro_pyboundary` shows > 5% of experiment wall time), reference book (by definition), test
runtime (within CI budgets). Fast where it matters, plain everywhere else.
