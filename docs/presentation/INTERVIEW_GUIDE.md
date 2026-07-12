# Interview Guide

How to present MicroSim in interviews, question by question. Format: the question you'll get,
the answer's spine, and the depth-follow-ups with where to go. Practice these out loud; the
spine sentences are designed to be said, not read. Cross-references point to the doc that
carries the full argument — re-read it the night before.

## The 90-second project summary (memorize the beats)

"MicroSim is a deterministic exchange simulator plus a market-making research platform I
specified end-to-end and built with a spec-driven AI workflow. Core is a C++20 price-time-
priority matching engine — exact integer arithmetic, zero-allocation steady state, benchmarked
at [measured numbers] — wrapped in a discrete-event simulation with a configurable latency
model. Correctness comes from 17 machine-checked invariants, property tests, and differential
testing against a deliberately slow reference book. On top: a Python research layer that ran
three pre-registered experiments — latency vs market-maker performance, inventory-skewed vs
fixed-spread quoting, and queue position vs fill probability — with paired designs, common
random numbers, and bootstrap CIs. Everything replays byte-identically from a seed."

## Why-questions

**Why is this relevant to a quant firm?**
Spine: it exercises the exact intersection the job is — performance-sensitive C++ around
market mechanics, with statistical honesty about results. Every design decision is logged
with alternatives (DECISIONS.md); the firm's daily work is making these tradeoffs.

**Why C++? Why Python too?**
Spine: C++ where latency and layout matter (the engine is allocation-free and cache-conscious
by measurement); Python where iteration speed matters (analysis, orchestration). The boundary
is coarse — configure/run/retrieve — because a per-event boundary would cost more than the
engine itself. → PYTHON_API.md.

**Why simulate instead of using real data?**
Spine: the research questions need *counterfactuals* — same market, different latency —
which recorded data cannot give (replayed flow doesn't react to you; impact is wrong by
construction). Simulation trades realism for internal validity, and the docs are explicit
about which conclusions that supports. → ORDER_FLOW_MODELS stage 5, RESEARCH_QUESTIONS.

## Data structures & algorithms

