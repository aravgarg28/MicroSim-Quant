/// \file
/// `microsim_run` — a minimal demonstration driver for the exchange (Tier-A
/// walking skeleton; the full scenario-file replay CLI is R1-21). It registers
/// one instrument and a few participants, feeds a scripted order flow through
/// the matching engine, and prints the resulting trades and the final book in
/// human-readable dollars (via the R1-06 boundary conversions). Determinism:
/// no clock reads, no randomness — the output is a pure function of the script.

#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/convert.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/venue.hpp"

namespace mc = microsim::core;
namespace me = microsim::engine;
namespace mb = microsim::book;

namespace {

constexpr int kDp = 2;  // the instrument quotes in cents (2 dp)
constexpr mc::InstrumentId kInstr{1};

mc::InstrumentConfig demo_instrument() {
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

std::string usd(mc::Price p, const mc::InstrumentConfig& instr) {
  return "$" + mc::price_to_decimal(p, instr, kDp);
}

std::string usd(mc::Cash c) {
  return "$" + mc::cash_to_decimal(c, kDp);
}

const char* side_str(mc::Side s) {
  return s == mc::Side::Buy ? "BUY " : "SELL";
}

// A scripted line: a participant name for display plus the message to send.
struct Scripted {
  std::string who;
  mc::NewOrder msg;
};

mc::NewOrder lim(mc::ParticipantId p, std::uint64_t clord, mc::Side side, std::int64_t px,
                 std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = mc::OrderType::Limit,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{px}};
}

mc::NewOrder mkt(mc::ParticipantId p, std::uint64_t clord, mc::Side side, std::int64_t qty) {
  return mc::NewOrder{.participant = p,
                      .client_order_id = mc::ClientOrderId{clord},
                      .instrument = kInstr,
                      .side = side,
                      .type = mc::OrderType::Market,
                      .qty = mc::Qty{qty},
                      .price = mc::Price{}};
}

void print_events(const std::vector<mc::Outbound>& evs, const mc::InstrumentConfig& instr) {
  for (const mc::Outbound& e : evs) {
    if (std::holds_alternative<mc::OrderAccepted>(e)) {
      std::cout << "      accepted  order #" << std::get<mc::OrderAccepted>(e).order_id.value()
                << "\n";
    } else if (std::holds_alternative<mc::OrderRejected>(e)) {
      std::cout << "      REJECTED  (" << mc::to_cstr(std::get<mc::OrderRejected>(e).reason)
                << ")\n";
    } else if (std::holds_alternative<mc::Trade>(e)) {
      const auto& t = std::get<mc::Trade>(e);
      std::cout << "      • trade T" << t.trade_id.value() << "  " << t.qty.lots() << " @ "
                << usd(t.price, instr) << "   maker #" << t.maker_order_id.value() << "  taker #"
                << t.taker_order_id.value() << "\n";
    } else if (std::holds_alternative<mc::OrderCanceled>(e)) {
      const auto& c = std::get<mc::OrderCanceled>(e);
      std::cout << "      canceled  " << c.remaining_qty.lots() << " lots ("
                << mc::to_cstr(c.reason) << ")\n";
    }
    // Fills are the private per-party legs of each Trade; omitted from this
    // summary view to keep the transcript readable.
  }
}

template <class Engine>
void print_book(const Engine& eng) {
  const mb::BookState s = eng.book().dump_state();
  const mc::InstrumentConfig& instr = eng.instrument();
  std::cout << "\n  Final book (price / resting qty):\n";
  std::cout << "        ASKS\n";
  // Asks best-first is lowest-first; print worst-to-best so the spread sits in
  // the middle, like a real depth ladder.
  for (auto it = s.asks.rbegin(); it != s.asks.rend(); ++it) {
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

}  // namespace

int main() {
  const mc::InstrumentConfig instr = demo_instrument();

  me::Venue venue;
  venue.add_instrument(instr);
  const mc::ParticipantId mm1{1};
  const mc::ParticipantId mm2{2};
  const mc::ParticipantId taker{9};
  venue.add_participant(mc::ParticipantConfig{.id = mm1});
  venue.add_participant(mc::ParticipantConfig{.id = mm2});
  venue.add_participant(mc::ParticipantConfig{.id = taker});

  me::MatchingEngine<mb::ReferenceBook> eng{venue, instr};

  // A scripted flow: two makers post depth, then a taker sweeps the offers and
  // a marketable sell hits the bid. Mirrors the EXCHANGE_RULES §15 example.
  const std::vector<Scripted> script = {
      {"MM1", lim(mm1, 1, mc::Side::Sell, 1003, 10)},    // ask $10.03 x10
      {"MM2", lim(mm2, 1, mc::Side::Sell, 1003, 15)},    // ask $10.03 x15 (behind)
      {"MM1", lim(mm1, 2, mc::Side::Sell, 1005, 40)},    // ask $10.05 x40
      {"MM1", lim(mm1, 3, mc::Side::Buy, 1001, 20)},     // bid $10.01 x20
      {"MM2", lim(mm2, 2, mc::Side::Buy, 1000, 30)},     // bid $10.00 x30
      {"TKR", mkt(taker, 1, mc::Side::Buy, 60)},         // MARKET BUY 60 -> sweeps
      {"TKR", lim(taker, 2, mc::Side::Sell, 1001, 25)},  // SELL crosses the bid
  };

  std::cout << "MicroSim — deterministic exchange simulator\n";
  std::cout << "Instrument " << instr.symbol << "   tick $0.01   band "
            << usd(instr.min_price, instr) << "–" << usd(instr.max_price, instr)
            << "   fees: taker " << usd(instr.fees.taker_fee_per_lot) << "/lot, maker rebate "
            << usd(instr.fees.maker_rebate_per_lot) << "/lot\n\n";

  int trade_count = 0;
  long long volume = 0;
  for (const Scripted& line : script) {
    const bool is_market = line.msg.type == mc::OrderType::Market;
    std::cout << ">> " << line.who << "  " << (is_market ? "MARKET " : "LIMIT ")
              << side_str(line.msg.side) << " " << line.msg.qty.lots();
    if (!is_market) {
      std::cout << " @ " << usd(line.msg.price, instr);
    }
    std::cout << "\n";

    const std::vector<mc::Outbound> evs = eng.process(line.msg);
    print_events(evs, instr);
    for (const mc::Outbound& e : evs) {
      if (std::holds_alternative<mc::Trade>(e)) {
        ++trade_count;
        volume += std::get<mc::Trade>(e).qty.lots();
      }
    }
  }

  print_book(eng);
  std::cout << "\n  " << trade_count << " trades, " << volume << " lots matched, "
            << eng.registry().size() << " orders processed.\n";
  return 0;
}
