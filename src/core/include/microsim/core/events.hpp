#pragma once

/// \file
/// Outbound events: everything the exchange emits (task R1-04). Private events
/// go to the owning participant; the audit `Trade` records a match. Definitions
/// follow the Output-events list in MATCHING_ENGINE_SPEC.md and EXCHANGE_RULES.md
/// §4/§5/§11/§14.
///
/// Public market-data messages (TradeMD, book deltas) are deliberately NOT here
/// — they belong to the `md` module (R2). Serialization of these events is the
/// log format's job (R1-11). Payloads carry no sequencing header; the sequencer
/// pairs each with an EventHeader, keeping payloads trivially copyable and small.

#include <array>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <variant>

#include "microsim/core/types.hpp"

namespace microsim::core {

// =============================================================================
// Reason codes (R-14, complete enumeration)
// =============================================================================

/// Why a message was rejected (R-14 rejects). `InvalidTick` originates only at
/// the config/Python boundary (R-3.4), never inside the engine, but is part of
/// the complete enumeration.
enum class RejectReason : std::uint8_t {
  UnknownInstrument = 0,
  UnknownParticipant,
  Malformed,
  PriceOnMarketOrder,
  InvalidQty,
  OrderTooLarge,
  PriceOutOfBands,
  InvalidTick,
  DuplicateClientOrderId,
  MaxOpenOrders,
  RiskOrderTooLarge,
  MaxPosition,
  UnknownOrder,
  NotOrderOwner,
  TooLateToCancel,
  TooLateToModify,
  MarketClosed,
};

/// Why a resting order was canceled (R-14 cancel reasons). `SelfTradePrevented`
/// is Release 2 (R-8.2) but is part of the complete enumeration.
enum class CancelReason : std::uint8_t {
  ByRequest = 0,
  NoLiquidity,
  SelfTradePrevented,
  ModifyToDone,
  SessionEnd,
};

/// Which side of a trade a fill was on (R-5.7 / R-11.2): the maker rests and is
/// paid a rebate; the taker aggresses and pays a fee.
enum class LiquidityFlag : std::uint8_t { Maker = 0, Taker = 1 };

[[nodiscard]] const char* to_cstr(RejectReason r) noexcept;
[[nodiscard]] const char* to_cstr(CancelReason r) noexcept;
[[nodiscard]] const char* to_cstr(LiquidityFlag f) noexcept;

/// Parse back from the canonical name (round-trips with to_cstr). Returns false
/// on an unknown name; used by log readers and tests.
[[nodiscard]] bool from_cstr(std::string_view name, RejectReason& out) noexcept;
[[nodiscard]] bool from_cstr(std::string_view name, CancelReason& out) noexcept;

/// All enumerators, for exhaustive iteration in tests (kept in sync with the
/// enums by a static_assert on the count).
inline constexpr std::array<RejectReason, 17> kAllRejectReasons = {
    RejectReason::UnknownInstrument,
    RejectReason::UnknownParticipant,
    RejectReason::Malformed,
    RejectReason::PriceOnMarketOrder,
    RejectReason::InvalidQty,
    RejectReason::OrderTooLarge,
    RejectReason::PriceOutOfBands,
    RejectReason::InvalidTick,
    RejectReason::DuplicateClientOrderId,
    RejectReason::MaxOpenOrders,
    RejectReason::RiskOrderTooLarge,
    RejectReason::MaxPosition,
    RejectReason::UnknownOrder,
    RejectReason::NotOrderOwner,
    RejectReason::TooLateToCancel,
    RejectReason::TooLateToModify,
    RejectReason::MarketClosed,
};

inline constexpr std::array<CancelReason, 5> kAllCancelReasons = {
    CancelReason::ByRequest,    CancelReason::NoLiquidity, CancelReason::SelfTradePrevented,
    CancelReason::ModifyToDone, CancelReason::SessionEnd,
};

// =============================================================================
// Sequencing header (R-10.2)
// =============================================================================

/// Assigned to every outbound event by the sequencer (R1-11). Kept separate from
/// the payloads so the payloads stay small and header-agnostic.
struct EventHeader {
  Seq seq_out;       ///< strictly increasing, gap-free per simulation
  Seq seq_in;        ///< the triggering inbound message's seq
  SimTime ts_event;  ///< logical time at processing

  friend bool operator==(const EventHeader&, const EventHeader&) noexcept = default;
};

// =============================================================================
// Event payloads
// =============================================================================

/// The order was accepted; `order_id` is now assigned (R-4.1, R-4.3). Echoes the
/// participant and its client id for correlation.
struct OrderAccepted {
  OrderId order_id;
  ParticipantId participant;
  ClientOrderId client_order_id;

