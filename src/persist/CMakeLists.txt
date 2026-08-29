microsim_add_library(persist src/module_info.cpp src/log.cpp src/wire.cpp)

target_link_libraries(microsim_persist PUBLIC microsim::core)
