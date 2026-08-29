/// \file
/// Matching microbenchmarks (task R1-23, BENCHMARK_PLAN.md rows bm_match_market /
/// bm_match_limit_marketable): the cost of one aggressive order sweeping the top
/// of book, driven end to end through `MatchingEngine::process` (validation,
/// risk, match loop, fee/fill emission) — the hot path that sets the engine's
/// headline throughput/latency numbers. Run over both books so the sweep cost is
/// reported for FastBook and the ReferenceBook oracle.
///
/// UseManualTime brackets exactly the taker's `process` call. Resting liquidity
/// is (re)built and any remainder drained *outside* that bracket, and the engine
/// is periodically reconstructed so the order registry does not grow without
/// bound over a long run — neither shows up in the measured time or allocs/op.

#include <cstdint>
#include <vector>

#include "microsim/book/fast_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/venue.hpp"

#include "support/bench_support.hpp"

namespace {

namespace mc = microsim::core;
namespace mb = microsim::book;
namespace me = microsim::engine;
namespace ms = microsim::bench;

constexpr mc::InstrumentId kInstr{1};
constexpr std::int64_t kAskBase = 1000;  // first (best) ask price tick
constexpr std::int64_t kLevelQty = 10;   // lots resting at each swept level (even: partial = half)

// A tight band around the prices actually used (asks 1000..1020, remainder at
// 1020): the engine is rebuilt fresh for every measured op — so each taker faces
// a registry of only the K makers, keeping the O(open-orders) risk scan out of
// the measurement — and a small band keeps that per-iteration rebuild cheap.
mc::InstrumentConfig instrument() {
  return mc::InstrumentConfig{.id = kInstr,
                              .symbol = "BM",
                              .tick_size = 1,
                              .lot_size = 1,
                              .min_price = mc::Price{900},
                              .max_price = mc::Price{1100},
                              .max_order_qty = mc::Qty{10'000'000}};
}

me::Venue make_venue() {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(mc::ParticipantConfig{.id = mc::ParticipantId{1}});  // maker
  v.add_participant(mc::ParticipantConfig{.id = mc::ParticipantId{2}});  // taker
  return v;
}

mc::NewOrder sell_limit(std::uint64_t clord, std::int64_t price_tick, std::int64_t qty) {
  return mc::NewOrder{.participant = mc::ParticipantId{1},
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = mc::Side::Sell,
                      .type = mc::OrderType::Limit,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{price_tick}};
}

mc::NewOrder buy_market(std::uint64_t clord, std::int64_t qty) {
  return mc::NewOrder{.participant = mc::ParticipantId{2},
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = mc::Side::Buy,
                      .type = mc::OrderType::Market,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{}};
}

mc::NewOrder buy_limit(std::uint64_t clord, std::int64_t price_tick, std::int64_t qty) {
  return mc::NewOrder{.participant = mc::ParticipantId{2},
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = mc::Side::Buy,
                      .type = mc::OrderType::Limit,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{price_tick}};
}

// Build the K resting ask levels (each kLevelQty lots) the taker will sweep, in
// a fresh engine. Untimed setup for both matching benchmarks.
template <class Book>
me::MatchingEngine<Book> engine_with_asks(std::int64_t levels) {
  me::MatchingEngine<Book> engine{make_venue(), instrument()};
  std::uint64_t clord = 1;
  for (std::int64_t i = 0; i < levels; ++i) {
    engine.process(mc::Inbound{sell_limit(clord++, kAskBase + i, kLevelQty)});
  }
  return engine;
}

// ----- bm_match_market: a MARKET order sweeps K levels (full vs partial) -------

template <class Book>
void match_market(benchmark::State& state) {
  const std::int64_t levels = state.range(0);
  const bool partial = state.range(1) != 0;
  const std::int64_t total = levels * kLevelQty;
  // Full: consume every level exactly. Partial: stop half way through the last
  // level, so the marginal maker is a partial fill (R-5.4) rather than a clear.
  const std::int64_t taken = partial ? total - kLevelQty / 2 : total;
  const std::uint64_t taker_clord = static_cast<std::uint64_t>(levels) + 1;

  std::uint64_t op_allocs = 0;
  for (auto _ : state) {
    me::MatchingEngine<Book> engine = engine_with_asks<Book>(levels);  // untimed setup
    const mc::Inbound taker{buy_market(taker_clord, taken)};

    const std::uint64_t before = ms::alloc_count();
    const auto t0 = ms::Clock::now();
    std::vector<mc::Outbound> events = engine.process(taker);  // <-- measured
    const auto t1 = ms::Clock::now();
    op_allocs += ms::alloc_count() - before;
    state.SetIterationTime(ms::seconds_between(t0, t1));
    benchmark::DoNotOptimize(events.data());
    benchmark::ClobberMemory();
  }
  ms::set_allocs_per_op(state, op_allocs);
}

// ----- bm_match_limit_marketable: sweep K levels, remainder rests -------------

template <class Book>
void match_limit_marketable(benchmark::State& state) {
  const std::int64_t levels = state.range(0);
  const std::int64_t total = levels * kLevelQty;
  const std::int64_t remainder = kLevelQty;  // rests after the sweep
  const std::uint64_t taker_clord = static_cast<std::uint64_t>(levels) + 1;

  std::uint64_t op_allocs = 0;
  for (auto _ : state) {
    me::MatchingEngine<Book> engine = engine_with_asks<Book>(levels);  // untimed setup
    // Priced above every resting ask, so it sweeps all K levels and rests the
    // remainder as a bid at its limit (the remainder-rests path in the plan).
    const mc::Inbound taker{buy_limit(taker_clord, kAskBase + levels, total + remainder)};

    const std::uint64_t before = ms::alloc_count();
    const auto t0 = ms::Clock::now();
    std::vector<mc::Outbound> events = engine.process(taker);  // <-- measured
    const auto t1 = ms::Clock::now();
    op_allocs += ms::alloc_count() - before;
    state.SetIterationTime(ms::seconds_between(t0, t1));
    benchmark::DoNotOptimize(events.data());
    benchmark::ClobberMemory();
  }
  ms::set_allocs_per_op(state, op_allocs);
}

}  // namespace

BENCHMARK_TEMPLATE(match_market, mb::FastBook)->UseManualTime()->ArgsProduct({{1, 5, 20}, {0, 1}});
BENCHMARK_TEMPLATE(match_market, mb::ReferenceBook)
    ->UseManualTime()
    ->ArgsProduct({{1, 5, 20}, {0, 1}});

BENCHMARK_TEMPLATE(match_limit_marketable, mb::FastBook)->UseManualTime()->Arg(1)->Arg(5)->Arg(20);
BENCHMARK_TEMPLATE(match_limit_marketable, mb::ReferenceBook)
    ->UseManualTime()
    ->Arg(1)
    ->Arg(5)
    ->Arg(20);

BENCHMARK_MAIN();
