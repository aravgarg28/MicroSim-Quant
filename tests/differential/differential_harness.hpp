#pragma once

/// \file
/// The differential harness (task R1-20, INV-15): drive one generated scenario
/// through two engines built over different book implementations and assert they
/// agree at every step — the same event stream out of each message, and the same
/// book state after it. This is the highest-leverage correctness check in the
/// project: it re-derives the entire matching semantics from two independent
/// data structures (FastBook's tick-indexed arrays vs ReferenceBook's map+list),
/// so a bug in either that changes any observable behavior is caught the moment
/// the scenario reaches it (REFERENCE_MODEL.md §"Differential harness").
///
///   for msg in scenario:
///       fast_events = engine<FastBook>.process(msg)
///       ref_events  = engine<ReferenceBook>.process(msg)
///       assert fast_events == ref_events      # event-stream equality (the stronger half)
///       assert dump(fast)  == dump(ref)       # levels, queues, aggregates
///
/// Event-stream equality is the stronger half: two books can reach the same
/// resting state via different (wrong) trades, which the state dump alone would
/// miss. The harness is templated on the two book types so the same code proves
/// FastBook ≡ ReferenceBook here and, in the mutation test, that a deliberately
/// broken book *is* caught (a check that can never fail is decoration).

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "microsim/book/fast_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/engine/matching_engine.hpp"

#include "property_runner.hpp"  // make_property_venue, instrument_config
#include "scenario_gen.hpp"     // ScenarioConfig, ScenarioGen, Profile

namespace microsim::diff {

namespace mc = microsim::core;
namespace me = microsim::engine;
namespace mp = microsim::prop;

/// Run `scenario` through an engine<BookA> and an engine<BookB> in lockstep,
/// returning a description of the first message at which they diverge, or nullopt
/// if the two agreed on every event and every book state to the end.
template <class BookA, class BookB>
inline std::optional<std::string> find_divergence(const std::vector<mc::Inbound>& scenario,
                                                  const mp::ScenarioConfig& cfg = {}) {
  me::MatchingEngine<BookA> a{mp::make_property_venue(cfg), mp::instrument_config(cfg)};
  me::MatchingEngine<BookB> b{mp::make_property_venue(cfg), mp::instrument_config(cfg)};

  for (std::size_t i = 0; i < scenario.size(); ++i) {
    const std::vector<mc::Outbound> ea = a.process(scenario[i]);
    const std::vector<mc::Outbound> eb = b.process(scenario[i]);
    if (ea != eb) {
      return "message " + std::to_string(i) + ": event streams differ (INV-15)";
    }
    if (a.book().dump_state() != b.book().dump_state()) {
      return "message " + std::to_string(i) + ": book state dumps differ (INV-15)";
    }
  }
  return std::nullopt;
}

/// Generate a `(seed, profile, n)` scenario and check FastBook against the
/// ReferenceBook oracle. A trailing SessionEnd is appended so the session-end
/// cancel sequence (which reads the book in canonical order) is compared too.
inline std::optional<std::string> check_fast_vs_reference(std::uint64_t seed, mp::Profile profile,
                                                          std::size_t n,
                                                          const mp::ScenarioConfig& cfg = {}) {
  std::vector<mc::Inbound> scenario = mp::ScenarioGen{seed, profile, cfg}.generate(n);
  scenario.emplace_back(mc::SessionEnd{});
  return find_divergence<microsim::book::FastBook, microsim::book::ReferenceBook>(scenario, cfg);
}

}  // namespace microsim::diff
