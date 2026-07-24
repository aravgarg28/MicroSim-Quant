#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: cross-type addition. Notional (Price x Qty) needs the instrument.
int main() {
  auto x = Price{1} + Qty{1};
  (void)x;
}
