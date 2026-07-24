#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: no implicit conversion from a raw integer to Price.
int main() {
  Price p = 5;
  (void)p;
}