  friend bool operator==(const OrderAccepted&, const OrderAccepted&) noexcept = default;
};

/// A message was rejected with a single reason (R-3.3, R-4.3). For a rejected
/// NewOrder, `order_id` is unset (default) and `client_order_id` correlates; for
/// a rejected cancel/modify, `order_id` is the target and `client_order_id` is
/// unset.
struct OrderRejected {
  ParticipantId participant;
  ClientOrderId client_order_id;
  OrderId order_id;
  RejectReason reason;

  friend bool operator==(const OrderRejected&, const OrderRejected&) noexcept = default;
};

/// A resting order was removed with quantity remaining (R-6.2, R-5.5, R-8.2,
/// R-7.3, R-12.3), carrying the reason and the quantity that did not trade.
struct OrderCanceled {
  OrderId order_id;
  ParticipantId participant;
  Qty remaining_qty;
  CancelReason reason;

  friend bool operator==(const OrderCanceled&, const OrderCanceled&) noexcept = default;
};

/// A modify succeeded (R-7.5), emitted before any fills the modify triggers.
struct OrderModified {
  OrderId order_id;
  ParticipantId participant;
  Qty new_qty;
  Price new_price;

  friend bool operator==(const OrderModified&, const OrderModified&) noexcept = default;
};

/// One counterparty's private view of a trade (R-5.7, R-11.2): the fee charged
/// (taker, positive) or rebate paid (maker, encoded as the signed cash delta),
/// and which side of the book this order was.
struct Fill {
  OrderId order_id;
  ParticipantId participant;
  TradeId trade_id;
  Price price;
  Qty qty;
  Cash fee;  ///< signed: taker pays (>0 cost), maker receives (<0 cost)
  LiquidityFlag liquidity;

  friend bool operator==(const Fill&, const Fill&) noexcept = default;
};

/// The audit record of a match (R-5.7): full identities and the aggressor side.
/// The anonymized public version (TradeMD) is produced by the `md` module (R2).
struct Trade {
  TradeId trade_id;
  Price price;
  Qty qty;
  OrderId maker_order_id;
  OrderId taker_order_id;
  ParticipantId maker_participant;
  ParticipantId taker_participant;
  Side aggressor;

  friend bool operator==(const Trade&, const Trade&) noexcept = default;
};

/// Any outbound event. The FILLED-terminal state is implicit (the fill that
/// brings remaining to zero), so there is no separate OrderFilled event (R-4.3).
using Outbound =
    std::variant<OrderAccepted, OrderRejected, OrderCanceled, OrderModified, Fill, Trade>;

/// An outbound event paired with its sequencing header (R-10.2): the unit the
/// sequencer (R1-11) emits. Equality is field-wise on both the header and the
/// payload, which is what the invariant checks (R1-17) and the differential
/// harness (R1-20) compare — two runs of the same sequenced input must produce
/// equal streams (R-10.3).
struct SequencedEvent {
  EventHeader header;
  Outbound event;

  friend bool operator==(const SequencedEvent&, const SequencedEvent&) noexcept = default;
};

// ----- invariants on the enumerations and payload sizes -----------------------

static_assert(kAllRejectReasons.size() == static_cast<std::size_t>(RejectReason::MarketClosed) + 1,
              "kAllRejectReasons must list every RejectReason");
static_assert(kAllCancelReasons.size() == static_cast<std::size_t>(CancelReason::SessionEnd) + 1,
              "kAllCancelReasons must list every CancelReason");

static_assert(std::is_trivially_copyable_v<OrderAccepted>);
static_assert(std::is_trivially_copyable_v<OrderRejected>);
static_assert(std::is_trivially_copyable_v<OrderCanceled>);
static_assert(std::is_trivially_copyable_v<OrderModified>);
static_assert(std::is_trivially_copyable_v<Fill>);
static_assert(std::is_trivially_copyable_v<Trade>);
static_assert(std::is_trivially_copyable_v<EventHeader>);
static_assert(sizeof(OrderAccepted) <= 64);
static_assert(sizeof(OrderRejected) <= 64);
static_assert(sizeof(OrderCanceled) <= 64);
static_assert(sizeof(OrderModified) <= 64);
static_assert(sizeof(Fill) <= 64);
static_assert(sizeof(Trade) <= 64);
static_assert(std::is_trivially_copyable_v<Outbound>);

std::ostream& operator<<(std::ostream& os, RejectReason r);
std::ostream& operator<<(std::ostream& os, CancelReason r);
std::ostream& operator<<(std::ostream& os, LiquidityFlag f);

}  // namespace microsim::core
