#pragma once

/// \file
/// Inbound messages: everything a participant (or the simulation) can send to
/// the exchange (task R1-04). These are the raw business payloads defined by
/// EXCHANGE_RULES.md §3, §6, §7 and the Input-events list in
/// MATCHING_ENGINE_SPEC.md. The authoritative ordering fields (seq, ts_event)
/// are assigned later by the sequencer (R1-11) and live in EventHeader
/// (events.hpp) — they are not part of the payload, which keeps every message
/// trivially copyable and within the 64-byte budget (MEMORY_MODEL.md).

#include <cstdint>
#include <ostream>
#include <variant>

#include "microsim/core/types.hpp"

namespace microsim::core {

/// The two supported order types (R-3.1). No others exist in the MVP.
enum class OrderType : std::uint8_t { Limit = 0, Market = 1 };

[[nodiscard]] const char* to_cstr(OrderType t) noexcept;

/// New order (R-3.2). For a MARKET order `price` must be the default Price{}
/// (zero) — the validation chain (R-3.3 item 4) rejects a priced market order.
struct NewOrder {
  ParticipantId participant;
  ClientOrderId client_order_id;
  InstrumentId instrument;
  Side side;
  OrderType type;
  Qty qty;
  Price price;  ///< meaningful for LIMIT; Price{} for MARKET

  friend bool operator==(const NewOrder&, const NewOrder&) noexcept = default;
};

/// Cancel a resting order (R-6.1). `order_id` is the sole key; a participant may
/// cancel only its own orders (enforced by the gateway, R-6.1).
struct CancelOrder {
  ParticipantId participant;
  OrderId order_id;

  friend bool operator==(const CancelOrder&, const CancelOrder&) noexcept = default;
};

/// Cancel/replace (R-7.1). Both fields are the new *total* values; send the
/// current value to leave one unchanged. Priority effects are the engine's job
/// (R-7.2), not the message's.
struct ModifyOrder {
  ParticipantId participant;
  OrderId order_id;
  Qty new_qty;
  Price new_price;

  friend bool operator==(const ModifyOrder&, const ModifyOrder&) noexcept = default;
};

/// Internal control event closing the trading session (R-12). Carries no
/// fields; the engine cancels all resting orders on receipt (R-12.3).
struct SessionEnd {
  friend bool operator==(const SessionEnd&, const SessionEnd&) noexcept = default;
};

/// Any inbound message, as delivered to the exchange. The engine dispatches on
/// the active alternative (MATCHING_ENGINE_SPEC.md top-level dispatch).
using Inbound = std::variant<NewOrder, CancelOrder, ModifyOrder, SessionEnd>;

// Each message is a trivially copyable value within the event size budget.
static_assert(std::is_trivially_copyable_v<NewOrder>);
static_assert(std::is_trivially_copyable_v<CancelOrder>);
static_assert(std::is_trivially_copyable_v<ModifyOrder>);
static_assert(std::is_trivially_copyable_v<SessionEnd>);
static_assert(sizeof(NewOrder) <= 64);
static_assert(sizeof(CancelOrder) <= 64);
static_assert(sizeof(ModifyOrder) <= 64);
static_assert(std::is_trivially_copyable_v<Inbound>);

std::ostream& operator<<(std::ostream& os, OrderType t);

}  // namespace microsim::core
