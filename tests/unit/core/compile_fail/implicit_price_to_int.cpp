#include <cstdint>

#include "microsim/core/types.hpp"
using namespace microsim::core;

// Banned: no implicit conversion from Price to a raw integer.
int main() {
  std::int64_t x = Price{1};
  (void)x;
}
