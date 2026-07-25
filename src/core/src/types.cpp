#include "microsim/core/types.hpp"

namespace microsim::core {

const char* to_cstr(Side s) noexcept {
  switch (s) {
    case Side::Buy:
      return "BUY";
    case Side::Sell:
      return "SELL";
  }
  return "?";  // unreachable for a valid Side; keeps the compiler happy.
}

std::ostream& operator<<(std::ostream& os, Price p) {
  return os << p.ticks() << 't';
}

std::ostream& operator<<(std::ostream& os, Qty q) {
  return os << q.lots() << "lot";
}

std::ostream& operator<<(std::ostream& os, Cash c) {
  return os << c.minor() << "mu";
}

std::ostream& operator<<(std::ostream& os, SimTime t) {
  return os << t.ns() << "ns";
}

std::ostream& operator<<(std::ostream& os, Duration d) {
  return os << d.ns() << "ns";
}

std::ostream& operator<<(std::ostream& os, Side s) {
  return os << to_cstr(s);
}

}  // namespace microsim::core
