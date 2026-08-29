#pragma once

/// \file
/// A process-global allocation counter for the benchmark suite (task R1-23). It
/// replaces the global `operator new`/`operator delete` with counting wrappers
/// so a benchmark can report an honest **allocs/op** column — the measured
/// enforcement of the "zero steady-state allocations in the engine path" target
/// (MEMORY_MODEL.md §"Allocation policy"; the CI smoke asserts the 0 columns
/// stay 0 from the post-optimization releases on).
///
/// This lives only in the benchmark binaries (never in the library or the test
/// binaries): replacing global `operator new` is a whole-program decision, and
/// the benchmark executables are exactly where we want it. Counting is a relaxed
/// atomic add per allocation — negligible against anything worth benchmarking,
/// and never on the timed path except the one op each benchmark brackets.

#include <cstdint>

namespace microsim::bench {

/// Number of `operator new` calls since process start (or the last reset).
[[nodiscard]] std::uint64_t alloc_count() noexcept;

/// Total bytes requested through `operator new` since process start (or reset).
[[nodiscard]] std::uint64_t alloc_bytes() noexcept;

/// Reset both counters to zero.
void reset_alloc_stats() noexcept;

}  // namespace microsim::bench
