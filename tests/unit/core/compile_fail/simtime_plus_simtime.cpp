#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: adding two instants (only SimTime +/- Duration is allowed).
int main() {
  auto x = SimTime{1} + SimTime{1};
  (void)x;
}
