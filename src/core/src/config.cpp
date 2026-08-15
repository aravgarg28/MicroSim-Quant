#include "microsim/core/config.hpp"

namespace microsim::core {

namespace {

/// The int64 headroom bound: worst-case notional must not exceed this, leaving
/// >= 100x headroom before INT64_MAX (NUMERIC_REPRESENTATION overflow analysis).
constexpr std::int64_t kHeadroomLimit = std::numeric_limits<std::int64_t>::max() / 100;

/// True if a*b <= limit for non-negative a, b, limit, computed without
/// overflowing (the division cannot overflow and short-circuits the product).
[[nodiscard]] constexpr bool product_within(std::int64_t a, std::int64_t b,
                                            std::int64_t limit) noexcept {
  if (a == 0 || b == 0) {
    return true;
  }
  return a <= limit / b;
}

/// True if the product of all four non-negative factors stays within `limit`,
/// never overflowing intermediate results.
[[nodiscard]] constexpr bool product4_within(std::int64_t a, std::int64_t b, std::int64_t c,
                                             std::int64_t d, std::int64_t limit) noexcept {
  if (!product_within(a, b, limit)) {
    return false;
  }
  const std::int64_t ab = a * b;
  if (!product_within(ab, c, limit)) {
    return false;
  }
  const std::int64_t abc = ab * c;
  return product_within(abc, d, limit);
}

}  // namespace

const char* to_cstr(ConfigError e) noexcept {
  switch (e) {
    case ConfigError::BadTickSize:
      return "BAD_TICK_SIZE";
    case ConfigError::BadLotSize:
      return "BAD_LOT_SIZE";
    case ConfigError::MinPriceBelowOne:
      return "MIN_PRICE_BELOW_ONE";
    case ConfigError::MaxPriceNotAboveMin:
      return "MAX_PRICE_NOT_ABOVE_MIN";
    case ConfigError::BadMaxOrderQty:
      return "BAD_MAX_ORDER_QTY";
    case ConfigError::NegativeFee:
      return "NEGATIVE_FEE";
    case ConfigError::OverflowHeadroom:
      return "OVERFLOW_HEADROOM";
    case ConfigError::BadRiskLimit:
      return "BAD_RISK_LIMIT";
    case ConfigError::BadSessionLength:
      return "BAD_SESSION_LENGTH";
    case ConfigError::InitialRefOutOfBand:
      return "INITIAL_REF_OUT_OF_BAND";
  }
  return "?";
}

std::optional<ConfigError> InstrumentConfig::validate() const noexcept {
  if (tick_size <= 0) {
    return ConfigError::BadTickSize;
  }
  if (lot_size <= 0) {
    return ConfigError::BadLotSize;
  }
  if (min_price.ticks() < 1) {
    return ConfigError::MinPriceBelowOne;
  }
  if (max_price <= min_price) {
    return ConfigError::MaxPriceNotAboveMin;
  }
  if (max_order_qty.lots() < 1) {
    return ConfigError::BadMaxOrderQty;
  }
  if (fees.taker_fee_per_lot.minor() < 0 || fees.maker_rebate_per_lot.minor() < 0) {
    return ConfigError::NegativeFee;
  }
  // Per-trade worst case: max_price * tick_size * max_order_qty * lot_size.
  if (!product4_within(max_price.ticks(), tick_size, max_order_qty.lots(), lot_size,
                       kHeadroomLimit)) {
    return ConfigError::OverflowHeadroom;
  }
  return std::nullopt;
}

std::optional<ConfigError> ParticipantConfig::validate() const noexcept {
  if (risk.max_position_lots.lots() < 0 || risk.max_order_qty_lots.lots() < 1 ||
      risk.max_open_orders < 0 || risk.max_notional.minor() < 0) {
    return ConfigError::BadRiskLimit;
  }
  return std::nullopt;
}

std::optional<ConfigError> SessionConfig::validate(const InstrumentConfig& instr) const noexcept {
  if (length.ns() <= 0) {
    return ConfigError::BadSessionLength;
  }
  if (initial_reference < instr.min_price || initial_reference > instr.max_price) {
    return ConfigError::InitialRefOutOfBand;
  }
  if (max_fills_estimate < 1) {
    return ConfigError::BadSessionLength;
  }
  // Session-aggregate worst case: per-trade worst case * max_fills_estimate.
  // Compute the per-trade worst case within the limit first (instrument.validate
  // guarantees it fits), then fold in the fill count.
  if (!product_within(instr.max_price.ticks(), instr.tick_size, kHeadroomLimit)) {
    return ConfigError::OverflowHeadroom;
  }
  const std::int64_t t1 = instr.max_price.ticks() * instr.tick_size;
  if (!product_within(t1, instr.max_order_qty.lots(), kHeadroomLimit)) {
    return ConfigError::OverflowHeadroom;
  }
  const std::int64_t t2 = t1 * instr.max_order_qty.lots();
  if (!product_within(t2, instr.lot_size, kHeadroomLimit)) {
    return ConfigError::OverflowHeadroom;
  }
  const std::int64_t per_trade = t2 * instr.lot_size;
  if (!product_within(per_trade, max_fills_estimate, kHeadroomLimit)) {
    return ConfigError::OverflowHeadroom;
  }
  return std::nullopt;
}

}  // namespace microsim::core
