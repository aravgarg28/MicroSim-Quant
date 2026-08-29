#include "microsim/engine/order_registry.hpp"

#include <cassert>

namespace microsim::engine {

namespace {

/// R-3.3 item 3: the enum value is one the engine defines. Messages arrive with
/// typed enums, but a fuzzer (or a corrupt log) can forge an out-of-range byte;
/// this is the guard the reason code MALFORMED exists for.
[[nodiscard]] bool valid_side(Side s) noexcept {
  return static_cast<std::uint8_t>(s) <= static_cast<std::uint8_t>(Side::Sell);
}

[[nodiscard]] bool valid_type(OrderType t) noexcept {
  return static_cast<std::uint8_t>(t) <= static_cast<std::uint8_t>(OrderType::Market);
}

}  // namespace

std::optional<RejectReason> OrderRegistry::validate_new(const core::NewOrder& m,
                                                        const Venue& venue) const {
  // R-3.3, applied in this exact order — first failure wins (one reject per
  // message, deterministic reason).
  const core::InstrumentConfig* instr = venue.find_instrument(m.instrument);
  if (instr == nullptr) {
    return RejectReason::UnknownInstrument;  // item 1
  }
  if (venue.find_participant(m.participant) == nullptr) {
    return RejectReason::UnknownParticipant;  // item 2
  }
  if (!valid_side(m.side) || !valid_type(m.type)) {
    return RejectReason::Malformed;  // item 3
  }
  if (m.type == OrderType::Market && m.price != Price{}) {
    return RejectReason::PriceOnMarketOrder;  // item 4
  }
  if (m.qty < Qty{1}) {
    return RejectReason::InvalidQty;  // item 5
  }
  if (m.qty > instr->max_order_qty) {
    return RejectReason::OrderTooLarge;  // item 6
  }
  if (m.type == OrderType::Limit && (m.price < instr->min_price || m.price > instr->max_price)) {
    return RejectReason::PriceOutOfBands;  // item 7
  }
  if (used_client_ids_.contains({m.participant.value(), m.client_order_id.value()})) {
    return RejectReason::DuplicateClientOrderId;  // item 8
  }
  return std::nullopt;
}

std::optional<RejectReason> OrderRegistry::validate_modify(
    const core::ModifyOrder& m, const core::InstrumentConfig& instr) const {
  // R-7.1: the new total quantity and price must pass the same bands a new order
  // does (R-3.3 items 5–7), first failure wins.
  if (m.new_qty < Qty{1}) {
    return RejectReason::InvalidQty;  // item 5
  }
  if (m.new_qty > instr.max_order_qty) {
    return RejectReason::OrderTooLarge;  // item 6
  }
  if (m.new_price < instr.min_price || m.new_price > instr.max_price) {
    return RejectReason::PriceOutOfBands;  // item 7
  }
  return std::nullopt;
}

OrderId OrderRegistry::create(const core::NewOrder& m) {
  const OrderId id = next_id_;
  next_id_ = next_id_.next();  // R-4.1: strictly increasing in arrival order

  orders_.emplace(id, OrderRecord{.id = id,
                                  .participant = m.participant,
                                  .client_order_id = m.client_order_id,
                                  .instrument = m.instrument,
                                  .side = m.side,
                                  .type = m.type,
                                  .price = m.price,
                                  .total_qty = m.qty,
                                  .filled_qty = Qty{0},
                                  .state = OrderState::Live});
  used_client_ids_.insert({m.participant.value(), m.client_order_id.value()});
  return id;
}

std::int64_t OrderRegistry::open_order_count(ParticipantId participant) const {
  std::int64_t n = 0;
  for (const auto& [id, rec] : orders_) {
    if (rec.participant == participant && rec.state == OrderState::Live) {
      ++n;
    }
  }
  return n;
}

std::int64_t OrderRegistry::same_side_open_qty(ParticipantId participant, Side side) const {
  std::int64_t sum = 0;
  for (const auto& [id, rec] : orders_) {
    if (rec.participant == participant && rec.state == OrderState::Live && rec.side == side) {
      sum += rec.remaining().lots();
    }
  }
  return sum;
}

OrderRecord* OrderRegistry::lookup(OrderId id) {
  auto it = orders_.find(id);
  return it == orders_.end() ? nullptr : &it->second;
}

const OrderRecord* OrderRegistry::lookup(OrderId id) const {
  auto it = orders_.find(id);
  return it == orders_.end() ? nullptr : &it->second;
}

void OrderRegistry::apply_fill(OrderId id, Qty q) {
  OrderRecord* rec = lookup(id);
  assert(rec != nullptr && "fill on an unknown order");
  assert(rec->state == OrderState::Live && "fill on a terminal order (INV-7)");
  assert(q > Qty{0} && q <= rec->remaining() && "fill exceeds remaining");
  rec->filled_qty += q;
  if (rec->remaining() == Qty{0}) {
    rec->state = OrderState::Filled;  // R-4.3 implicit FILLED terminal
  }
}

void OrderRegistry::modify(OrderId id, Qty new_total_qty, Price new_price) {
  OrderRecord* rec = lookup(id);
  assert(rec != nullptr && "modify of an unknown order");
  assert(rec->state == OrderState::Live && "modify on a terminal order (INV-7)");
  assert(new_total_qty > rec->filled_qty && "modify-to-done must be handled by the caller (R-7.3)");
  rec->total_qty = new_total_qty;
  rec->price = new_price;
}

void OrderRegistry::finalize(OrderId id, OrderState terminal) {
  OrderRecord* rec = lookup(id);
  assert(rec != nullptr && "finalize of an unknown order");
  assert(rec->state == OrderState::Live && "order already terminal (INV-7 absorbing)");
  assert(terminal != OrderState::Live && "finalize must move to a terminal state");
  rec->state = terminal;
}

}  // namespace microsim::engine
