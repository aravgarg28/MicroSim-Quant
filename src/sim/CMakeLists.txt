microsim_add_library(sim src/module_info.cpp)

target_link_libraries(microsim_sim PUBLIC microsim::core)
