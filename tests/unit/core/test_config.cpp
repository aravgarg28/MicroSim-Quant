#include <cstdint>
#include <limits>
#include <optional>

#include <gtest/gtest.h>

#include "microsim/core/config.hpp"

// R1-05: pins InstrumentConfig/ParticipantConfig/SessionConfig validation
// (EXCHANGE_RULES §1/§9/§11) and the exact Notional computation (R-1.3), plus
// the overflow-headroom bound from NUMERIC_REPRESENTATION.md.

namespace mc = microsim::core;

namespace {

// A valid instrument: $5.00-$15.00 band, 1-cent tick, 1-unit lot, 2c/1c fees.
mc::InstrumentConfig good_instrument() {
  return mc::InstrumentConfig{
      .id = mc::InstrumentId{1},
      .symbol = "SIM",
      .tick_size = 1,
      .lot_size = 1,
      .min_price = mc::Price{500},
      .max_price = mc::Price{1500},
      .max_order_qty = mc::Qty{1000},
      .fees = {.taker_fee_per_lot = mc::Cash{2}, .maker_rebate_per_lot = mc::Cash{1}}};
}

}  // namespace

// ----- Notional (R-1.3) -------------------------------------------------------

TEST(Notional, ExactIntegerProduct) {
  mc::InstrumentConfig i = good_instrument();
  // 10 lots @ 1003 ticks, tick=1c, lot=1 → 1003 * 1 * 10 * 1 = 10030 cents.
  EXPECT_EQ(mc::Notional(mc::Price{1003}, mc::Qty{10}, i).minor(), 10030);
}

TEST(Notional, ScalesWithTickAndLot) {
  mc::InstrumentConfig i = good_instrument();
  i.tick_size = 5;  // 5 minor units/tick
  i.lot_size = 100;
  // 1200 * 5 * 3 * 100 = 1,800,000
  EXPECT_EQ(mc::Notional(mc::Price{1200}, mc::Qty{3}, i).minor(), 1'800'000);
}

// ----- InstrumentConfig::validate (R-1.1/R-11.1) ------------------------------

TEST(InstrumentValidate, AcceptsGoodConfig) {
  EXPECT_EQ(good_instrument().validate(), std::nullopt);
}

TEST(InstrumentValidate, RejectsEachBadField) {
  {
    auto i = good_instrument();
    i.tick_size = 0;
    EXPECT_EQ(i.validate(), mc::ConfigError::BadTickSize);
  }
  {
    auto i = good_instrument();
    i.lot_size = -1;
    EXPECT_EQ(i.validate(), mc::ConfigError::BadLotSize);
  }
  {
    auto i = good_instrument();
    i.min_price = mc::Price{0};
    EXPECT_EQ(i.validate(), mc::ConfigError::MinPriceBelowOne);
  }
  {
    auto i = good_instrument();
    i.max_price = i.min_price;  // not strictly above
    EXPECT_EQ(i.validate(), mc::ConfigError::MaxPriceNotAboveMin);
  }
  {
    auto i = good_instrument();
    i.max_order_qty = mc::Qty{0};
    EXPECT_EQ(i.validate(), mc::ConfigError::BadMaxOrderQty);
  }
  {
    auto i = good_instrument();
    i.fees.taker_fee_per_lot = mc::Cash{-1};
    EXPECT_EQ(i.validate(), mc::ConfigError::NegativeFee);
  }
}

TEST(InstrumentValidate, OverflowHeadroomBoundary) {
  // Choose factors whose product just exceeds INT64_MAX/100 → rejected, and a
  // slightly smaller one that is accepted. Use a huge tick_size to trip it.
  auto i = good_instrument();
  i.max_price = mc::Price{1'000'000};
  i.max_order_qty = mc::Qty{1'000'000};
  i.tick_size = 1'000'000;
  i.lot_size = 1'000'000;  // 1e6^4 = 1e24 >> 9.2e16/100 → overflow
  EXPECT_EQ(i.validate(), mc::ConfigError::OverflowHeadroom);

  // A modest instrument is comfortably within headroom.
  auto j = good_instrument();
  j.max_price = mc::Price{100'000};
  j.max_order_qty = mc::Qty{100'000};
  j.tick_size = 100;
  j.lot_size = 1;  // 1e5 * 1e2 * 1e5 * 1 = 1e12 < 9.2e14
  EXPECT_EQ(j.validate(), std::nullopt);
}

// ----- ParticipantConfig::validate (§9) ---------------------------------------

TEST(ParticipantValidate, AcceptsAndRejects) {
  mc::ParticipantConfig p{.id = mc::ParticipantId{1},
                          .risk = {.max_position_lots = mc::Qty{500},
                                   .max_order_qty_lots = mc::Qty{100},
                                   .max_open_orders = 50,
                                   .max_notional = mc::Cash{0}}};
  EXPECT_EQ(p.validate(), std::nullopt);

  p.risk.max_open_orders = -1;
  EXPECT_EQ(p.validate(), mc::ConfigError::BadRiskLimit);

  p.risk.max_open_orders = 50;
  p.risk.max_order_qty_lots = mc::Qty{0};  // must be >= 1
  EXPECT_EQ(p.validate(), mc::ConfigError::BadRiskLimit);
}

// ----- SessionConfig::validate (§12) ------------------------------------------

TEST(SessionValidate, BandAndLength) {
  auto instr = good_instrument();
  mc::SessionConfig s{.length = mc::Duration{600'000'000'000},
                      .initial_reference = mc::Price{1000},
                      .max_fills_estimate = 1'000'000};
  EXPECT_EQ(s.validate(instr), std::nullopt);

  s.length = mc::Duration{0};
  EXPECT_EQ(s.validate(instr), mc::ConfigError::BadSessionLength);

  s.length = mc::Duration{1000};
  s.initial_reference = mc::Price{400};  // below band
  EXPECT_EQ(s.validate(instr), mc::ConfigError::InitialRefOutOfBand);
}

TEST(SessionValidate, AggregateOverflowRejected) {
  auto instr = good_instrument();
  instr.max_price = mc::Price{1'000'000};
  instr.tick_size = 1'000'000;
  instr.max_order_qty = mc::Qty{1'000'000};  // per-trade already large
  mc::SessionConfig s{.length = mc::Duration{1000},
                      .initial_reference = mc::Price{1'000'000},
                      .max_fills_estimate = 1'000'000};
  EXPECT_EQ(s.validate(instr), mc::ConfigError::OverflowHeadroom);
}

// ----- ConfigError names ------------------------------------------------------

TEST(ConfigError, HasNames) {
  EXPECT_STREQ(mc::to_cstr(mc::ConfigError::OverflowHeadroom), "OVERFLOW_HEADROOM");
  EXPECT_STREQ(mc::to_cstr(mc::ConfigError::BadTickSize), "BAD_TICK_SIZE");
}
