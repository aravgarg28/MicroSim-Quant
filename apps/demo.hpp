#pragma once

/// \file
/// Shared front-end pieces for the demo CLIs (task R1-21): the fixed instrument
/// and participant set, the scripted order flow, and the human-readable
/// transcript/book printers. `microsim_run` drives this scenario and tees an
/// input log + event log; `microsim_replay` reads that input log back and must
/// reproduce the event log byte for byte. Both build the *same* venue and
/// instrument here, because the R1 log carries only messages and timestamps —
/// the run's config comes from a shared builder (a config file / manifest is
/// R1-22 / R1-06 pt2).

#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "microsim/book/fast_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/convert.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/venue.hpp"

namespace microsim::demo {

namespace mc = microsim::core;
namespace me = microsim::engine;
namespace mb = microsim::book;

/// The demo book type: the production FastBook, built from the instrument band.
using Engine = me::MatchingEngine<mb::FastBook>;

constexpr int kDp = 2;  // the instrument quotes in cents (2 dp)
constexpr mc::InstrumentId kInstr{1};
constexpr mc::ParticipantId kMM1{1};
constexpr mc::ParticipantId kMM2{2};
constexpr mc::ParticipantId kTaker{9};

inline mc::InstrumentConfig demo_instrument() {
  return mc::InstrumentConfig{
      .id = kInstr,
      .symbol = "SIM",
      .tick_size = 1,  // 1 minor unit (cent) per tick
      .lot_size = 1,
      .min_price = mc::Price{500},   // $5.00
      .max_price = mc::Price{1500},  // $15.00
      .max_order_qty = mc::Qty{10000},
      .fees = {.taker_fee_per_lot = mc::Cash{2}, .maker_rebate_per_lot = mc::Cash{1}}};
}

inline me::Venue demo_venue() {
  me::Venue venue;
  venue.add_instrument(demo_instrument());
  venue.add_participant(mc::ParticipantConfig{.id = kMM1});
  venue.add_participant(mc::ParticipantConfig{.id = kMM2});
  venue.add_participant(mc::ParticipantConfig{.id = kTaker});
  return venue;
}

inline mc::NewOrder lim(mc::ParticipantId p, std::uint64_t clord, mc::Side side, std::int64_t px,
                        std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = mc::OrderType::Limit,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{px}};
}

inline mc::NewOrder mkt(mc::ParticipantId p, std::uint64_t clord, mc::Side side, std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = mc::OrderType::Market,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{}};
}

/// The scripted flow (mirrors the EXCHANGE_RULES §15 example): two makers post
/// depth, a MARKET BUY sweeps the offers, then a SELL limit crosses the bid.
/// Produces 4 trades / 80 lots — the smoke test's golden.
inline std::vector<mc::Inbound> demo_script() {
  return {
      lim(kMM1, 1, mc::Side::Sell, 1003, 10),    // ask $10.03 x10
      lim(kMM2, 1, mc::Side::Sell, 1003, 15),    // ask $10.03 x15 (behind)
      lim(kMM1, 2, mc::Side::Sell, 1005, 40),    // ask $10.05 x40
      lim(kMM1, 3, mc::Side::Buy, 1001, 20),     // bid $10.01 x20
      lim(kMM2, 2, mc::Side::Buy, 1000, 30),     // bid $10.00 x30
      mkt(kTaker, 1, mc::Side::Buy, 60),         // MARKET BUY 60 -> sweeps the asks
      lim(kTaker, 2, mc::Side::Sell, 1001, 25),  // SELL crosses the bid
  };
}

// ----- printing ---------------------------------------------------------------

inline std::string usd(mc::Price p, const mc::InstrumentConfig& instr) {
  return "$" + mc::price_to_decimal(p, instr, kDp);
}

inline std::string usd(mc::Cash c) {
  return "$" + mc::cash_to_decimal(c, kDp);
}

inline const char* side_str(mc::Side s) {
  return s == mc::Side::Buy ? "BUY " : "SELL";
}

inline std::string who(mc::ParticipantId p) {
  if (p == kMM1) {
    return "MM1";
  }
  if (p == kMM2) {
    return "MM2";
  }
  if (p == kTaker) {
    return "TKR";
  }
  return "P" + std::to_string(p.value());
}

