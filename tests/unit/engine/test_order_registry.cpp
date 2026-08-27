#include <optional>

#include <gtest/gtest.h>

#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/order_registry.hpp"
#include "microsim/engine/venue.hpp"

// R1-10: pins order identity/lifecycle (R-4.1/R-4.2) and the gateway validation
// chain (R-3.3 items 1-8). Validation order is proven by multi-defect messages:
// when two rules would each fail, the earlier one must win.

namespace me = microsim::engine;
namespace mc = microsim::core;

using mc::ClientOrderId;
using mc::InstrumentId;
using mc::OrderId;
using mc::OrderType;
using mc::ParticipantId;
using mc::Price;
using mc::Qty;
using mc::RejectReason;
using mc::Side;

namespace {

constexpr InstrumentId kInstr{1};
constexpr ParticipantId kParty{7};

// $5.00-$15.00 band, 1-cent tick, max 1000 lots/order.
mc::InstrumentConfig instrument() {
  return mc::InstrumentConfig{.id = kInstr,
                              .symbol = "SIM",
                              .tick_size = 1,
                              .lot_size = 1,
                              .min_price = Price{500},
                              .max_price = Price{1500},
                              .max_order_qty = Qty{1000}};
}

me::Venue venue_with_one_instrument() {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(mc::ParticipantConfig{.id = kParty});
  return v;
}

// A valid limit order the tests mutate one field at a time to trip each rule.
mc::NewOrder good_limit(std::uint64_t clord = 1) {
  return mc::NewOrder{.participant = kParty,
                      .client_order_id = ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = Side::Buy,
                      .type = OrderType::Limit,
                      .qty = Qty{10},
                      .price = Price{1000}};
}

}  // namespace

// ----- happy path -------------------------------------------------------------

TEST(OrderRegistry, ValidLimitPasses) {
  const me::Venue v = venue_with_one_instrument();
  me::OrderRegistry reg;
  EXPECT_EQ(reg.validate_new(good_limit(), v), std::nullopt);
}

// ----- each rule, in isolation ------------------------------------------------

TEST(OrderRegistry, EachValidationRuleFires) {
  const me::Venue v = venue_with_one_instrument();
  const me::OrderRegistry reg;

  auto bad_instrument = good_limit();
  bad_instrument.instrument = InstrumentId{99};
  EXPECT_EQ(reg.validate_new(bad_instrument, v), RejectReason::UnknownInstrument);

  auto bad_party = good_limit();
  bad_party.participant = ParticipantId{99};
  EXPECT_EQ(reg.validate_new(bad_party, v), RejectReason::UnknownParticipant);

  auto priced_market = good_limit();
  priced_market.type = OrderType::Market;  // price 1000 is left set -> illegal
  EXPECT_EQ(reg.validate_new(priced_market, v), RejectReason::PriceOnMarketOrder);

  auto zero_qty = good_limit();
  zero_qty.qty = Qty{0};
  EXPECT_EQ(reg.validate_new(zero_qty, v), RejectReason::InvalidQty);

  auto too_big = good_limit();
  too_big.qty = Qty{1001};
  EXPECT_EQ(reg.validate_new(too_big, v), RejectReason::OrderTooLarge);

  auto out_of_band = good_limit();
  out_of_band.price = Price{400};  // below min 500
  EXPECT_EQ(reg.validate_new(out_of_band, v), RejectReason::PriceOutOfBands);

  auto valid_market = good_limit();
  valid_market.type = OrderType::Market;
  valid_market.price = Price{};  // market with no price is fine
  EXPECT_EQ(reg.validate_new(valid_market, v), std::nullopt);
}

// ----- ordering: earlier rule wins over a later one (first-failure-wins) ------

