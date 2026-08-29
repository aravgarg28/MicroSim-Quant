# Benchmark targets (docs/performance/BENCHMARK_PLAN.md).
#
# Authoritative numbers are collected on the development Mac under the rules in
# docs/performance/METHODOLOGY.md — never in CI, never across machines. The
# smoke benchmark proves the harness is wired (task R1-01); the R1-23 suite adds
# the book and matching microbenchmarks with the counting-allocator hook.

# The counting operator-new hook (allocs/op column). Object library so every
# benchmark links exactly one definition of the global allocation functions.
add_library(microsim_bench_support OBJECT support/alloc_counter.cpp)
target_include_directories(microsim_bench_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(microsim_bench_support PRIVATE microsim::warnings)

function(microsim_add_benchmark name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE microsim::warnings benchmark::benchmark
                                        microsim_bench_support)
endfunction()

microsim_add_benchmark(bm_smoke bm_smoke.cpp)
target_link_libraries(bm_smoke PRIVATE microsim::core)

# R1-23 microbenchmark suite: book ops (Fast vs Reference) and the matching hot
# path. Each links the engine (which brings book + core) and the alloc hook.
microsim_add_benchmark(bm_book bm_book.cpp)
target_link_libraries(bm_book PRIVATE microsim::book)

microsim_add_benchmark(bm_match bm_match.cpp)
target_link_libraries(bm_match PRIVATE microsim::engine)
