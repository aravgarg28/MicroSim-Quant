# Command-line front ends. Tier-A ships microsim_run, a scripted demonstration
# driver; the scenario-file replay CLIs (microsim_run v1, microsim_replay) are
# fleshed out in R1-21.

add_executable(microsim_run microsim_run.cpp)
target_link_libraries(microsim_run PRIVATE microsim::engine microsim::warnings)
