#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: adding two prices is meaningless (only Price +/- int64 is allowed).
int main() {
  auto x = Price{1} + Price{1};
  (void)x;
}
