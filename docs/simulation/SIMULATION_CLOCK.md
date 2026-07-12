# Simulation Clock

The discrete-event core: one logical clock, one event queue, total deterministic ordering.

## Time domains (kept rigorously distinct)

| Time | Meaning | Where |
|---|---|---|
| **Logical time** `SimTime` (int64 ns from 0) | the simulation's single authoritative clock | everywhere in-sim |
| Event time | logical time at which an event *fires* (is delivered) | queue key |
| Creation time | logical time when the event was scheduled | audit field |
| Exchange time `ts_event` | logical time when the engine processed a message | R-10.2, on all outputs |
| Participant observation time | logical time a message reaches that participant (creation + latency) | per DATA_FLOW step 8 |
| Wall-clock time | real time | **benchmarks and run metadata only** — never in-sim |

There is no "strategy-observation lag" hidden anywhere: observation time = delivery time from
the latency model, period. If a strategy wants to model its own compute time, that is an
explicit `decision_latency` parameter scheduling its intents later — visible in config, never
implicit.

## Event queue and total ordering

A binary min-heap keyed by the tuple:

```
(fire_time, priority_class, insertion_seq)
```

- `fire_time`: SimTime ns.
- `priority_class` (uint8): resolves *simultaneous* events in a fixed domain order:
  `0 = EngineInternal (session transitions)`, `1 = InboundToExchange`,
  `2 = OutboundDelivery (MD + private reports)`, `3 = ParticipantWakeup (timers)`.
  Rationale: at equal timestamps, the exchange finishes acting before anyone hears about it,
  and deliveries land before participant timers fire (so a timer at time t sees everything
  delivered at t).
- `insertion_seq` (uint64, global, monotonic): final tiebreak = creation order. Two orders
  sent "simultaneously" by two agents arrive in the order their intents were generated, which
  is itself deterministic because agent callbacks run in deterministic order.

The triple is unique per event ⇒ the heap's comparison is a strict total order ⇒ **pop order
is deterministic regardless of heap implementation details.** This single property carries
INV-10.

## Scheduling rules

- `schedule(event, fire_time ≥ now)` — scheduling into the past asserts (architecture rule).
- Zero-delay scheduling is legal (fire_time = now) and lands after currently-firing
  same-time-class events per insertion_seq — used by zero-latency MVP configs.
- The loop: pop → advance `now` to fire_time → dispatch to owner module → repeat until queue
  empty or session end + drain.

## Seed management and RNG streams

- One **master seed** (uint64) per run, recorded in the manifest.
- Every consumer gets a **named stream**: `rng("flow")`, `rng("latency/P3")`,
  `rng("agent/noise_taker_1")`, … Stream state = counter-based PRNG (Philox-style or
  SplitMix64-seeded PCG per stream) keyed by `hash(master_seed, stream_name)`.
- **Property this buys:** adding, removing, or reordering one consumer never changes any other
  stream's draws — experiments stay comparable across code versions, and A/B arms can share
  the *identical* flow realization (common random numbers, see EXPERIMENT_PLAN.md) by sharing
  the `flow` stream while strategies differ.
- Drawing is deterministic and platform-stable: integer algorithms only; any distribution
  sampling (exponential, normal) uses our own fixed inverse-CDF/ziggurat implementations, not
  `std::normal_distribution` (whose output is implementation-defined across standard
  libraries — a classic cross-platform determinism trap, worth telling in interviews).

## Replay interaction

Replay mode (DATA_FLOW.md) seeds nothing: it feeds the recorded sequenced input log with
original `ts_event`s straight to the gateway. The clock still runs (for outbound latency
delivery if configured) but all inbound nondeterminism is gone by construction.

## Tests

- Unit: tuple-ordering table tests (every class pair at equal/unequal times); insertion-seq
  tiebreak; past-scheduling assert.
- Property: random event soups delivered in sorted-tuple order; INV-10 byte-identical reruns.
- RNG: stream independence (removing stream A leaves stream B's sequence unchanged);
  cross-platform golden-value tests for each distribution sampler (macOS vs Linux CI must
  produce identical draws).
- Benchmarks: schedule/pop throughput at queue depths 10³–10⁶ (this queue is on every event's
  path — see BENCHMARK_PLAN.md).
