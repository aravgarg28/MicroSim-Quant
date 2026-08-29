# Command-line front ends (docs/architecture/REPOSITORY_STRUCTURE.md). Both drive
# the same demo scenario/config (demo.hpp): microsim_run tees an input + event
# log, microsim_replay reads the input log back and reproduces the event log
# byte-identically (INV-10). Both link the engine (matching + sequencer) and
# persist (the on-disk log).

add_executable(microsim_run microsim_run.cpp)
target_link_libraries(microsim_run PRIVATE microsim::engine microsim::persist microsim::warnings)

add_executable(microsim_replay microsim_replay.cpp)
target_link_libraries(microsim_replay PRIVATE microsim::engine microsim::persist microsim::warnings)