/// Print the ">> ..." line describing an inbound message.
inline void print_inbound(const mc::Inbound& msg, const mc::InstrumentConfig& instr) {
  std::visit(
      [&instr](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, mc::NewOrder>) {
          const bool market = m.type == mc::OrderType::Market;
          std::cout << ">> " << who(m.participant) << "  " << (market ? "MARKET " : "LIMIT ")
                    << side_str(m.side) << " " << m.qty.lots();
          if (!market) {
            std::cout << " @ " << usd(m.price, instr);
          }
          std::cout << "\n";
        } else if constexpr (std::is_same_v<T, mc::CancelOrder>) {
          std::cout << ">> " << who(m.participant) << "  CANCEL order #" << m.order_id.value()
                    << "\n";
        } else if constexpr (std::is_same_v<T, mc::ModifyOrder>) {
          std::cout << ">> " << who(m.participant) << "  MODIFY order #" << m.order_id.value()
                    << " -> " << m.new_qty.lots() << " @ " << usd(m.new_price, instr) << "\n";
        } else {
          static_assert(std::is_same_v<T, mc::SessionEnd>, "unhandled inbound alternative");
          std::cout << ">> SESSION END\n";
        }
      },
      msg);
}

/// Print the interesting outbound events for a message (fills are omitted from
/// the summary; they are the private legs of each Trade).
inline void print_events(const std::vector<mc::Outbound>& evs, const mc::InstrumentConfig& instr) {
  for (const mc::Outbound& e : evs) {
    if (const auto* a = std::get_if<mc::OrderAccepted>(&e)) {
      std::cout << "      accepted  order #" << a->order_id.value() << "\n";
    } else if (const auto* rj = std::get_if<mc::OrderRejected>(&e)) {
      std::cout << "      REJECTED  (" << mc::to_cstr(rj->reason) << ")\n";
    } else if (const auto* t = std::get_if<mc::Trade>(&e)) {
      std::cout << "      • trade T" << t->trade_id.value() << "  " << t->qty.lots() << " @ "
                << usd(t->price, instr) << "   maker #" << t->maker_order_id.value() << "  taker #"
                << t->taker_order_id.value() << "\n";
    } else if (const auto* c = std::get_if<mc::OrderCanceled>(&e)) {
      std::cout << "      canceled  " << c->remaining_qty.lots() << " lots ("
                << mc::to_cstr(c->reason) << ")\n";
    } else if (const auto* mod = std::get_if<mc::OrderModified>(&e)) {
      std::cout << "      modified  order #" << mod->order_id.value() << "\n";
    }
  }
}

inline void print_book(const Engine& eng) {
  const mb::BookState s = eng.book().dump_state();
  const mc::InstrumentConfig& instr = eng.instrument();
  std::cout << "\n  Final book (price / resting qty):\n";
  std::cout << "        ASKS\n";
  for (auto it = s.asks.rbegin(); it != s.asks.rend();
       ++it) {  // worst-to-best, spread in the middle
    mc::Qty q{0};
    for (const auto& o : it->orders) {
      q += o.remaining;
    }
    std::cout << "     " << usd(it->price, instr) << "   x " << q.lots() << "\n";
  }
  std::cout << "        ----------------  spread\n";
  for (const auto& level : s.bids) {  // bids best-first (highest first)
    mc::Qty q{0};
    for (const auto& o : level.orders) {
      q += o.remaining;
    }
    std::cout << "     " << usd(level.price, instr) << "   x " << q.lots() << "\n";
  }
  std::cout << "        BIDS\n";
}

/// Tally trades/lots from an event stream, for the run summary line.
struct Tally {
  int trades = 0;
  long long lots = 0;
};

inline void tally(const std::vector<mc::Outbound>& evs, Tally& acc) {
  for (const mc::Outbound& e : evs) {
    if (const auto* t = std::get_if<mc::Trade>(&e)) {
      ++acc.trades;
      acc.lots += t->qty.lots();
    }
  }
}

}  // namespace microsim::demo
