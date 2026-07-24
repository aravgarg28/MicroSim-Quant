microsim_add_library(accounting src/module_info.cpp)

target_link_libraries(microsim_accounting PUBLIC microsim::core)
