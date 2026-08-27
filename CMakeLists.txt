cmake_minimum_required(VERSION 3.27)

project(
  MicroSim
  VERSION 0.0.1
  DESCRIPTION "Deterministic market microstructure research platform"
  LANGUAGES CXX)

# ------------------------------------------------------------------------------
# Global settings
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy, editors, and scripts/check_deps.py.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(MICROSIM_BUILD_TESTS "Build the test suites" ON)
option(MICROSIM_BUILD_BENCHMARKS "Build the benchmark suites" ON)
option(MICROSIM_WERROR "Treat warnings as errors (enabled on CI)" OFF)

include(cmake/warnings.cmake)
include(cmake/deps.cmake)

# ------------------------------------------------------------------------------
# Libraries (see docs/architecture/COMPONENT_BOUNDARIES.md for the dependency
# rules these targets encode; edges below are the allowlist, enforced later by
# scripts/check_deps.py).
# ------------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/book)
add_subdirectory(src/sim)
add_subdirectory(src/accounting)
add_subdirectory(src/md)
add_subdirectory(src/persist)
add_subdirectory(src/engine)
add_subdirectory(src/agents)
add_subdirectory(src/strategy)
add_subdirectory(src/metrics)

add_subdirectory(apps)

if(MICROSIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MICROSIM_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

message(STATUS "MicroSim ${PROJECT_VERSION}: tests=${MICROSIM_BUILD_TESTS} "
               "benchmarks=${MICROSIM_BUILD_BENCHMARKS} werror=${MICROSIM_WERROR}")
