#include <cstdint>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/sequencer.hpp"
#include "microsim/engine/venue.hpp"

// R1-11: pins the sequencer (R-10). Every inbound message gets a strictly
// increasing seq in arrival order (R-10.1); every outbound event carries a
// gap-free seq_out, the triggering seq_in, and ts_event (R-10.2); and replaying
// the same sequenced stream yields an equal event stream (R-10.3).

namespace me = microsim::engine;
namespace mc = microsim::core;
namespace mb = microsim::book;

using mc::ClientOrderId;
using mc::InstrumentId;
using mc::OrderId;
using mc::OrderType;
using mc::ParticipantId;
using mc::Price;
using mc::Qty;
using mc::Seq;
using mc::Side;
using mc::SimTime;

namespace {

constexpr InstrumentId kInstr{1};

mc::InstrumentConfig instrument() {
  return mc::InstrumentConfig{.id = kInstr,
                              .symbol = "SIM",
                              .tick_size = 1,
                              .lot_size = 1,
                              .min_price = Price{500},
                              .max_price = Price{1500},
                              .max_order_qty = Qty{1000}};
}

using Seqr = me::Sequencer<mb::ReferenceBook>;

Seqr make_sequencer() {
  me::Venue v;
  v.add_instrument(instrument());
  v.add_participant(mc::ParticipantConfig{.id = ParticipantId{1}});
  v.add_participant(mc::ParticipantConfig{.id = ParticipantId{9}});
  return Seqr{v, instrument()};
}

mc::NewOrder limit(ParticipantId p, std::uint64_t clord, Side side, std::int64_t px,
                   std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = OrderType::Limit,
                      .qty = Qty{qty},
                      .price = Price{px}};
}

}  // namespace

// ----- R-10.1: inbound seq is strictly increasing in arrival order ------------

TEST(Sequencer, InboundSeqIncreasesPerMessage) {
  Seqr s = make_sequencer();
  EXPECT_EQ(s.next_seq(), Seq::first());

  auto e1 = s.submit(limit(ParticipantId{1}, 1, Side::Buy, 1000, 5), SimTime{100});
  auto e2 = s.submit(limit(ParticipantId{1}, 2, Side::Buy, 1001, 5), SimTime{200});

  // Each accept is triggered by its own message: seq_in 1 then 2, gap-free.
  ASSERT_EQ(e1.size(), 1u);
  ASSERT_EQ(e2.size(), 1u);
  EXPECT_EQ(e1.front().header.seq_in, Seq{1});
  EXPECT_EQ(e2.front().header.seq_in, Seq{2});
  EXPECT_EQ(s.next_seq(), Seq{3});
}

// ----- R-10.2: seq_out is gap-free across events AND across messages ----------

TEST(Sequencer, OutboundSeqIsGapFreeAcrossAllEvents) {
  Seqr s = make_sequencer();
  s.submit(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10), SimTime{10});  // #1 rests (1 event)
  // A crossing buy produces maker Fill, taker Fill, Trade = three events at once.
  auto trade_evs = s.submit(limit(ParticipantId{9}, 1, Side::Buy, 1003, 10), SimTime{20});

  // Collect every seq_out emitted so far and require 1,2,3,4 with no gaps.
  std::vector<std::uint64_t> seq_outs;
  seq_outs.push_back(1);  // the resting order's OrderAccepted from the first submit
  for (const auto& ev : trade_evs) {
    seq_outs.push_back(ev.header.seq_out.value());
  }
  ASSERT_EQ(trade_evs.size(), 4u);  // taker OrderAccepted + maker Fill + taker Fill + Trade
  const std::vector<std::uint64_t> expected{1, 2, 3, 4, 5};
  EXPECT_EQ(seq_outs, expected);
  EXPECT_EQ(s.next_seq_out(), Seq{6});
}

// ----- R-10.2: seq_in links every event back to its triggering message --------

TEST(Sequencer, EventsCarryTriggeringSeqIn) {
  Seqr s = make_sequencer();
  s.submit(limit(ParticipantId{1}, 1, Side::Sell, 1003, 10), SimTime{10});            // seq_in 1
  auto evs = s.submit(limit(ParticipantId{9}, 1, Side::Buy, 1003, 10), SimTime{20});  // seq_in 2
  ASSERT_FALSE(evs.empty());
  for (const auto& ev : evs) {
    EXPECT_EQ(ev.header.seq_in, Seq{2});  // all triggered by the second message
  }
}

// ----- R-10.2: ts_event is the logical time the message was processed ---------

TEST(Sequencer, TsEventStampsSubmissionTime) {
  Seqr s = make_sequencer();
  auto evs = s.submit(limit(ParticipantId{1}, 1, Side::Buy, 1000, 5), SimTime{4242});
  ASSERT_EQ(evs.size(), 1u);
  EXPECT_EQ(evs.front().header.ts_event, SimTime{4242});
  EXPECT_EQ(s.clock(), SimTime{4242});
}

// ----- rejects are sequenced too (they are outbound events) -------------------

TEST(Sequencer, RejectConsumesInboundSeqAndCarriesSeqOut) {
  Seqr s = make_sequencer();
  // Unknown participant -> the message is still sequenced, the reject is stamped.
  auto evs = s.submit(limit(ParticipantId{7}, 1, Side::Buy, 1000, 5), SimTime{5});
  ASSERT_EQ(evs.size(), 1u);
  ASSERT_TRUE(std::holds_alternative<mc::OrderRejected>(evs.front().event));
  EXPECT_EQ(evs.front().header.seq_in, Seq{1});
  EXPECT_EQ(evs.front().header.seq_out, Seq{1});
  EXPECT_EQ(s.next_seq(), Seq{2});      // the rejected message still consumed a seq
  EXPECT_EQ(s.next_seq_out(), Seq{2});  // and its reject consumed a seq_out
}

// ----- R-10.3: replaying the same stream yields an equal event stream ----------

TEST(Sequencer, ReplayIsDeterministic) {
  const std::vector<std::pair<mc::Inbound, SimTime>> script{
      {limit(ParticipantId{1}, 1, Side::Buy, 1000, 5), SimTime{1}},
      {limit(ParticipantId{9}, 1, Side::Sell, 1002, 8), SimTime{2}},
      {limit(ParticipantId{9}, 2, Side::Sell, 1000, 3), SimTime{3}},  // crosses the bid
      {mc::CancelOrder{.participant = ParticipantId{1}, .order_id = OrderId{1}}, SimTime{4}},
  };

  auto run = [&script]() {
    Seqr s = make_sequencer();
    std::vector<mc::SequencedEvent> all;
    for (const auto& [msg, ts] : script) {
      auto evs = s.submit(msg, ts);
      all.insert(all.end(), evs.begin(), evs.end());
    }
    return all;
  };

  const std::vector<mc::SequencedEvent> a = run();
  const std::vector<mc::SequencedEvent> b = run();
  EXPECT_EQ(a, b);          // R-10.3: byte-for-byte identical outbound stream
  EXPECT_FALSE(a.empty());  // and it actually did something
}
