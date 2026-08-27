#pragma once

/// \file
/// Boundary conversions between human/config decimal strings and the engine's
/// exact integer types (task R1-06). This is the *only* place decimal text meets
/// the integer core: everything inside the engine is already ticks/lots/minor
/// units. Implements NUMERIC_REPRESENTATION.md §"Conversion and rounding rules":
///
///   * decimal string -> minor units -> ticks/lots, *exactly*;
///   * a value that is not a whole multiple of the tick (or lot) is **rejected**
///     (`NotTickMultiple` / `NotLotMultiple`, the `INVALID_TICK` semantics),
///     never rounded;
///   * more fractional digits than the currency scale allows is a rejection, not
///     a truncation;
///   * formatting back to text is integer division/modulo on the scales, never
///     double formatting.
///
/// Nothing here allocates on the parse path, throws, or reads a clock. The
/// formatting helpers build a std::string (the one allocation, off the hot path).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "microsim/core/config.hpp"
#include "microsim/core/types.hpp"

namespace microsim::core {

/// Why a boundary conversion failed. Distinct from ConfigError (construction of
/// an instrument) and RejectReason (a runtime order message): these describe a
/// single string<->integer conversion at the edge.
enum class ConvertError : std::uint8_t {
  Empty = 0,              ///< empty or whitespace-only input
  BadFormat,              ///< a character that is not sign/digit/'.'; misplaced sign or dot
  TooManyFractionDigits,  ///< more digits after '.' than the currency scale represents
  Overflow,               ///< magnitude does not fit the int64 minor-unit range
  NotTickMultiple,        ///< minor units are not a whole number of ticks (INVALID_TICK)
  NotLotMultiple,         ///< base units are not a whole number of lots
  Negative,               ///< a negative value where only non-negative is allowed
};

[[nodiscard]] const char* to_cstr(ConvertError e) noexcept;

/// The result of a boundary conversion: a value plus an optional error. When
/// `error` is set, `value` is unspecified. This mirrors the house style of
/// `validate()` returning `std::optional<Error>`, extended to carry a value.
template <class T>
struct Converted {
  T value{};
  std::optional<ConvertError> error{};

  [[nodiscard]] constexpr bool ok() const noexcept { return !error.has_value(); }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }
};

// =============================================================================
// Decimal string -> exact minor units
// =============================================================================
//
// `frac_digits` is the currency scale: the number of fractional decimal digits
// the minor unit represents (2 for cents, 0 for a whole-unit currency). "10.03"
// with frac_digits=2 is 1003 minor units; "10.031" with frac_digits=2 is
// rejected (TooManyFractionDigits), never silently truncated to 1003.

/// Parse a signed decimal string to exact minor units at the given scale.
[[nodiscard]] Converted<std::int64_t> parse_decimal_minor(std::string_view text,
                                                          int frac_digits) noexcept;

// =============================================================================
// Decimal string -> Price / Qty (with tick/lot validation)
// =============================================================================

/// "10.03" (+ tick_size, frac_digits) -> Price in ticks, or NotTickMultiple.
/// `tick_size` is minor units per tick (> 0, as guaranteed by a validated
/// instrument). The minor-unit value must be a whole multiple of `tick_size`.
[[nodiscard]] Converted<Price> price_from_decimal(std::string_view text, std::int64_t tick_size,
                                                  int frac_digits) noexcept;

/// Convenience: use the instrument's tick_size and price scale.
[[nodiscard]] Converted<Price> price_from_decimal(std::string_view text,
                                                  const InstrumentConfig& instr,
                                                  int frac_digits) noexcept;

/// Parse a base-unit decimal/integer quantity to Qty in lots. `lot_size` is base
/// units per lot (> 0). With lot_size == 1 and frac_digits == 0 this is a plain
/// integer parse. Non-multiples of the lot are rejected (NotLotMultiple).
[[nodiscard]] Converted<Qty> qty_from_decimal(std::string_view text, std::int64_t lot_size,
                                              int frac_digits) noexcept;

// =============================================================================
// Integer engine values -> decimal strings (reporting boundary)
// =============================================================================
//
// Formatting is integer division/modulo on the scales — never double formatting.
// "$10.03" printed from 1003 ticks (tick_size 1, 2 dp) is byte-exact.

/// Format a Price back to a decimal string: ticks * tick_size minor units,
/// rendered with `frac_digits` fractional places. Handles the negative sign and
/// zero-padding of the fractional part.
[[nodiscard]] std::string price_to_decimal(Price price, std::int64_t tick_size, int frac_digits);

/// Convenience: use the instrument's tick_size.
[[nodiscard]] std::string price_to_decimal(Price price, const InstrumentConfig& instr,
                                           int frac_digits);

/// Format a Cash amount (already in minor units) with `frac_digits` places.
[[nodiscard]] std::string cash_to_decimal(Cash cash, int frac_digits);

/// Format a Qty back to a decimal string of base units: lots * lot_size, with
/// `frac_digits` fractional places (0 for whole-lot instruments).
[[nodiscard]] std::string qty_to_decimal(Qty qty, std::int64_t lot_size, int frac_digits);

}  // namespace microsim::core