**Walk me through your order book.**
Spine: two implementations. Reference: `std::map` of price → FIFO list, written for
obviousness — it's the differential-testing oracle. Fast: array of levels indexed by
`(price − min)/tick` — O(1) lookup with no search because instruments declare a price band;
intrusive doubly-linked FIFOs in a slab (cancel = O(1) unlink); occupancy bitmap +
`countr_zero` to find the next non-empty level when the best empties. Know cold: the
complexity table (ORDER_BOOK_DESIGN.md), why cancels dominate real flow, and the memory math
(4,000 levels × 32B = 128KB/side, L2-resident).
Follow-ups: "what if the band is wrong?" (reject at the boundary — it's a market rule, R-1.1);
"deep vs sparse books?" (bitmap bounds the gap scan); "why not a heap?" (stale-entry problem,
no ordered iteration).

**Why intrusive lists?**
Cancel needs O(1) removal given an order handle; non-intrusive lists need an extra
node allocation and a pointer indirection per order. Intrusive links live inside the order
struct in the slab — no allocation, better locality. Cost: intrusive structures couple
lifetime to the container discipline — which the slab's single-owner rule provides anyway
(MEMORY_MODEL.md).

## Correctness & determinism

**How do you know the engine is correct?**
Spine, in order: (1) rules written before code, every rule ID cited by a named test;
(2) 17 invariants checked after every message in test builds; (3) property tests over
state-aware generated scenarios; (4) differential testing — fast engine vs independent slow
oracle, *event-stream* equality not just end-state; (5) fuzzing with the same invariant
oracles; (6) sanitizers with a zero-suppression policy. The differential test is the crown:
correctness by agreement of independent implementations across millions of scenarios.

**How is it deterministic? Why do you care?**
Spine: single logical clock; total event ordering by (time, priority-class, insertion-seq);
named counter-based RNG streams per consumer; no wall clock, no unordered-container iteration
in any output path; and platform-stable samplers — `std::normal_distribution` produces
different sequences on libc++ vs libstdc++, so we ship our own. Care because: replay
debugging (any bug reproduces from a seed), CRN experiments (paired arms see identical
markets), and CI-verifiable byte-identical reruns. → SIMULATION_CLOCK.md.

**Prices as integers — why and how?**
`0.1 + 0.2 ≠ 0.3`; accounting that reconciles to the exact cent is only possible in integers.
Price = int64 ticks, cash = int64 minor units, strong types so Price+Qty doesn't compile;
fees flat per-lot so no rounding exists anywhere; the single rounding rule in the system is
the session-end half-tick mark, and I can point at it. → NUMERIC_REPRESENTATION.md.

## Concurrency

**Why is your engine single-threaded? / How would you scale it?**
Spine: matching within an instrument is causally serial — real venues serialize it too. A
message's effect depends on the book the previous message left; threads inside one book buy
synchronization costs, not throughput. Parallelism lives where it's free: N processes × N
seeds for research, and (evaluated in R5) one-thread-per-instrument sharding with SPSC rings
— with a pre-registered adoption bar so the evaluation can't rationalize. If asked to design
it live: shard by instrument, SPSC in/out per shard, merge deterministic per-shard streams.
→ THREADING_MODEL.md. The honest kicker: "I pre-committed to rejecting threads if they
measure under 1.7× on two instruments — a measured no is a result."

## Performance

**What did you optimize and what did it buy?**
Answer from the Phase-8 table (numbers + attribution + the tried-didn't-pay list). Structure:
baseline → profile → allocation removal → layout → book stage-2 → each with before/after.
Never claim an unmeasured win; the tried-didn't-pay table is the credibility flex.

**How do I know your benchmarks are real?**
Methodology doc written before numbers existed; every number links a JSON manifest with
hardware/flags/repetitions; outliers reported not trimmed; no cross-machine comparisons.
→ METHODOLOGY.md ("a number without a manifest is treated as false").

## Market microstructure & research

**Explain adverse selection like I'm not a trader.** Primer's markout framing: your resting
order fills exactly when someone faster/better-informed wants it to — measured as post-fill
mid drift against you. Then the RQ1 result: [measured effect per latency decade].

**What did your experiments actually show?** One sentence per RQ from the findings docs,
each with CI and scope caveat. Then, unprompted, the limitations: Poisson flow has no real
information; results are properties of the simulated market; that's why conclusions were
pre-registered with "not justified" lists. Volunteering limitations before being asked is
the strongest researcher signal available.

**Why did you drop the imbalance-prediction question?** Because in a simulator whose flow I
authored, imbalance predictivity can be an artifact of my own generator — the result would be
circular. (D9 — this answer alone has won interviews; use it.)

**What would differ in a real exchange?** Auctions/halts, IOC/FOK and more order types,
risk at multiple layers with regulatory rules, bps fees with tiering, hidden/iceberg
liquidity, multi-venue fragmentation and SIP vs direct feeds, real network stacks (kernel
bypass, multicast arbitrage), and matching engines that are redundant state machines with
failover. Knowing the delta list cold shows the simplifications were choices, not ignorance.

## The AI-workflow question (it will come up)

**"So Claude built this?"** — Spine: "I ran it like a real engineering org: I made every
design decision explicit in specs — exchange rules, invariants, architecture, task
breakdowns, all in the repo — and used AI as the implementation team against those specs,
with differential tests and invariant checks as the review gate. The decision log shows
which calls were mine and why. Ask me anything in it." Then let them. (This only works if
it's true — which is what the per-checkpoint reviews and this guide's study discipline are
for. Never bluff a doc you haven't re-read.)

## Study plan before interview season

Week 1: primer + exchange rules + this guide, out loud. Week 2: order-book + clock +
threading docs; whiteboard the book from memory. Week 3: research trio + findings; re-derive
one CI. Ongoing: after every real interview, add the questions you got to this doc.
