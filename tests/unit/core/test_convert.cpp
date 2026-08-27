#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "microsim/core/config.hpp"
#include "microsim/core/convert.hpp"

// R1-06: pins the decimal-string <-> integer boundary conversions
// (NUMERIC_REPRESENTATION.md §"Conversion and rounding rules"): exact parsing to
// minor units/ticks/lots, rejection (never rounding) of non-representable and
// non-tick-multiple values, and integer-only formatting back to decimal text.

namespace mc = microsim::core;

namespace {

mc::InstrumentConfig cent_instrument() {
  // 1-cent tick, 1-unit lot; prices quoted with 2 fractional digits.
  return mc::InstrumentConfig{.id = mc::InstrumentId{1},
                              .symbol = "SIM",
                              .tick_size = 1,
                              .lot_size = 1,
                              .min_price = mc::Price{1},
                              .max_price = mc::Price{100000},
                              .max_order_qty = mc::Qty{1000000}};
}

}  // namespace

// ----- parse_decimal_minor: the exact decimal parse ---------------------------

TEST(ParseDecimalMinor, WholeAndFraction) {
  EXPECT_EQ(mc::parse_decimal_minor("10.03", 2).value, 1003);
  EXPECT_EQ(mc::parse_decimal_minor("10", 2).value, 1000);  // padded to scale
  EXPECT_EQ(mc::parse_decimal_minor("0.01", 2).value, 1);
  EXPECT_EQ(mc::parse_decimal_minor("10.3", 2).value, 1030);  // one dp padded
  EXPECT_EQ(mc::parse_decimal_minor("10.", 2).value, 1000);   // trailing dot ok
  EXPECT_EQ(mc::parse_decimal_minor(".05", 2).value, 5);      // leading dot ok
}

TEST(ParseDecimalMinor, Sign) {
  EXPECT_EQ(mc::parse_decimal_minor("-10.03", 2).value, -1003);
  EXPECT_EQ(mc::parse_decimal_minor("+10.03", 2).value, 1003);
  EXPECT_EQ(mc::parse_decimal_minor("-0.00", 2).value, 0);
}

TEST(ParseDecimalMinor, ZeroFracDigitsIsIntegerParse) {
  EXPECT_EQ(mc::parse_decimal_minor("1000", 0).value, 1000);
  EXPECT_EQ(mc::parse_decimal_minor("1000.0", 0).error, mc::ConvertError::TooManyFractionDigits);
}

TEST(ParseDecimalMinor, TrailingZeroFormsAreEqual) {
  EXPECT_EQ(mc::parse_decimal_minor("10.30", 2).value, mc::parse_decimal_minor("10.3", 2).value);
  EXPECT_EQ(mc::parse_decimal_minor("10.00", 2).value, mc::parse_decimal_minor("10", 2).value);
}

TEST(ParseDecimalMinor, RejectsTooMuchPrecision) {
  EXPECT_EQ(mc::parse_decimal_minor("10.031", 2).error, mc::ConvertError::TooManyFractionDigits);
  EXPECT_EQ(mc::parse_decimal_minor("0.001", 2).error, mc::ConvertError::TooManyFractionDigits);
}

TEST(ParseDecimalMinor, RejectsBadFormat) {
  EXPECT_EQ(mc::parse_decimal_minor("", 2).error, mc::ConvertError::Empty);
  EXPECT_EQ(mc::parse_decimal_minor("   ", 2).error, mc::ConvertError::Empty);
  EXPECT_EQ(mc::parse_decimal_minor("1.2.3", 2).error, mc::ConvertError::BadFormat);
  EXPECT_EQ(mc::parse_decimal_minor("10a", 2).error, mc::ConvertError::BadFormat);
  EXPECT_EQ(mc::parse_decimal_minor("1 0", 2).error, mc::ConvertError::BadFormat);
  EXPECT_EQ(mc::parse_decimal_minor(".", 2).error, mc::ConvertError::BadFormat);
  EXPECT_EQ(mc::parse_decimal_minor("-", 2).error, mc::ConvertError::BadFormat);
}

