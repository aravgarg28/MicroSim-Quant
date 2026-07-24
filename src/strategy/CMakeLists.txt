microsim_add_library(strategy src/module_info.cpp)

target_link_libraries(microsim_strategy PUBLIC microsim::core microsim::md microsim::sim microsim::agents)
