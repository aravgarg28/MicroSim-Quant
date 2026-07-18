# Strict warning set applied to every first-party target.
#
# Rationale and the -Werror policy (CI-only) are documented in
# docs/build/BUILD_SYSTEM.md. Third-party dependencies are included as SYSTEM
# (see deps.cmake) so none of this applies to them.

add_library(microsim_warnings INTERFACE)
add_library(microsim::warnings ALIAS microsim_warnings)

set(_microsim_warning_flags
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Woverloaded-virtual
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wno-unused-parameter)

target_compile_options(microsim_warnings INTERFACE ${_microsim_warning_flags})

if(MICROSIM_WERROR)
  target_compile_options(microsim_warnings INTERFACE -Werror)
endif()

# Convenience helper: every first-party target is registered through this so the
# warning set and the include-directory convention stay in one place.
#
#   microsim_add_library(book)
#     -> target microsim_book, alias microsim::book,
#        public headers in src/book/include, sources given after the name.
function(microsim_add_library name)
  set(target "microsim_${name}")
  add_library(${target} ${ARGN})
  add_library(microsim::${name} ALIAS ${target})
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(${target} PRIVATE microsim::warnings)
endfunction()
