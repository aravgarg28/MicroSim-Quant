#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: notional must go through Notional(price, qty, instrument), not '*'.
int main() {
  auto x = Price{1} * Qty{1};
  (void)x;
}
