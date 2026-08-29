#include "microsim/persist/wire.hpp"

#include <variant>

#include "microsim/persist/log.hpp"

namespace microsim::persist {

namespace mc = microsim::core;

namespace {

// ----- id / value helpers -----------------------------------------------------

void put_price(ByteWriter& w, mc::Price p) {
  w.i64(p.ticks());
}

void put_qty(ByteWriter& w, mc::Qty q) {
  w.i64(q.lots());
}

void put_cash(ByteWriter& w, mc::Cash c) {
  w.i64(c.minor());
}

mc::Price get_price(ByteReader& r) {
  return mc::Price{r.i64()};
}

mc::Qty get_qty(ByteReader& r) {
  return mc::Qty{r.i64()};
}

mc::Cash get_cash(ByteReader& r) {
  return mc::Cash{r.i64()};
}

// ----- inbound payloads -------------------------------------------------------

void encode_new(ByteWriter& w, const mc::NewOrder& m) {
  w.u32(m.participant.value());
  w.u64(m.client_order_id.value());
  w.u32(m.instrument.value());
  w.u8(static_cast<std::uint8_t>(m.side));
  w.u8(static_cast<std::uint8_t>(m.type));
  put_qty(w, m.qty);
  put_price(w, m.price);
}

mc::NewOrder decode_new(ByteReader& r) {
  mc::NewOrder m{};
  m.participant = mc::ParticipantId{r.u32()};
  m.client_order_id = mc::ClientOrderId{r.u64()};
  m.instrument = mc::InstrumentId{r.u32()};
  m.side = static_cast<mc::Side>(r.u8());
  m.type = static_cast<mc::OrderType>(r.u8());
  m.qty = get_qty(r);
  m.price = get_price(r);
  return m;
}

void encode_cancel(ByteWriter& w, const mc::CancelOrder& m) {
  w.u32(m.participant.value());
  w.u64(m.order_id.value());
}

mc::CancelOrder decode_cancel(ByteReader& r) {
  mc::CancelOrder m{};
  m.participant = mc::ParticipantId{r.u32()};
  m.order_id = mc::OrderId{r.u64()};
  return m;
}

void encode_modify(ByteWriter& w, const mc::ModifyOrder& m) {
  w.u32(m.participant.value());
  w.u64(m.order_id.value());
  put_qty(w, m.new_qty);
  put_price(w, m.new_price);
}

mc::ModifyOrder decode_modify(ByteReader& r) {
  mc::ModifyOrder m{};
  m.participant = mc::ParticipantId{r.u32()};
  m.order_id = mc::OrderId{r.u64()};
  m.new_qty = get_qty(r);
  m.new_price = get_price(r);
  return m;
}

// ----- outbound payloads ------------------------------------------------------

void encode_accepted(ByteWriter& w, const mc::OrderAccepted& e) {
  w.u64(e.order_id.value());
  w.u32(e.participant.value());
  w.u64(e.client_order_id.value());
}

mc::OrderAccepted decode_accepted(ByteReader& r) {
  mc::OrderAccepted e{};
  e.order_id = mc::OrderId{r.u64()};
  e.participant = mc::ParticipantId{r.u32()};
  e.client_order_id = mc::ClientOrderId{r.u64()};
  return e;
}

void encode_rejected(ByteWriter& w, const mc::OrderRejected& e) {
  w.u32(e.participant.value());
  w.u64(e.client_order_id.value());
  w.u64(e.order_id.value());
  w.u8(static_cast<std::uint8_t>(e.reason));
}

mc::OrderRejected decode_rejected(ByteReader& r) {
  mc::OrderRejected e{};
  e.participant = mc::ParticipantId{r.u32()};
  e.client_order_id = mc::ClientOrderId{r.u64()};
  e.order_id = mc::OrderId{r.u64()};
  e.reason = static_cast<mc::RejectReason>(r.u8());
  return e;
}

void encode_canceled(ByteWriter& w, const mc::OrderCanceled& e) {
  w.u64(e.order_id.value());
  w.u32(e.participant.value());
  put_qty(w, e.remaining_qty);
  w.u8(static_cast<std::uint8_t>(e.reason));
}

mc::OrderCanceled decode_canceled(ByteReader& r) {
  mc::OrderCanceled e{};
  e.order_id = mc::OrderId{r.u64()};
  e.participant = mc::ParticipantId{r.u32()};
  e.remaining_qty = get_qty(r);
  e.reason = static_cast<mc::CancelReason>(r.u8());
  return e;
}

void encode_modified(ByteWriter& w, const mc::OrderModified& e) {
  w.u64(e.order_id.value());
  w.u32(e.participant.value());
  put_qty(w, e.new_qty);
  put_price(w, e.new_price);
}

mc::OrderModified decode_modified(ByteReader& r) {
  mc::OrderModified e{};
  e.order_id = mc::OrderId{r.u64()};
  e.participant = mc::ParticipantId{r.u32()};
  e.new_qty = get_qty(r);
  e.new_price = get_price(r);
  return e;
}

void encode_fill(ByteWriter& w, const mc::Fill& e) {
  w.u64(e.order_id.value());
  w.u32(e.participant.value());
  w.u64(e.trade_id.value());
  put_price(w, e.price);
  put_qty(w, e.qty);
  put_cash(w, e.fee);
  w.u8(static_cast<std::uint8_t>(e.liquidity));
}

mc::Fill decode_fill(ByteReader& r) {
  mc::Fill e{};
  e.order_id = mc::OrderId{r.u64()};
  e.participant = mc::ParticipantId{r.u32()};
  e.trade_id = mc::TradeId{r.u64()};
  e.price = get_price(r);
  e.qty = get_qty(r);
  e.fee = get_cash(r);
  e.liquidity = static_cast<mc::LiquidityFlag>(r.u8());
  return e;
}

void encode_trade(ByteWriter& w, const mc::Trade& e) {
  w.u64(e.trade_id.value());
  put_price(w, e.price);
  put_qty(w, e.qty);
  w.u64(e.maker_order_id.value());
  w.u64(e.taker_order_id.value());
  w.u32(e.maker_participant.value());
  w.u32(e.taker_participant.value());
  w.u8(static_cast<std::uint8_t>(e.aggressor));
}

mc::Trade decode_trade(ByteReader& r) {
  mc::Trade e{};
  e.trade_id = mc::TradeId{r.u64()};
  e.price = get_price(r);
  e.qty = get_qty(r);
  e.maker_order_id = mc::OrderId{r.u64()};
  e.taker_order_id = mc::OrderId{r.u64()};
  e.maker_participant = mc::ParticipantId{r.u32()};
  e.taker_participant = mc::ParticipantId{r.u32()};
  e.aggressor = static_cast<mc::Side>(r.u8());
  return e;
}

}  // namespace

// ----- inbound record ---------------------------------------------------------

std::string encode_input(mc::SimTime ts, const mc::Inbound& msg) {
  ByteWriter w;
  w.i64(ts.ns());
  w.u8(static_cast<std::uint8_t>(msg.index()));
  std::visit(
      [&w](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, mc::NewOrder>) {
          encode_new(w, m);
        } else if constexpr (std::is_same_v<T, mc::CancelOrder>) {
          encode_cancel(w, m);
        } else if constexpr (std::is_same_v<T, mc::ModifyOrder>) {
          encode_modify(w, m);
        } else {
          static_assert(std::is_same_v<T, mc::SessionEnd>, "unhandled inbound alternative");
          // SessionEnd carries no fields.
        }
      },
      msg);
  return w.bytes();
}

