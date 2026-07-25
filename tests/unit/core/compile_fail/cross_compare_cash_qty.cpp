#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: comparisons only within a type.
int main() {
  bool b = (Cash{1} < Qty{1});
  (void)b;
}
