#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: Qty has no multiplication.
int main() {
  auto x = Qty{2} * Qty{3};
  (void)x;
}