bool decode_input(std::string_view bytes, mc::SimTime& ts, mc::Inbound& out) {
  ByteReader r(bytes);
  ts = mc::SimTime{r.i64()};
  switch (r.u8()) {
    case 0:
      out = decode_new(r);
      break;
    case 1:
      out = decode_cancel(r);
      break;
    case 2:
      out = decode_modify(r);
      break;
    case 3:
      out = mc::SessionEnd{};
      break;
    default:
      return false;  // unknown message tag
  }
  return r.at_end();
}

// ----- event record -----------------------------------------------------------

std::string encode_event(const mc::SequencedEvent& ev) {
  ByteWriter w;
  w.u64(ev.header.seq_out.value());
  w.u64(ev.header.seq_in.value());
  w.i64(ev.header.ts_event.ns());
  w.u8(static_cast<std::uint8_t>(ev.event.index()));
  std::visit(
      [&w](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, mc::OrderAccepted>) {
          encode_accepted(w, e);
        } else if constexpr (std::is_same_v<T, mc::OrderRejected>) {
          encode_rejected(w, e);
        } else if constexpr (std::is_same_v<T, mc::OrderCanceled>) {
          encode_canceled(w, e);
        } else if constexpr (std::is_same_v<T, mc::OrderModified>) {
          encode_modified(w, e);
        } else if constexpr (std::is_same_v<T, mc::Fill>) {
          encode_fill(w, e);
        } else {
          static_assert(std::is_same_v<T, mc::Trade>, "unhandled outbound alternative");
          encode_trade(w, e);
        }
      },
      ev.event);
  return w.bytes();
}

bool decode_event(std::string_view bytes, mc::SequencedEvent& out) {
  ByteReader r(bytes);
  out.header.seq_out = mc::Seq{r.u64()};
  out.header.seq_in = mc::Seq{r.u64()};
  out.header.ts_event = mc::SimTime{r.i64()};
  switch (r.u8()) {
    case 0:
      out.event = decode_accepted(r);
      break;
    case 1:
      out.event = decode_rejected(r);
      break;
    case 2:
      out.event = decode_canceled(r);
      break;
    case 3:
      out.event = decode_modified(r);
      break;
    case 4:
      out.event = decode_fill(r);
      break;
    case 5:
      out.event = decode_trade(r);
      break;
    default:
      return false;  // unknown event tag
  }
  return r.at_end();
}

}  // namespace microsim::persist