TEST(OrderRegistry, FirstFailureWinsAcrossMultipleDefects) {
  const me::Venue v = venue_with_one_instrument();
  const me::OrderRegistry reg;

  // Unknown instrument AND unknown participant -> item 1 (instrument) wins.
  auto m1 = good_limit();
  m1.instrument = InstrumentId{99};
  m1.participant = ParticipantId{99};
  EXPECT_EQ(reg.validate_new(m1, v), RejectReason::UnknownInstrument);

  // Zero qty AND out-of-band price -> item 5 (qty) beats item 7 (price).
  auto m2 = good_limit();
  m2.qty = Qty{0};
  m2.price = Price{400};
  EXPECT_EQ(reg.validate_new(m2, v), RejectReason::InvalidQty);

  // Too-large qty AND out-of-band price -> item 6 (size) beats item 7 (price).
  auto m3 = good_limit();
  m3.qty = Qty{5000};
  m3.price = Price{99999};
  EXPECT_EQ(reg.validate_new(m3, v), RejectReason::OrderTooLarge);
}

// ----- client-order-id dedup (item 8), recorded only on create ----------------

TEST(OrderRegistry, DuplicateClientOrderIdRejectedAfterCreate) {
  const me::Venue v = venue_with_one_instrument();
  me::OrderRegistry reg;

  // Before creation, the id is free.
  EXPECT_EQ(reg.validate_new(good_limit(42), v), std::nullopt);
  reg.create(good_limit(42));
  // Now the same participant reusing 42 is a duplicate...
  EXPECT_EQ(reg.validate_new(good_limit(42), v), RejectReason::DuplicateClientOrderId);
  // ...but a different client id is fine.
  EXPECT_EQ(reg.validate_new(good_limit(43), v), std::nullopt);

  // A *different* participant may reuse client id 42 (dedup is per participant).
  me::Venue v2 = v;
  v2.add_participant(mc::ParticipantConfig{.id = ParticipantId{8}});
  auto other = good_limit(42);
  other.participant = ParticipantId{8};
  EXPECT_EQ(reg.validate_new(other, v2), std::nullopt);
}

// ----- identity: order_ids strictly increasing from 1 (R-4.1) -----------------

TEST(OrderRegistry, OrderIdsStrictlyIncreasing) {
  me::OrderRegistry reg;
  const OrderId a = reg.create(good_limit(1));
  const OrderId b = reg.create(good_limit(2));
  const OrderId c = reg.create(good_limit(3));
  EXPECT_EQ(a, OrderId{1});
  EXPECT_EQ(b, OrderId{2});
  EXPECT_EQ(c, OrderId{3});
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
}

// ----- lifecycle: fills accumulate, FILLED is terminal (R-4.2/R-4.3) ----------

TEST(OrderRegistry, FillsAccumulateToFilledTerminal) {
  me::OrderRegistry reg;
  const OrderId id = reg.create(good_limit());  // qty 10
  me::OrderRecord* r = reg.lookup(id);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->state, me::OrderState::Live);
  EXPECT_EQ(r->remaining(), Qty{10});

  reg.apply_fill(id, Qty{4});
  EXPECT_EQ(r->remaining(), Qty{6});
  EXPECT_FALSE(r->terminal());

  reg.apply_fill(id, Qty{6});  // completes the order
  EXPECT_EQ(r->remaining(), Qty{0});
  EXPECT_EQ(r->state, me::OrderState::Filled);
  EXPECT_TRUE(r->terminal());
}

TEST(OrderRegistry, FinalizeCancelsLiveOrder) {
  me::OrderRegistry reg;
  const OrderId id = reg.create(good_limit());
  reg.finalize(id, me::OrderState::Canceled);
  EXPECT_EQ(reg.lookup(id)->state, me::OrderState::Canceled);
  EXPECT_TRUE(reg.lookup(id)->terminal());
}

TEST(OrderRegistry, LookupUnknownIsNull) {
  me::OrderRegistry reg;
  EXPECT_EQ(reg.lookup(OrderId{123}), nullptr);
}
