microsim_add_library(engine src/module_info.cpp src/order_registry.cpp)

target_link_libraries(microsim_engine PUBLIC microsim::core microsim::book microsim::accounting microsim::md)
