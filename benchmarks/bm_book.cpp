/// \file
/// Book microbenchmarks (task R1-23, BENCHMARK_PLAN.md rows bm_book_insert /
/// cancel / modify), run against both `FastBook` and `ReferenceBook` so every
/// row carries the Fast-vs-Reference pair the optimization roadmap compares.
///
/// Each op is measured in isolation with UseManualTime: the `for (auto _ :
/// state)` body brackets exactly the operation under test with a steady_clock
/// read and the allocation counter, and does its bookkeeping (restoring the book
/// to a steady depth, refilling an exhausted queue) *outside* that bracket, so
/// neither the time nor the allocs/op column is polluted by setup. Placement and
/// queue-position knobs (state.range) reproduce the workload rows in the plan.
///
/// These are the unoptimized stage-1 baseline (`std::deque` FIFOs, on-demand
/// aggregates): the numbers are the denominator every later FastBook
/// optimization is measured against (OPTIMIZATION_ROADMAP.md Phase 0).

#include <cstdint>
#include <deque>

#include "microsim/book/fast_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/types.hpp"

#include "support/bench_support.hpp"

namespace {

namespace mc = microsim::core;
namespace mb = microsim::book;
namespace ms = microsim::bench;

// A price band wide enough for the deepest random-placement run (4k levels) with
// room to spare; the at-best runs concentrate everything at the mid.
constexpr std::int64_t kMinTick = 1;
constexpr std::int64_t kMaxTick = 16384;
constexpr std::int64_t kMidTick = 8192;
constexpr std::int64_t kRestQty = 1000;  ///< generous so reduce always shrinks to > 0

mb::RestingOrder resting(std::uint64_t id, mc::Price price, std::int64_t qty = kRestQty) {
  return mb::RestingOrder{.id = mc::OrderId{id},
                          .participant = mc::ParticipantId{1},
                          .side = mc::Side::Buy,
                          .price = price,
                          .remaining = mc::Qty{qty}};
}

// ----- bm_book_insert: add one order into a book already holding `depth` -------

template <class Book>
void book_insert(benchmark::State& state) {
  const std::int64_t depth = state.range(0);
  const bool random_level = state.range(1) != 0;
  Book book = ms::make_book<Book>(mc::Price{kMinTick}, mc::Price{kMaxTick});
  ms::SplitMix64 rng(0xB0'0C'11);
  std::uint64_t next_id = 1;
  const auto price = [&]() {
    return random_level ? mc::Price{rng.between(kMinTick, kMaxTick)} : mc::Price{kMidTick};
  };

  std::deque<std::uint64_t> live;  // ids in insertion order, so the oldest is front
  for (std::int64_t i = 0; i < depth; ++i) {
    const std::uint64_t id = next_id++;
    book.add(resting(id, price()));
    live.push_back(id);
  }

  std::uint64_t op_allocs = 0;
  for (auto _ : state) {
    const std::uint64_t id = next_id++;
    const mb::RestingOrder o = resting(id, price());

    const std::uint64_t before = ms::alloc_count();
    const auto t0 = ms::Clock::now();
    book.add(o);  // <-- measured
    const auto t1 = ms::Clock::now();
    op_allocs += ms::alloc_count() - before;
    state.SetIterationTime(ms::seconds_between(t0, t1));

    live.push_back(id);
    book.remove(mc::OrderId{live.front()});  // untimed: hold depth (oldest is O(1) at front)
    live.pop_front();
  }
  ms::set_allocs_per_op(state, op_allocs);
}

// ----- bm_book_cancel: remove a resting order (front vs back of its queue) -----

template <class Book>
void book_cancel(benchmark::State& state) {
  const std::int64_t depth = state.range(0);
  const bool from_back = state.range(1) != 0;  // front = oldest, back = newest
  Book book = ms::make_book<Book>(mc::Price{kMinTick}, mc::Price{kMaxTick});
  ms::SplitMix64 rng(0xCA'11'CE);
  std::uint64_t next_id = 1;

  std::deque<std::uint64_t> live;
  const auto refill = [&]() {
    for (std::int64_t i = 0; i < depth; ++i) {
      const std::uint64_t id = next_id++;
      book.add(resting(id, mc::Price{rng.between(kMinTick, kMaxTick)}));
      live.push_back(id);
    }
  };
  refill();

  std::uint64_t op_allocs = 0;
  for (auto _ : state) {
    if (live.empty()) {
      refill();  // untimed
    }
    std::uint64_t id = 0;
    if (from_back) {
      id = live.back();
      live.pop_back();
    } else {
      id = live.front();
      live.pop_front();
    }

    const std::uint64_t before = ms::alloc_count();
    const auto t0 = ms::Clock::now();
    book.remove(mc::OrderId{id});  // <-- measured
    const auto t1 = ms::Clock::now();
    op_allocs += ms::alloc_count() - before;
    state.SetIterationTime(ms::seconds_between(t0, t1));
  }
  ms::set_allocs_per_op(state, op_allocs);
}

// ----- bm_book_modify: priority-keeping reduce vs priority-losing re-queue -----

template <class Book>
void book_modify(benchmark::State& state) {
  const std::int64_t depth = state.range(0);
  const bool requeue = state.range(1) != 0;  // false = reduce in place, true = remove+re-add
  Book book = ms::make_book<Book>(mc::Price{kMinTick}, mc::Price{kMaxTick});
  ms::SplitMix64 rng(0x0D'1F'70);
  std::uint64_t next_id = 1;
  const auto rand_price = [&]() { return mc::Price{rng.between(kMinTick, kMaxTick)}; };

  std::deque<std::uint64_t> live;
  for (std::int64_t i = 0; i < depth; ++i) {
    const std::uint64_t id = next_id++;
    book.add(resting(id, rand_price()));
    live.push_back(id);
  }

  std::uint64_t op_allocs = 0;
  for (auto _ : state) {
    const std::uint64_t id = live.front();
    live.pop_front();

    const std::uint64_t before = ms::alloc_count();
    const auto t0 = ms::Clock::now();
    if (requeue) {
      // R-7.2 priority-losing path: out of the book, back in at a new price.
      const std::uint64_t new_id = next_id++;
      book.remove(mc::OrderId{id});
      book.add(resting(new_id, rand_price()));
      const auto t1 = ms::Clock::now();
      op_allocs += ms::alloc_count() - before;
      state.SetIterationTime(ms::seconds_between(t0, t1));
      live.push_back(new_id);  // occupancy held: one out, one in
    } else {
      // R-7.2 priority-keeping path: shrink remaining in place.
      book.reduce(mc::OrderId{id}, mc::Qty{kRestQty - 1});
      const auto t1 = ms::Clock::now();
      op_allocs += ms::alloc_count() - before;
      state.SetIterationTime(ms::seconds_between(t0, t1));
      book.remove(mc::OrderId{id});  // untimed teardown
      const std::uint64_t new_id = next_id++;
      book.add(resting(new_id, rand_price()));  // untimed: restore depth
      live.push_back(new_id);
    }
  }
  ms::set_allocs_per_op(state, op_allocs);
}

}  // namespace

// Depth × {placement | position | mode}. Fast and Reference are separate rows.
BENCHMARK_TEMPLATE(book_insert, mb::FastBook)
    ->UseManualTime()
    ->ArgsProduct({{10, 100, 1000, 4000}, {0, 1}});
BENCHMARK_TEMPLATE(book_insert, mb::ReferenceBook)
    ->UseManualTime()
    ->ArgsProduct({{10, 100, 1000, 4000}, {0, 1}});

BENCHMARK_TEMPLATE(book_cancel, mb::FastBook)
    ->UseManualTime()
    ->ArgsProduct({{10, 100, 1000, 4000}, {0, 1}});
BENCHMARK_TEMPLATE(book_cancel, mb::ReferenceBook)
    ->UseManualTime()
    ->ArgsProduct({{10, 100, 1000, 4000}, {0, 1}});

BENCHMARK_TEMPLATE(book_modify, mb::FastBook)
    ->UseManualTime()
    ->ArgsProduct({{10, 100, 1000, 4000}, {0, 1}});
BENCHMARK_TEMPLATE(book_modify, mb::ReferenceBook)
    ->UseManualTime()
    ->ArgsProduct({{10, 100, 1000, 4000}, {0, 1}});

BENCHMARK_MAIN();
