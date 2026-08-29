# Test targets. Layout mirrors src/ (docs/architecture/REPOSITORY_STRUCTURE.md),
# so tests/unit/<module>/ holds the unit tests for microsim_<module>.
#
# Test naming convention (docs/testing/TEST_STRATEGY.md): tests cite the rule or
# invariant they cover — test_R_5_4_*, prop_INV_6_*. The R1-01 link smoke tests
# below are the one exception: they prove wiring, not behavior.

include(GoogleTest)

# microsim_add_test(book test_link_book.cpp ...)
#   -> target test_book, linked against microsim::book, gtest_main, warnings.
function(microsim_add_test module)
  set(target "test_${module}")
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE microsim::${module} microsim::warnings
                                          GTest::gtest_main)
  # DISCOVERY_MODE PRE_TEST defers test enumeration from build time to ctest
  # time. Build-time discovery (the default) intermittently fails on CI runners
  # with "Missing expected JSON file with test list"; PRE_TEST avoids running
  # the executables during the build entirely.
  gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
endfunction()

microsim_add_test(core unit/core/test_link_core.cpp unit/core/test_types.cpp
                  unit/core/test_events.cpp unit/core/test_config.cpp
                  unit/core/test_convert.cpp)

# Compile-fail tests: prove the banned strong-type operations
# (docs/numerics/NUMERIC_REPRESENTATION.md) do not compile. Each target is
# EXCLUDE_FROM_ALL so it never breaks the normal build; a CTest builds it and
# expects failure (WILL_FAIL). ok_control below is the positive control: it uses
# only allowed operations and MUST build, so the harness demonstrably
# discriminates rather than passing on any compile error.
function(microsim_add_compile_fail case)
  set(target "cf_core_${case}")
  add_executable(${target} EXCLUDE_FROM_ALL "unit/core/compile_fail/${case}.cpp")
  target_link_libraries(${target} PRIVATE microsim::core)
  set_target_properties(${target} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  add_test(NAME core.compile_fail.${case}
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target}
                   --config $<CONFIG>)
  set_tests_properties(core.compile_fail.${case} PROPERTIES WILL_FAIL TRUE)
endfunction()

foreach(case
        price_plus_qty
        price_plus_price
        price_times_qty
        implicit_int_to_price
        implicit_price_to_int
        qty_plus_cash
        simtime_plus_simtime
        qty_times_qty
        cross_compare_cash_qty)
  microsim_add_compile_fail(${case})
endforeach()

# Positive control: builds as part of the normal build; if it ever fails to
# compile, the whole build fails (not silently a passing WILL_FAIL).
add_executable(cf_core_ok_control unit/core/compile_fail/ok_control.cpp)
target_link_libraries(cf_core_ok_control PRIVATE microsim::core microsim::warnings)
add_test(NAME core.compile_fail.ok_control_runs COMMAND cf_core_ok_control)
microsim_add_test(book unit/book/test_link_book.cpp unit/book/test_reference_book.cpp)
microsim_add_test(sim unit/sim/test_link_sim.cpp)
microsim_add_test(accounting unit/accounting/test_link_accounting.cpp)
microsim_add_test(md unit/md/test_link_md.cpp)
microsim_add_test(persist unit/persist/test_link_persist.cpp)
microsim_add_test(engine unit/engine/test_link_engine.cpp unit/engine/test_order_registry.cpp
                  unit/engine/test_matching_engine.cpp unit/engine/test_cancel.cpp
                  unit/engine/test_modify.cpp unit/engine/test_risk.cpp
                  unit/engine/test_session_end.cpp unit/engine/test_sequencer.cpp)
microsim_add_test(agents unit/agents/test_link_agents.cpp)
microsim_add_test(strategy unit/strategy/test_link_strategy.cpp)
microsim_add_test(metrics unit/metrics/test_link_metrics.cpp)

# Smoke test for the demo CLI: it must run to completion and produce the known
# scripted-scenario result (a golden-output check on the walking-skeleton demo).
add_test(NAME app.microsim_run.smoke COMMAND microsim_run)
set_tests_properties(app.microsim_run.smoke
                     PROPERTIES PASS_REGULAR_EXPRESSION "4 trades, 80 lots matched")
