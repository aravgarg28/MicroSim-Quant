microsim_add_library(agents src/module_info.cpp)

target_link_libraries(microsim_agents PUBLIC microsim::core microsim::md microsim::sim)
