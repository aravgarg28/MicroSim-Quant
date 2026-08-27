microsim_add_library(book src/module_info.cpp src/reference_book.cpp)

target_link_libraries(microsim_book PUBLIC microsim::core)
