#pragma once

/// \file
/// Pre-trade risk checks and the signed position tally (task R1-15), implementing
/// EXCHANGE_RULES.md §9. Risk is the ninth gateway stage, run after R-3.3 items
/// 1–8 pass and before an order is accepted; the same delta checks re-run on a
/// modify (R-9.5).
///
/// The position tally is the minimal signed-fill accumulator that seeds full
/// accounting (E11): a buy fill moves a participant long (+lots), a sell fill
/// short (−lots). Open-order counts and same-side open quantity are recomputed
/// from the registry on demand (R-9.1/9.3) — order-independent aggregates, so
/// determinism (R-10.3) holds despite the registry's unordered storage.

#include <cstdint>
#include <map>
#include <optional>

#include "microsim/core/config.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/order_registry.hpp"

namespace microsim::engine {

/// Owns each participant's net position (signed lots) and runs the §9 checks
/// against it plus the live-order state in the registry.
class RiskEngine {
 public:
  /// R-9.1/9.2/9.3 for a new order, in that order, first failure wins. The order
  /// is not yet in the registry, so its quantity is added on top of the
  /// participant's existing exposure. Returns the reject reason or nullopt.
  [[nodiscard]] std::optional<core::RejectReason> check_new(const core::NewOrder& m,
                                                            const core::ParticipantRisk& limits,
                                                            const OrderRegistry& registry) const {
    // R-9.1: open-order slots. (The order about to be created is not yet counted.)
    if (registry.open_order_count(m.participant) >= limits.max_open_orders) {
      return core::RejectReason::MaxOpenOrders;
    }
    // R-9.2: participant's own (possibly stricter) per-order size cap; the
    // instrument cap is already enforced by R-3.3 item 6.
    if (m.qty > limits.max_order_qty_lots) {
      return core::RejectReason::RiskOrderTooLarge;
    }
    // R-9.3: worst case is this order and every same-side open order filling.
    const std::int64_t same_side = registry.same_side_open_qty(m.participant, m.side);
    const std::int64_t worst =
        position(m.participant) + core::sign_of(m.side) * (same_side + m.qty.lots());
    if (breaches(worst, limits.max_position_lots)) {
      return core::RejectReason::MaxPosition;
    }
    return std::nullopt;
  }

  /// R-9.5 for a modify: the same size and worst-case position checks, but on the
  /// delta the modify introduces (the order already contributes its old remaining
  /// to the registry aggregates, so swap that for the new remaining). Open-order
  /// count is unchanged by a modify, so R-9.1 does not re-run. A failure leaves
  /// the original order untouched (the caller checks before mutating).
  [[nodiscard]] std::optional<core::RejectReason> check_modify(
      const OrderRecord& order, const core::ModifyOrder& m, const core::ParticipantRisk& limits,
      const OrderRegistry& registry) const {
    if (m.new_qty > limits.max_order_qty_lots) {  // R-9.2 on the new total
      return core::RejectReason::RiskOrderTooLarge;
    }
    const std::int64_t old_remaining = order.remaining().lots();
    const std::int64_t new_remaining = m.new_qty.lots() - order.filled_qty.lots();
    const std::int64_t same_side = registry.same_side_open_qty(order.participant, order.side);
    const std::int64_t worst =
        position(order.participant) +
        core::sign_of(order.side) * (same_side - old_remaining + new_remaining);
    if (breaches(worst, limits.max_position_lots)) {  // R-9.3 with the new size
      return core::RejectReason::MaxPosition;
    }
    return std::nullopt;
  }

  /// Apply a fill to a participant's position (R-4.3): +q lots on a buy, −q on a
  /// sell. Called for both sides of every trade.
  void on_fill(core::ParticipantId participant, core::Side side, core::Qty q) {
    position_[participant] += core::sign_of(side) * q.lots();
  }

  /// A participant's net position in lots (0 if it has never traded).
  [[nodiscard]] std::int64_t position(core::ParticipantId participant) const {
    const auto it = position_.find(participant);
    return it == position_.end() ? 0 : it->second;
  }

 private:
  /// |worst| > limit, guarding against the case where the limit is not set.
  [[nodiscard]] static bool breaches(std::int64_t worst, core::Qty max_position_lots) noexcept {
    const std::int64_t limit = max_position_lots.lots();
    return worst > limit || worst < -limit;
  }

  std::map<core::ParticipantId, std::int64_t> position_;  ///< signed lots per participant
};

}  // namespace microsim::engine
