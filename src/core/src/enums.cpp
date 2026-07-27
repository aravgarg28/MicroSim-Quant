#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"

// String tables for the message/event enums (R1-04). Each to_cstr is a switch
// with no default, so -Wswitch makes adding an enumerator without a name a
// compile error — the exhaustiveness check the task asks for.

namespace microsim::core {

const char* to_cstr(OrderType t) noexcept {
  switch (t) {
    case OrderType::Limit:
      return "LIMIT";
    case OrderType::Market:
      return "MARKET";
  }
  return "?";
}

const char* to_cstr(RejectReason r) noexcept {
  switch (r) {
    case RejectReason::UnknownInstrument:
      return "UNKNOWN_INSTRUMENT";
    case RejectReason::UnknownParticipant:
      return "UNKNOWN_PARTICIPANT";
    case RejectReason::Malformed:
      return "MALFORMED";
    case RejectReason::PriceOnMarketOrder:
      return "PRICE_ON_MARKET_ORDER";
    case RejectReason::InvalidQty:
      return "INVALID_QTY";
    case RejectReason::OrderTooLarge:
      return "ORDER_TOO_LARGE";
    case RejectReason::PriceOutOfBands:
      return "PRICE_OUT_OF_BANDS";
    case RejectReason::InvalidTick:
      return "INVALID_TICK";
    case RejectReason::DuplicateClientOrderId:
      return "DUPLICATE_CLIENT_ORDER_ID";
    case RejectReason::MaxOpenOrders:
      return "MAX_OPEN_ORDERS";
    case RejectReason::RiskOrderTooLarge:
      return "RISK_ORDER_TOO_LARGE";
    case RejectReason::MaxPosition:
      return "MAX_POSITION";
    case RejectReason::UnknownOrder:
      return "UNKNOWN_ORDER";
    case RejectReason::NotOrderOwner:
      return "NOT_ORDER_OWNER";
    case RejectReason::TooLateToCancel:
      return "TOO_LATE_TO_CANCEL";
    case RejectReason::TooLateToModify:
      return "TOO_LATE_TO_MODIFY";
    case RejectReason::MarketClosed:
      return "MARKET_CLOSED";
  }
  return "?";
}

const char* to_cstr(CancelReason r) noexcept {
  switch (r) {
    case CancelReason::ByRequest:
      return "BY_REQUEST";
    case CancelReason::NoLiquidity:
      return "NO_LIQUIDITY";
    case CancelReason::SelfTradePrevented:
      return "SELF_TRADE_PREVENTED";
    case CancelReason::ModifyToDone:
      return "MODIFY_TO_DONE";
    case CancelReason::SessionEnd:
      return "SESSION_END";
  }
  return "?";
}

const char* to_cstr(LiquidityFlag f) noexcept {
  switch (f) {
    case LiquidityFlag::Maker:
      return "MAKER";
    case LiquidityFlag::Taker:
      return "TAKER";
  }
  return "?";
}

bool from_cstr(std::string_view name, RejectReason& out) noexcept {
  for (RejectReason r : kAllRejectReasons) {
    if (name == to_cstr(r)) {
      out = r;
      return true;
    }
  }
  return false;
}

bool from_cstr(std::string_view name, CancelReason& out) noexcept {
  for (CancelReason r : kAllCancelReasons) {
    if (name == to_cstr(r)) {
      out = r;
      return true;
    }
  }
  return false;
}

std::ostream& operator<<(std::ostream& os, OrderType t) {
  return os << to_cstr(t);
}

std::ostream& operator<<(std::ostream& os, RejectReason r) {
  return os << to_cstr(r);
}

std::ostream& operator<<(std::ostream& os, CancelReason r) {
  return os << to_cstr(r);
}

std::ostream& operator<<(std::ostream& os, LiquidityFlag f) {
  return os << to_cstr(f);
}

}  // namespace microsim::core
