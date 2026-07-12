# Threading Model

**Decision: the simulation core is single-threaded through Release 4.** Parallelism exists
from Release 2 anyway — at the *process* level, where it is free of risk: experiment sweeps run
N independent simulations in N processes. Intra-simulation threading is evaluated in Release 5
against measurements, with one-thread-per-instrument as the only candidate design.

This ordering (correctness → determinism → measured parallelism) is deliberate and is the
right interview answer: exchanges themselves serialize matching per instrument, so a
single-threaded matcher is not a simplification — it is the domain's actual shape.

## Why single-threaded first

1. **Determinism is a product requirement** (INV-10). A deterministic multithreaded discrete-
   event simulation requires conservative synchronization (e.g. lockstep time barriers) that
   typically *loses* to a well-written single-threaded loop at MicroSim's scale.
2. **The workload is causally serial.** Every message's effect depends on the book state left
   by the previous message (R-5.1). Within one instrument there is no exploitable parallelism
   in matching — only pipeline-style overlap with tiny stages and expensive handoffs.
3. **Research throughput scales embarrassingly at the run level.** 1,000 seeded runs on 8
   cores = 8 single-threaded simulations at once, zero synchronization, perfect determinism.
   This is where the actual research compute goes.

## Alternatives compared

| Design | Throughput | Determinism | Complexity | Verdict |
|---|---|---|---|---|
| **Single-threaded event loop** | one core, but zero sync overhead; cache-hot | trivial | low | **R1–R4 choice** |
| **Process-per-run sweeps** | linear in cores across runs | perfect (per run) | trivial | **R2+ choice for research** |
| **One thread per instrument** | linear in instruments (if flow is independent) | preserved: each instrument's stream stays serial; cross-instrument order is irrelevant when flows are independent | moderate: SPSC queues in, per-thread engines, result merge | **R5 candidate — adopt only if measured** |
| Shared worker pool over messages | poor: per-message locking or reordering | broken without heavy machinery (must re-serialize per book) | high | rejected |
| Lock-based shared engine | negative scaling likely (contended book mutex) | fragile | moderate | rejected |
| Actor framework (e.g. one actor per module) | overhead per hop dominates µs-scale work | achievable but effortful | high + dependency | rejected |
| Lock-free MPMC everything | impressive-sounding; solves a problem MicroSim doesn't have | hard | very high | rejected (résumé-driven design) |

## Release-5 candidate design (pre-specified so evaluation is honest)

- **Thread ownership:** main thread owns config, lifecycle, result merge. Each instrument
  shard thread owns: its event queue slice, engine, book, publisher, that instrument's
  participants' per-instrument state. Accounting per participant is sharded per instrument and
  merged at barriers (participants trading N instruments are the complication — and the reason
  this may be rejected).
- **Communication:** SPSC ring buffer per direction per shard (bounded, power-of-two capacity,
  cache-line-aligned head/tail — see `MEMORY_MODEL.md` false-sharing notes).
- **Synchronization:** only the ring buffers (acquire/release); no locks in steady state.
- **Backpressure:** bounded queues; producer blocks (simulation time, not wall time, so
  blocking is just scheduling) — no drops, ever.
- **Shutdown:** poison-pill message per queue after session-end events drain; join all; merge.
- **Determinism condition:** cross-instrument independence. If Release 5 introduces
  cross-instrument logic (portfolio risk checks across books), sharding must either serialize
  those checks at barriers or be abandoned. This condition is written down *now* so the later
  evaluation can't rationalize.

## Data-race and false-sharing policy

- TSan CI job becomes mandatory the moment any `std::thread` enters a shipping target.
- All cross-thread data goes through the ring buffers by value; no shared mutable objects.
- Queue head/tail indices padded to 64-byte cache lines (`alignas(std::hardware_destructive_interference_size)`),
  benchmarked with and without padding to *show* the false-sharing effect rather than assert it.

## Benchmark methodology for the R5 decision

Adopt threading only if, on the disclosed Mac hardware, per
`docs/performance/METHODOLOGY.md`:

1. Baseline: single-threaded events/sec on N instruments (N = 1, 2, 4, 8), same total flow.
2. Sharded: same workloads, one thread per instrument (pinned where the OS allows).
3. Adoption bar: ≥ 1.7× total throughput at N = 2 and ≥ 3× at N = 4, with byte-identical
   per-instrument event streams vs the single-threaded run, TSan clean.
4. Either outcome is documented with the measurements. **A measured rejection is a valid and
   publishable result** — "we measured, sharding bought 1.2× for 3× complexity, we declined"
   is a stronger systems story than unexamined threads.
