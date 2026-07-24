#include <benchmark/benchmark.h>

#include "microsim/core/module_info.hpp"

// R1-01: proves the Google Benchmark harness builds, links, and runs. Real
// benchmarks (bm_book_insert, bm_match_market, ...) arrive with task R1-23.
static void BM_SmokeModuleName(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(microsim::core::module_name());
  }
}

BENCHMARK(BM_SmokeModuleName);

BENCHMARK_MAIN();
