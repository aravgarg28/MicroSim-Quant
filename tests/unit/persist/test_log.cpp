#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/persist/log.hpp"
#include "microsim/persist/wire.hpp"

// R1-21: pins the on-disk run log — the byte primitives, the CRC-framed
// reader/writer, and the field-wise message/event encoding that replay round-
// trips (the on-disk face of INV-10). Corruption and truncation must be caught,
// not silently mis-read.

namespace mp = microsim::persist;
namespace mc = microsim::core;

using mc::Price;
using mc::Qty;
using mc::Side;

// ----- CRC-32 -----------------------------------------------------------------

TEST(PersistCrc, KnownVectorAndSensitivity) {
  // Standard CRC-32/IEEE check value for "123456789".
  EXPECT_EQ(mp::crc32("123456789"), 0xCBF43926U);
  EXPECT_EQ(mp::crc32(""), 0x00000000U);
  EXPECT_NE(mp::crc32("hello"), mp::crc32("hellp"));  // one-bit change moves the CRC
}

// ----- byte primitives round-trip ---------------------------------------------

TEST(PersistBytes, LittleEndianRoundTrip) {
  mp::ByteWriter w;
  w.u8(0xAB);
  w.u16(0x1234);
  w.u32(0xDEADBEEF);
  w.u64(0x0123456789ABCDEFULL);
  w.i64(-42);

  mp::ByteReader r(w.bytes());
  EXPECT_EQ(r.u8(), 0xABU);
  EXPECT_EQ(r.u16(), 0x1234U);
  EXPECT_EQ(r.u32(), 0xDEADBEEFU);
  EXPECT_EQ(r.u64(), 0x0123456789ABCDEFULL);
  EXPECT_EQ(r.i64(), -42);
  EXPECT_TRUE(r.at_end());
  EXPECT_TRUE(r.ok());
}

TEST(PersistBytes, ReadPastEndLatchesNotOk) {
  mp::ByteWriter w;
  w.u16(7);
  mp::ByteReader r(w.bytes());
  EXPECT_EQ(r.u16(), 7U);
  (void)r.u32();  // nothing left
  EXPECT_FALSE(r.ok());
}

// ----- framed log writer/reader round-trip ------------------------------------

TEST(PersistLog, WriteReadRecordsRoundTrip) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  {
    mp::LogWriter writer(ss, mp::LogKind::Input);
    writer.write_record("first");
    writer.write_record("");  // empty record is legal
    writer.write_record(std::string("with\0null", 9));
  }
  mp::LogReader reader(ss);
  EXPECT_EQ(reader.kind(), mp::LogKind::Input);
  EXPECT_EQ(reader.next_record(), std::optional<std::string>{"first"});
  EXPECT_EQ(reader.next_record(), std::optional<std::string>{""});
  EXPECT_EQ(reader.next_record(), std::optional<std::string>{std::string("with\0null", 9)});
  EXPECT_EQ(reader.next_record(), std::nullopt);  // clean end
}

TEST(PersistLog, RejectsBadMagic) {
  std::stringstream ss("not-a-microsim-log-at-all", std::ios::in | std::ios::binary);
  EXPECT_THROW(mp::LogReader{ss}, mp::LogError);
}

TEST(PersistLog, DetectsCorruptedRecord) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  {
    mp::LogWriter writer(ss, mp::LogKind::Event);
    writer.write_record("payload-bytes");
  }
  std::string blob = ss.str();
  blob[blob.size() - 5] ^= 0x01;  // flip a bit inside the payload; CRC no longer matches
  std::stringstream corrupt(blob, std::ios::in | std::ios::binary);
  mp::LogReader reader(corrupt);
  EXPECT_THROW((void)reader.next_record(), mp::LogError);
}

TEST(PersistLog, DetectsTruncatedRecord) {
  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  {
    mp::LogWriter writer(ss, mp::LogKind::Event);
    writer.write_record("a-decent-sized-record");
  }
  std::string blob = ss.str();
  blob.resize(blob.size() - 3);  // lose the tail
  std::stringstream truncated(blob, std::ios::in | std::ios::binary);
  mp::LogReader reader(truncated);
  EXPECT_THROW((void)reader.next_record(), mp::LogError);
}

// ----- wire encode/decode for every message and event -------------------------

