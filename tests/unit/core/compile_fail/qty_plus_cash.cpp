#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: cross-type addition of lots and money.
int main() {
  auto x = Qty{1} + Cash{1};
  (void)x;
}