TEST(ParseDecimalMinor, RejectsOverflow) {
  // 10^18 * 10 fractional-scale would blow int64; a 19-digit integer overflows.
  EXPECT_EQ(mc::parse_decimal_minor("9999999999999999999", 0).error, mc::ConvertError::Overflow);
}

// ----- price_from_decimal: tick-multiple enforcement --------------------------

TEST(PriceFromDecimal, ExactTicks) {
  mc::InstrumentConfig i = cent_instrument();
  EXPECT_EQ(mc::price_from_decimal("10.03", i, 2).value.ticks(), 1003);
  EXPECT_TRUE(mc::price_from_decimal("10.03", i, 2).ok());
}

TEST(PriceFromDecimal, NonTickMultipleRejected) {
  mc::InstrumentConfig i = cent_instrument();
  i.tick_size = 5;  // 5-minor-unit tick (a "nickel" tick)
  // 10.03 = 1003 minor units; 1003 % 5 != 0 -> INVALID_TICK, never rounded.
  EXPECT_EQ(mc::price_from_decimal("10.03", i, 2).error, mc::ConvertError::NotTickMultiple);
  // 10.05 = 1005 minor units; 1005 / 5 = 201 ticks.
  EXPECT_EQ(mc::price_from_decimal("10.05", i, 2).value.ticks(), 201);
}

TEST(PriceFromDecimal, NegativeRejected) {
  mc::InstrumentConfig i = cent_instrument();
  EXPECT_EQ(mc::price_from_decimal("-10.03", i, 2).error, mc::ConvertError::Negative);
}

// ----- qty_from_decimal -------------------------------------------------------

TEST(QtyFromDecimal, IntegerLots) {
  EXPECT_EQ(mc::qty_from_decimal("250", 1, 0).value.lots(), 250);
}

TEST(QtyFromDecimal, LotMultipleEnforced) {
  // lot_size 100 base units: 250 base units is not a whole lot.
  EXPECT_EQ(mc::qty_from_decimal("250", 100, 0).error, mc::ConvertError::NotLotMultiple);
  EXPECT_EQ(mc::qty_from_decimal("300", 100, 0).value.lots(), 3);
}

// ----- formatting round-trips (integer division/modulo, never doubles) --------

TEST(PriceToDecimal, PadsFraction) {
  mc::InstrumentConfig i = cent_instrument();
  EXPECT_EQ(mc::price_to_decimal(mc::Price{1003}, i, 2), "10.03");
  EXPECT_EQ(mc::price_to_decimal(mc::Price{1000}, i, 2), "10.00");
  EXPECT_EQ(mc::price_to_decimal(mc::Price{5}, i, 2), "0.05");
}

TEST(CashToDecimal, Signed) {
  EXPECT_EQ(mc::cash_to_decimal(mc::Cash{-1003}, 2), "-10.03");
  EXPECT_EQ(mc::cash_to_decimal(mc::Cash{0}, 2), "0.00");
  EXPECT_EQ(mc::cash_to_decimal(mc::Cash{1234567}, 0), "1234567");
}

TEST(Convert, StringToPriceRoundTrips) {
  mc::InstrumentConfig i = cent_instrument();
  for (const char* s : {"10.03", "0.01", "999.99", "10.00"}) {
    const auto p = mc::price_from_decimal(s, i, 2);
    ASSERT_TRUE(p.ok()) << s;
    EXPECT_EQ(mc::price_to_decimal(p.value, i, 2), std::string(s)) << s;
  }
}

TEST(QtyToDecimal, ScalesByLot) {
  EXPECT_EQ(mc::qty_to_decimal(mc::Qty{3}, 100, 0), "300");
  EXPECT_EQ(mc::qty_to_decimal(mc::Qty{250}, 1, 0), "250");
}
