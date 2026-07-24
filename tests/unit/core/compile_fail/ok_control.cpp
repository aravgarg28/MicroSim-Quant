#include <cstdint>

#include "microsim/core/types.hpp"
using namespace microsim::core;

int main() {
  Price p = Price{1001} + 2;
  std::int64_t d = Price{1003} - p;
  Qty q = Qty{4} + Qty{6};
  Cash c = -(Cash{10} - Cash{3});
  SimTime t = SimTime::zero() + Duration{500};
  Duration dur = t - SimTime::zero();
  (void)d;
  (void)q;
  (void)c;
  (void)dur;
  return 0;
}
