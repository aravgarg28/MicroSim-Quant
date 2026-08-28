#include "microsim/core/convert.hpp"

#include <cctype>
#include <limits>

namespace microsim::core {

const char* to_cstr(ConvertError e) noexcept {
  switch (e) {
    case ConvertError::Empty:
      return "Empty";
    case ConvertError::BadFormat:
      return "BadFormat";
    case ConvertError::TooManyFractionDigits:
      return "TooManyFractionDigits";
    case ConvertError::Overflow:
      return "Overflow";
    case ConvertError::NotTickMultiple:
      return "NotTickMultiple";
    case ConvertError::NotLotMultiple:
      return "NotLotMultiple";
    case ConvertError::Negative:
      return "Negative";
  }
  return "?";
}

namespace {

constexpr int kMaxFracDigits = 18;  ///< 10^18 < INT64_MAX; 10^19 overflows int64.

/// 10^n for 0 <= n <= 18, exact in int64. Callers guarantee the bound.
[[nodiscard]] constexpr std::int64_t pow10(int n) noexcept {
  std::int64_t r = 1;
  for (int i = 0; i < n; ++i) {
    r *= 10;
  }
  return r;
}

/// Append digit `d` to non-negative accumulator `acc` (acc*10 + d), guarding the
/// int64 range. Returns false on overflow, leaving `acc` unspecified.
[[nodiscard]] constexpr bool push_digit(std::int64_t& acc, int d) noexcept {
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  if (acc > (kMax - d) / 10) {
    return false;
  }
  acc = acc * 10 + d;
  return true;
}

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
  return c >= '0' && c <= '9';
}

[[nodiscard]] bool all_space(std::string_view s) noexcept {
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

Converted<std::int64_t> parse_decimal_minor(std::string_view text, int frac_digits) noexcept {
  if (frac_digits < 0 || frac_digits > kMaxFracDigits) {
    return {0, ConvertError::BadFormat};
  }
  if (text.empty() || all_space(text)) {
    return {0, ConvertError::Empty};
  }

  std::size_t i = 0;
  bool negative = false;
  if (text[i] == '+' || text[i] == '-') {
    negative = (text[i] == '-');
    ++i;
  }

  // Accumulate all significant digits into one integer, tracking how many landed
  // after the decimal point. minor = digits * 10^(frac_digits - seen_frac).
  std::int64_t acc = 0;
  int seen_frac = -1;  // -1 until a '.' is seen; then counts fractional digits.
  int total_digits = 0;

  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '.') {
      if (seen_frac != -1) {
        return {0, ConvertError::BadFormat};  // second dot
      }
      seen_frac = 0;
      continue;
    }
    if (!is_digit(c)) {
      return {0, ConvertError::BadFormat};
    }
    if (seen_frac != -1) {
      if (++seen_frac > frac_digits) {
        return {0, ConvertError::TooManyFractionDigits};
      }
    }
    if (!push_digit(acc, c - '0')) {
      return {0, ConvertError::Overflow};
    }
    ++total_digits;
  }

  if (total_digits == 0) {
    return {0, ConvertError::BadFormat};  // "", "+", ".", "-." etc.
  }

  // Scale up to the full minor-unit precision: pad the missing fractional digits.
  const int have_frac = seen_frac == -1 ? 0 : seen_frac;
  const std::int64_t scale = pow10(frac_digits - have_frac);
  if (scale != 1) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (acc > kMax / scale) {
      return {0, ConvertError::Overflow};
    }
    acc *= scale;
  }

  return {negative ? -acc : acc, std::nullopt};
}

Converted<Price> price_from_decimal(std::string_view text, std::int64_t tick_size,
                                    int frac_digits) noexcept {
  const auto minor = parse_decimal_minor(text, frac_digits);
  if (!minor.ok()) {
    return {Price{}, minor.error};
  }
  if (minor.value < 0) {
    return {Price{}, ConvertError::Negative};
  }
  if (tick_size <= 0 || minor.value % tick_size != 0) {
    return {Price{}, ConvertError::NotTickMultiple};
  }
  return {Price{minor.value / tick_size}, std::nullopt};
}

Converted<Price> price_from_decimal(std::string_view text, const InstrumentConfig& instr,
                                    int frac_digits) noexcept {
  return price_from_decimal(text, instr.tick_size, frac_digits);
}

Converted<Qty> qty_from_decimal(std::string_view text, std::int64_t lot_size,
                                int frac_digits) noexcept {
  const auto units = parse_decimal_minor(text, frac_digits);
  if (!units.ok()) {
    return {Qty{}, units.error};
  }
  if (units.value < 0) {
    return {Qty{}, ConvertError::Negative};
  }
  if (lot_size <= 0 || units.value % lot_size != 0) {
    return {Qty{}, ConvertError::NotLotMultiple};
  }
  return {Qty{units.value / lot_size}, std::nullopt};
}

namespace {

/// Render `minor` (a signed integer at scale 10^frac_digits) as a decimal string.
[[nodiscard]] std::string format_scaled(std::int64_t minor, int frac_digits) {
  std::string out;
  if (minor < 0) {
    out.push_back('-');
  }
  // Work in unsigned magnitude to handle INT64_MIN without overflow.
  const std::uint64_t mag =
      minor < 0 ? (~static_cast<std::uint64_t>(minor) + 1U) : static_cast<std::uint64_t>(minor);
  if (frac_digits == 0) {
    out += std::to_string(mag);
    return out;
  }
  const auto scale = static_cast<std::uint64_t>(pow10(frac_digits));
  const std::uint64_t whole = mag / scale;
  const std::uint64_t frac = mag % scale;
  out += std::to_string(whole);
  out.push_back('.');
  std::string frac_str = std::to_string(frac);
  if (static_cast<int>(frac_str.size()) < frac_digits) {
    out.append(static_cast<std::size_t>(frac_digits) - frac_str.size(), '0');
  }
  out += frac_str;
  return out;
}

}  // namespace

std::string price_to_decimal(Price price, std::int64_t tick_size, int frac_digits) {
  return format_scaled(price.ticks() * tick_size, frac_digits);
}

std::string price_to_decimal(Price price, const InstrumentConfig& instr, int frac_digits) {
  return price_to_decimal(price, instr.tick_size, frac_digits);
}

std::string cash_to_decimal(Cash cash, int frac_digits) {
  return format_scaled(cash.minor(), frac_digits);
}

std::string qty_to_decimal(Qty qty, std::int64_t lot_size, int frac_digits) {
  return format_scaled(qty.lots() * lot_size, frac_digits);
}

}  // namespace microsim::core
