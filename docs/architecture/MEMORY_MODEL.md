# Memory Model

Ownership, lifetime, and layout rules for the engine. The MVP implementation is deliberately
plain (correct, value-oriented, standard containers); this document also fixes the vocabulary
and the *later* optimization path so that optimizations are refactorings, not redesigns.

## Ownership rules

1. **Orders are owned by the book's order store**, one per instrument. Nothing else ever owns
   an order. Everything else refers to orders by **`OrderIndex`** (a 32-bit slot index into the
   store) or by `order_id` (the external key). No raw owning pointers, no `shared_ptr` — an
   order's lifetime is exactly: created at acceptance, slot recycled after its terminal event
   is emitted.
2. **Events are values.** Every message and event struct is trivially copyable POD-ish data
   (fixed-size, no heap members; strings live only in config). Handing an event to the queue,
   the log, or a callback copies it. At ≤ 64 bytes per event, copying beats shared ownership
   in both speed and reasoning.
3. **The event queue owns scheduled events** until delivery; delivery hands a const reference
   to the handler; the handler may copy but never store the reference (callback contract,
   stated on the interface).
4. **Config is immutable after simulation construction** — plain structs, freely referenced.
5. **Rule of zero everywhere.** No hand-written destructors/copy/move in domain types; RAII
   containers own all storage. Move-only types only where identity matters (the simulation
   object itself, file handles).

## Order store and lifetime (MVP → optimized, same interface)

- **MVP:** `std::vector<Order>` slab + free-list of recycled `OrderIndex` slots +
  `std::unordered_map<order_id, OrderIndex>` for external lookup. Slots make pointers stable
  *by index* (vector may reallocate; indices survive). Terminal orders release their slot.
- **Optimized (per ORDER_BOOK_DESIGN.md, when benchmarks demand):** same slab, but (a)
  reserve capacity up front (allocation-free steady state), (b) replace the unordered_map with
  an open-addressing table keyed by `order_id` (single probe common case), (c) intrusive FIFO:
  `Order` carries `prev/next OrderIndex` links, so queue membership costs zero extra
  allocations and cancel is O(1) unlink.
- **Generation counters** (slot reuse guard): `OrderIndex` pairs with an 8-bit generation in
  debug builds to catch use-after-recycle at the source; stripped in release.

## Price-level storage

MVP: ordered `std::map<Price, Level>` per side (Reference book keeps this permanently).
Optimized candidates (decision deferred to `docs/engine/ORDER_BOOK_DESIGN.md`, driven by the
banded-price-range assumption R-1.1): contiguous array of `Level` indexed by
`(price − min_price) / tick` with best-pointer tracking. `Level` holds aggregate qty + FIFO
head/tail `OrderIndex` — 32 bytes, four levels per cache line.

## Allocation policy

- **Steady-state target (post-R3 optimization pass): zero heap allocations per message** in
  the engine path. Achieved by: pre-reserved slabs (orders, levels), fixed-size event structs,
  pre-sized event queue storage, and log writers with owned buffers.
- **Allocations are permitted at:** construction/config time and session end. Metrics
  collectors run inside the loop, so their steady state must also be allocation-free;
  their export buffers may grow amortized (reserve-doubling), which the counting hook
  reports separately from per-message allocations.
- **Enforcement is measured, not asserted:** a counting allocator / `operator new` hook wraps
  benchmark runs; the benchmark suite reports allocations-per-message and CI fails the smoke
  benchmark if the engine path allocates in steady state (post-optimization releases only).

## Layout and cache locality

- Hot structs are laid out hot-first: `Order` puts (remaining qty, price, side/flags,
  prev/next links) in the first 32 bytes; audit fields (client_order_id, participant, ts)
  after. `static_assert(sizeof(Order) <= 64)` pins the budget.
- Data-oriented refactors (SoA for level arrays, separating cold audit data into a parallel
  array) are *candidates*, applied only with before/after benchmarks per the optimization
  roadmap — the MVP does not pre-pessimize into cleverness.
- Alignment: cache-line alignment appears in exactly two places when threading lands (queue
  head/tail; per-shard counters); nothing else gets `alignas` without a measurement.

## Copies, moves, and the Python boundary

- Engine → Python: **always deep copies** into NumPy/Arrow buffers allocated by the binding
  layer. No views into C++ memory cross the boundary (lifetime safety beats zero-copy at this
  boundary's granularity — results cross once per run, not per event).
- Big result tables are moved (not copied) from collectors into the binding's column builders
  where types allow.

## What is deliberately NOT done in the MVP

Custom arenas, huge pages, NUMA pinning, hand-rolled hash functions, SIMD scans, and
`mmap`-backed logs are all *out* until a profile on the disclosed hardware names one of them
as the bottleneck (see `docs/performance/OPTIMIZATION_ROADMAP.md`). The list exists so the
optimization pass has a menu — and so the interview answer to "what would you do next?" is
already written.