TEST(PersistWire, InboundRoundTripAllVariants) {
  const std::vector<mc::Inbound> msgs = {
      mc::NewOrder{.participant = mc::ParticipantId{7},
                   .client_order_id = mc::ClientOrderId{99},
                   .instrument = mc::InstrumentId{1},
                   .side = Side::Sell,
                   .type = mc::OrderType::Limit,
                   .qty = Qty{25},
                   .price = Price{1003}},
      mc::CancelOrder{.participant = mc::ParticipantId{7}, .order_id = mc::OrderId{3}},
      mc::ModifyOrder{.participant = mc::ParticipantId{2},
                      .order_id = mc::OrderId{5},
                      .new_qty = Qty{40},
                      .new_price = Price{1001}},
      mc::SessionEnd{},
  };
  for (const mc::Inbound& original : msgs) {
    const mc::SimTime ts{123456789};
    const std::string bytes = mp::encode_input(ts, original);
    mc::SimTime got_ts{};
    mc::Inbound got;
    ASSERT_TRUE(mp::decode_input(bytes, got_ts, got));
    EXPECT_EQ(got_ts, ts);
    EXPECT_EQ(got.index(), original.index());
    EXPECT_TRUE(got == original);
  }
}

TEST(PersistWire, EventRoundTripAllVariants) {
  const mc::EventHeader h{
      .seq_out = mc::Seq{11}, .seq_in = mc::Seq{4}, .ts_event = mc::SimTime{50}};
  const std::vector<mc::Outbound> events = {
      mc::OrderAccepted{.order_id = mc::OrderId{3},
                        .participant = mc::ParticipantId{7},
                        .client_order_id = mc::ClientOrderId{99}},
      mc::OrderRejected{.participant = mc::ParticipantId{7},
                        .client_order_id = mc::ClientOrderId{99},
                        .order_id = mc::OrderId{},
                        .reason = mc::RejectReason::PriceOutOfBands},
      mc::OrderCanceled{.order_id = mc::OrderId{3},
                        .participant = mc::ParticipantId{7},
                        .remaining_qty = Qty{5},
                        .reason = mc::CancelReason::ByRequest},
      mc::OrderModified{.order_id = mc::OrderId{5},
                        .participant = mc::ParticipantId{2},
                        .new_qty = Qty{40},
                        .new_price = Price{1001}},
      mc::Fill{.order_id = mc::OrderId{3},
               .participant = mc::ParticipantId{7},
               .trade_id = mc::TradeId{1},
               .price = Price{1003},
               .qty = Qty{10},
               .fee = mc::Cash{-1},
               .liquidity = mc::LiquidityFlag::Maker},
      mc::Trade{.trade_id = mc::TradeId{1},
                .price = Price{1003},
                .qty = Qty{10},
                .maker_order_id = mc::OrderId{3},
                .taker_order_id = mc::OrderId{9},
                .maker_participant = mc::ParticipantId{7},
                .taker_participant = mc::ParticipantId{9},
                .aggressor = Side::Buy},
  };
  for (const mc::Outbound& payload : events) {
    const mc::SequencedEvent original{.header = h, .event = payload};
    const std::string bytes = mp::encode_event(original);
    mc::SequencedEvent got;
    ASSERT_TRUE(mp::decode_event(bytes, got));
    EXPECT_TRUE(got == original);
  }
}

TEST(PersistWire, UnknownTagIsRejected) {
  // A record whose tag byte is out of range must fail to decode, not be guessed.
  std::string bad = mp::encode_input(mc::SimTime{0}, mc::Inbound{mc::SessionEnd{}});
  bad.back() = static_cast<char>(0x7F);  // corrupt the tag (last byte, SessionEnd has no fields)
  mc::SimTime ts{};
  mc::Inbound out;
  EXPECT_FALSE(mp::decode_input(bad, ts, out));
}

// ----- end-to-end: a stream of sequenced events through the framed log --------

TEST(PersistLog, EventLogEndToEnd) {
  std::vector<mc::SequencedEvent> written;
  for (std::uint64_t i = 1; i <= 5; ++i) {
    written.push_back(
        mc::SequencedEvent{.header = {.seq_out = mc::Seq{i},
                                      .seq_in = mc::Seq{i},
                                      .ts_event = mc::SimTime{static_cast<std::int64_t>(i)}},
                           .event = mc::OrderAccepted{.order_id = mc::OrderId{i},
                                                      .participant = mc::ParticipantId{1},
                                                      .client_order_id = mc::ClientOrderId{i}}});
  }

  std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
  {
    mp::LogWriter writer(ss, mp::LogKind::Event);
    for (const auto& ev : written) {
      writer.write_record(mp::encode_event(ev));
    }
  }

  mp::LogReader reader(ss);
  std::vector<mc::SequencedEvent> read_back;
  while (const std::optional<std::string> rec = reader.next_record()) {
    mc::SequencedEvent ev;
    ASSERT_TRUE(mp::decode_event(*rec, ev));
    read_back.push_back(ev);
  }
  EXPECT_EQ(read_back, written);
}
