# Benchmark targets (docs/performance/BENCHMARK_PLAN.md).
#
# Authoritative numbers are collected on the development Mac under the rules in
# docs/performance/METHODOLOGY.md — never in CI, never across machines. The
# smoke benchmark below only proves the harness is wired (task R1-01); the real
# suite arrives with R1-23.

function(microsim_add_benchmark name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE microsim::warnings benchmark::benchmark)
endfunction()

microsim_add_benchmark(bm_smoke bm_smoke.cpp)
target_link_libraries(bm_smoke PRIVATE microsim::core)
