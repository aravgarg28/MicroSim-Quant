#pragma once

/// \file
/// Order identity, lifecycle, and gateway validation (task R1-10). This is the
/// authoritative record of every order the exchange has seen: it assigns the
/// strictly-increasing `order_id` (R-4.1), tracks the R-4.2 state machine with
/// absorbing terminal states (INV-7), enforces per-participant client-order-id
/// uniqueness (R-3.3 item 8), and runs the deterministic validation chain
/// (R-3.3 items 1–8, first failure wins). Risk checks (item 9) are R1-15 and
/// live in a separate stage; matching is R1-12.

#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/venue.hpp"

namespace microsim::engine {

using core::ClientOrderId;
using core::InstrumentId;
using core::OrderId;
using core::OrderType;
using core::ParticipantId;
using core::Price;
using core::Qty;
using core::RejectReason;
using core::Side;

/// R-4.2 states. `Live` covers both in-flight (matching) and resting orders; the
/// terminal states are absorbing — no event may act on a terminal order (INV-7).
enum class OrderState : std::uint8_t { Live = 0, Filled, Canceled };

/// The registry's record of one order. `filled_qty` accumulates across trades;
/// `remaining()` is what is still workable. `price` is the limit price (Price{}
/// for a market order, which never rests).
struct OrderRecord {
  OrderId id{};
  ParticipantId participant{};
  ClientOrderId client_order_id{};
  InstrumentId instrument{};
  Side side{};
  OrderType type{};
  Price price{};
  Qty total_qty{};
  Qty filled_qty{};
  OrderState state{OrderState::Live};

  [[nodiscard]] Qty remaining() const noexcept { return total_qty - filled_qty; }

  [[nodiscard]] bool terminal() const noexcept { return state != OrderState::Live; }
};

class OrderRegistry {
 public:
  /// Validate a NewOrder per R-3.3 items 1–8, in that exact order, first failure
  /// wins. Returns the reject reason or nullopt if it passes items 1–8. Pure /
  /// const: it records nothing (dedup is recorded only on create()). Risk checks
  /// (R-3.3 item 9) run after this, in R1-15.
  [[nodiscard]] std::optional<RejectReason> validate_new(const core::NewOrder& m,
                                                         const Venue& venue) const;

  /// Validate a ModifyOrder's new quantity and price per R-7.1 (R-3.3 items 5–7
  /// semantics), first failure wins. Ownership (R-6.1) and terminal-state
  /// (R-6.3) checks run in the engine, against the order record, before this.
  /// Every modifiable order is a resting LIMIT order, so the price band always
  /// applies. Risk (R-9.5) is a later stage (R1-15).
  [[nodiscard]] std::optional<RejectReason> validate_modify(
      const core::ModifyOrder& m, const core::InstrumentConfig& instr) const;

  /// Assign the next order_id (R-4.1), record the client-order-id as used, and
  /// create a Live record. Precondition: validation (and, later, risk) passed.
  OrderId create(const core::NewOrder& m);

  [[nodiscard]] OrderRecord* lookup(OrderId id);
  [[nodiscard]] const OrderRecord* lookup(OrderId id) const;

  /// Apply `q` lots of fill to an order (R-4.3): accumulates filled quantity and
  /// flips the order to the absorbing FILLED state when nothing remains.
  void apply_fill(OrderId id, Qty q);

  /// Apply an accepted modify (R-7) to a Live order: set its new total quantity
  /// and limit price (remaining is derived, total − filled). Preconditions: the
  /// order is Live and `new_total_qty > filled_qty` — modify-to-done (R-7.3) is
  /// resolved by the caller before this.
  void modify(OrderId id, Qty new_total_qty, Price new_price);

  /// Move a Live order to an absorbing terminal state (CANCELED by request /
  /// no-liquidity / session-end / modify-to-done; FILLED is normally reached via
  /// apply_fill). No-op-safe only on Live orders — asserts otherwise (INV-7).
  void finalize(OrderId id, OrderState terminal);

  /// Count of orders ever created (order_ids are dense from 1).
  [[nodiscard]] std::size_t size() const noexcept { return orders_.size(); }

  /// Number of a participant's orders currently Live (resting or in-flight),
  /// for the open-order risk limit (R-9.1). Iterates the order map, but the
  /// result is a count and so is independent of iteration order (R-10.3).
  [[nodiscard]] std::int64_t open_order_count(ParticipantId participant) const;

  /// Sum of the remaining lots of a participant's Live orders on one side, for
  /// the worst-case position risk limit (R-9.3). Order-independent sum (R-10.3).
  [[nodiscard]] std::int64_t same_side_open_qty(ParticipantId participant, Side side) const;

 private:
  OrderId next_id_{OrderId::first()};
  std::unordered_map<OrderId, OrderRecord> orders_;
  // (participant, client_order_id) pairs seen this session — R-3.3 item 8.
  std::set<std::pair<std::uint32_t, std::uint64_t>> used_client_ids_;
};

}  // namespace microsim::engine
