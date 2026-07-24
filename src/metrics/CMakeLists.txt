microsim_add_library(metrics src/module_info.cpp)

target_link_libraries(microsim_metrics PUBLIC microsim::core microsim::md microsim::accounting)
