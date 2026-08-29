#pragma once

/// \file
/// Wiring that drives a generated scenario through the engine and checks every
/// implemented invariant after each message (task R1-18). Shared by the property
/// suite and the shrinker's unit tests.
///
/// Coverage: the InvariantMonitor runs INV-1..9, 12, 13, 14 after every message;
/// seq_out gap-freeness (part of INV-9) is checked off the sequencer; INV-10
/// (determinism) via `is_deterministic`; INV-17 (session-end cleanliness) after a
/// close. INV-11 (accounting) is partial until E11 — the position-sum-zero part is
/// checked here; INV-15 (FastBook vs ReferenceBook) lands with FastBook (R1-20).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/invariants.hpp"
#include "microsim/engine/sequencer.hpp"
#include "microsim/engine/venue.hpp"

#include "scenario_gen.hpp"

namespace microsim::prop {

namespace me = microsim::engine;

/// The instrument the generated scenarios trade, matching ScenarioConfig.
inline mc::InstrumentConfig instrument_config(const ScenarioConfig& cfg) {
  return mc::InstrumentConfig{.id = cfg.instrument,
                              .symbol = "PROP",
                              .tick_size = 1,
                              .lot_size = 1,
                              .min_price = mc::Price{cfg.min_price},
                              .max_price = mc::Price{cfg.max_price},
                              .max_order_qty = mc::Qty{cfg.max_order_qty}};
}

/// A venue with the scenario's instrument and its participants, all with
/// unlimited risk (so a well-formed order is always accepted — which is what
/// keeps the generator's order-id prediction exact).
inline me::Venue make_property_venue(const ScenarioConfig& cfg) {
  me::Venue v;
  v.add_instrument(instrument_config(cfg));
  for (std::uint32_t i = 1; i <= cfg.num_participants; ++i) {
    v.add_participant(mc::ParticipantConfig{.id = mc::ParticipantId{i}});
  }
  return v;
}

/// Run one scenario through a fresh engine, checking every invariant after each
/// message. Returns a description of the first violation, or nullopt if the whole
/// stream was clean. This is the predicate the shrinker minimizes against.
inline std::optional<std::string> find_violation(const std::vector<mc::Inbound>& scenario,
                                                 const ScenarioConfig& cfg = {}) {
  me::Sequencer<microsim::book::ReferenceBook> seqr{make_property_venue(cfg),
                                                    instrument_config(cfg)};
  me::InvariantMonitor monitor;
  mc::Seq expected_seq_out = mc::Seq::first();
  mc::SimTime clock = mc::SimTime::zero();

  for (const mc::Inbound& msg : scenario) {
    const std::vector<mc::SequencedEvent> stamped = seqr.submit(msg, clock);
    clock += mc::Duration{1};

    std::vector<mc::Outbound> payloads;
    payloads.reserve(stamped.size());
    for (const mc::SequencedEvent& e : stamped) {
      if (e.header.seq_out != expected_seq_out) {
        return std::string("INV-9: seq_out is not gap-free / strictly increasing");
      }
      expected_seq_out = expected_seq_out.next();
      payloads.push_back(e.event);
    }

    const std::vector<me::InvariantViolation> v = monitor.after_message(seqr.engine(), payloads);
    if (!v.empty()) {
      return std::string(v.front().id) + ": " + v.front().detail;
    }
  }

  // INV-11 (partial): lots are conserved — the net position over all participants
  // is zero (every bought lot was sold). Full cash/P&L reconciliation is E11.
  std::int64_t net = 0;
  for (std::uint32_t i = 1; i <= cfg.num_participants; ++i) {
    net += seqr.engine().risk().position(mc::ParticipantId{i});
  }
  if (net != 0) {
    return std::string("INV-11: net position over participants is not zero");
  }

  // INV-17: once closed, the book must be empty (all resting orders canceled).
  if (seqr.engine().session_state() == me::SessionState::Closed &&
      (!seqr.engine().book().empty(mc::Side::Buy) || !seqr.engine().book().empty(mc::Side::Sell))) {
    return std::string("INV-17: book not empty after session close");
  }
  return std::nullopt;
}

/// INV-10: two fresh runs of the same scenario emit byte-identical event streams.
inline bool is_deterministic(const std::vector<mc::Inbound>& scenario,
                             const ScenarioConfig& cfg = {}) {
  const auto run = [&]() {
    me::Sequencer<microsim::book::ReferenceBook> seqr{make_property_venue(cfg),
                                                      instrument_config(cfg)};
    std::vector<mc::SequencedEvent> all;
    mc::SimTime clock = mc::SimTime::zero();
    for (const mc::Inbound& msg : scenario) {
      const std::vector<mc::SequencedEvent> stamped = seqr.submit(msg, clock);
      clock += mc::Duration{1};
      all.insert(all.end(), stamped.begin(), stamped.end());
    }
    return all;
  };
  return run() == run();
}

}  // namespace microsim::prop
