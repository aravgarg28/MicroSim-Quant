# Third-party dependencies, fetched and pinned to exact tags.
#
# Decision S2 (docs/DECISIONS.md): FetchContent over vcpkg/Conan. The dependency
# set is tiny, pins live in-tree, and macOS + Linux CI behave identically with no
# external infrastructure. Adding a dependency requires a DECISIONS.md entry.
#
# Dependencies are marked SYSTEM so their headers never emit warnings into our
# strict warning set (see warnings.cmake).

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# ------------------------------------------------------------------------------
# GoogleTest — unit, property, and differential test harness.
# ------------------------------------------------------------------------------
if(MICROSIM_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES GTest)

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# ------------------------------------------------------------------------------
# Google Benchmark — performance measurement (docs/performance/BENCHMARK_PLAN.md).
# ------------------------------------------------------------------------------
if(MICROSIM_BUILD_BENCHMARKS)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.4
    GIT_SHALLOW TRUE
    SYSTEM
    FIND_PACKAGE_ARGS NAMES benchmark)

  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()
