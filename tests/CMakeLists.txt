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
  gtest_discover_tests(${target})
endfunction()

microsim_add_test(core unit/core/test_link_core.cpp)
microsim_add_test(book unit/book/test_link_book.cpp)
microsim_add_test(sim unit/sim/test_link_sim.cpp)
microsim_add_test(accounting unit/accounting/test_link_accounting.cpp)
microsim_add_test(md unit/md/test_link_md.cpp)
microsim_add_test(persist unit/persist/test_link_persist.cpp)
microsim_add_test(engine unit/engine/test_link_engine.cpp)
microsim_add_test(agents unit/agents/test_link_agents.cpp)
microsim_add_test(strategy unit/strategy/test_link_strategy.cpp)
microsim_add_test(metrics unit/metrics/test_link_metrics.cpp)
