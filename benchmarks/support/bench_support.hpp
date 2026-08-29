#pragma once

/// \file
/// Small shared helpers for the microbenchmarks (task R1-23): a deterministic
/// integer PRNG for placement, a per-op allocation bracket, and the code that
/// turns a bracketed allocation total into the `allocs/op` counter Google
/// Benchmark prints. Kept header-only and dependency-light so each benchmark
/// translation unit is self-contained.

#include <chrono>
#include <cstdint>
#include <type_traits>

#include <benchmark/benchmark.h>

#include "microsim/book/fast_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/types.hpp"

#include "alloc_counter.hpp"

namespace microsim::bench {

namespace mc = microsim::core;

/// SplitMix64 — a tiny, platform-stable integer PRNG (the same one the property
/// generator uses) so a benchmark's placement pattern is reproducible.
class SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

  std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  std::int64_t between(std::int64_t lo, std::int64_t hi) noexcept {
    if (hi <= lo) {
      return lo;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(hi - lo + 1);
    return lo + static_cast<std::int64_t>(next() % span);
  }

 private:
  std::uint64_t state_;
};

/// Steady-clock reading for a manually-timed op (benchmarks use UseManualTime so
/// only the bracketed op counts, not the untimed setup/teardown around it).
using Clock = std::chrono::steady_clock;

[[nodiscard]] inline double seconds_between(Clock::time_point a, Clock::time_point b) noexcept {
  return std::chrono::duration<double>(b - a).count();
}

/// Report the mean allocations per measured op: the total `operator new` calls
/// counted inside the per-op brackets, divided by the iteration count.
inline void set_allocs_per_op(benchmark::State& state, std::uint64_t bracketed_allocs) {
  const double per_op = state.iterations() == 0 ? 0.0
                                                : static_cast<double>(bracketed_allocs) /
                                                      static_cast<double>(state.iterations());
  state.counters["allocs/op"] = benchmark::Counter(per_op);
}

/// Construct a book over the price band. FastBook is built from the band; the
/// band-agnostic ReferenceBook is default-constructed (same split as the engine).
template <class Book>
[[nodiscard]] Book make_book(mc::Price min_price, mc::Price max_price) {
  if constexpr (std::is_constructible_v<Book, mc::Price, mc::Price>) {
    return Book(min_price, max_price);
  } else {
    return Book();
  }
}

}  // namespace microsim::bench
