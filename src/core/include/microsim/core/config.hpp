#pragma once

/// \file
/// Instrument, participant, and session configuration, with validation and the
/// exact `Notional` computation (task R1-05). Encodes EXCHANGE_RULES.md §1
/// (R-1.1/R-1.3), §9 (risk fields), §11 (fees), and the overflow-headroom bound
/// from NUMERIC_REPRESENTATION.md. Config is immutable after construction
/// (R-1.1); validation happens once, at construction, and guarantees the
/// matching path never overflows for in-band prices and valid quantities.

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include "microsim/core/types.hpp"

namespace microsim::core {

/// What made a configuration invalid. Distinct from RejectReason (which is for
/// runtime messages): these are construction-time errors.
enum class ConfigError : std::uint8_t {
  BadTickSize = 0,      ///< tick_size <= 0
  BadLotSize,           ///< lot_size <= 0
  MinPriceBelowOne,     ///< min_price_ticks < 1 (R-1.1)
  MaxPriceNotAboveMin,  ///< max_price_ticks <= min_price_ticks (R-1.1)
  BadMaxOrderQty,       ///< max_order_qty_lots < 1 (R-1.1)
  NegativeFee,          ///< taker fee or maker rebate < 0 (R-11.1)
  OverflowHeadroom,     ///< worst-case notional lacks 100x int64 headroom
  BadRiskLimit,         ///< a participant risk limit is negative / inconsistent
  BadSessionLength,     ///< session length <= 0
  InitialRefOutOfBand,  ///< initial reference price outside the instrument band
};

[[nodiscard]] const char* to_cstr(ConfigError e) noexcept;

/// Flat per-lot maker-taker fees (R-11.1). Both are non-negative; the taker pays
/// the fee, the maker receives the rebate.
struct FeeSchedule {
  Cash taker_fee_per_lot{0};
  Cash maker_rebate_per_lot{0};
};

/// An instrument definition (R-1.1). `tick_size` and `lot_size` are in minor
/// units; prices/quantities elsewhere are tick/lot counts.
struct InstrumentConfig {
  InstrumentId id{};
  std::string symbol;
  std::int64_t tick_size{1};  ///< minor units per tick, > 0
  std::int64_t lot_size{1};   ///< units per lot, > 0
  Price min_price{1};         ///< ticks, >= 1
  Price max_price{1};         ///< ticks, > min_price
  Qty max_order_qty{1};       ///< lots, >= 1
  FeeSchedule fees{};

  /// Validates R-1.1/R-1.3/R-11.1 and the per-trade overflow-headroom bound.
  [[nodiscard]] std::optional<ConfigError> validate() const noexcept;
};

/// Pre-trade risk limits for one participant (EXCHANGE_RULES §9). `max_notional`
/// is reserved for a Release-2 check (0 = unused).
struct ParticipantRisk {
  Qty max_position_lots{std::numeric_limits<std::int64_t>::max()};
  Qty max_order_qty_lots{std::numeric_limits<std::int64_t>::max()};
  std::int64_t max_open_orders{std::numeric_limits<std::int64_t>::max()};
  Cash max_notional{0};  ///< reserved (R2); 0 = unused
};

/// A registered participant (R-2.1) and its risk profile.
struct ParticipantConfig {
  ParticipantId id{};
  ParticipantRisk risk{};

  [[nodiscard]] std::optional<ConfigError> validate() const noexcept;
};

/// Session parameters (R-12). `max_fills_estimate` bounds the session-aggregate
/// overflow check (a generous default); it is not a hard cap on fills.
struct SessionConfig {
  Duration length{Duration{1}};  ///< logical ns, > 0
  Price initial_reference{1};    ///< R-12.4 fallback mark; within band
  std::int64_t max_fills_estimate{1'000'000};

  /// Validates the session against an instrument (band membership, aggregate
  /// overflow headroom).
  [[nodiscard]] std::optional<ConfigError> validate(const InstrumentConfig& instr) const noexcept;
};

/// Exact trade notional in cash minor units (R-1.3):
/// `price_ticks * tick_size * qty_lots * lot_size`. Assumes the instrument was
/// validated, so for an in-band price and a valid quantity this cannot overflow.
[[nodiscard]] constexpr Cash Notional(Price price, Qty qty,
                                      const InstrumentConfig& instr) noexcept {
  return Cash{price.ticks() * instr.tick_size * qty.lots() * instr.lot_size};
}

}  // namespace microsim::core
