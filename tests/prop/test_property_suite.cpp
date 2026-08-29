#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/book/reference_book.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/engine/sequencer.hpp"

#include "property_runner.hpp"
#include "scenario_gen.hpp"
#include "shrinker.hpp"

// R1-18: the property suite. Every implemented ENGINE_INVARIANTS.md entry is
// asserted after every message of engine-driven generated scenarios, across all
// PROPERTY_TESTS.md profiles and several seeds. A failure is shrunk to a minimal
// stream and printed with the reproducing (profile, seed, N) triple.

namespace mp = microsim::prop;
namespace mc = microsim::core;
namespace me = microsim::engine;

namespace {

// Per-PR budget (seconds). Nightly scales seeds and N up per PROPERTY_TESTS.md.
constexpr std::size_t kMessagesPerScenario = 800;
constexpr std::uint64_t kSeeds[] = {1, 2, 3};

struct NamedProfile {
  mp::Profile profile;
  const char* name;
};

constexpr NamedProfile kProfiles[] = {
    {mp::Profile::Uniform, "uniform"},
    {mp::Profile::CancelHeavy, "cancel_heavy"},
    {mp::Profile::CrossingHeavy, "crossing_heavy"},
    {mp::Profile::Adversarial, "adversarial"},
    {mp::Profile::Boundary, "boundary"},
    {mp::Profile::EmptyBook, "empty_book"},
    {mp::Profile::GapBook, "gap_book"},
};

// Count how many events of a kind a scenario produces (coverage sanity checks).
template <class Event>
std::size_t count_events(const std::vector<mc::Inbound>& scenario) {
  me::Sequencer<microsim::book::ReferenceBook> seqr{mp::make_property_venue({}),
                                                    mp::instrument_config({})};
  std::size_t n = 0;
  mc::SimTime clock = mc::SimTime::zero();
  for (const mc::Inbound& msg : scenario) {
    for (const mc::SequencedEvent& e : seqr.submit(msg, clock)) {
      n += std::holds_alternative<Event>(e.event);
    }
    clock += mc::Duration{1};
  }
  return n;
}

}  // namespace

// ----- the main property: nothing ever violates an invariant ------------------

TEST(PropSuite, EveryInvariantHoldsAcrossProfilesAndSeeds) {
  for (const NamedProfile& np : kProfiles) {
    for (std::uint64_t seed : kSeeds) {
      mp::ScenarioGen gen(seed, np.profile);
      std::vector<mc::Inbound> scenario = gen.generate(kMessagesPerScenario);
      scenario.push_back(mc::SessionEnd{});  // exercise R-12 close + INV-17 each run

      const std::optional<std::string> violation = mp::find_violation(scenario);
      if (violation.has_value()) {
        const std::vector<mc::Inbound> minimal = mp::shrink(
            scenario,
            [](const std::vector<mc::Inbound>& s) { return mp::find_violation(s).has_value(); });
        ADD_FAILURE() << "profile=" << np.name << " seed=" << seed << " N=" << kMessagesPerScenario
                      << "\n  first violation: " << *violation << "\n  shrank " << scenario.size()
                      << " -> " << minimal.size() << " messages";
      }
    }
  }
}

// ----- INV-10: determinism across every profile/seed --------------------------

TEST(PropSuite, RunsAreDeterministic) {
  for (const NamedProfile& np : kProfiles) {
    for (std::uint64_t seed : kSeeds) {
      mp::ScenarioGen gen(seed, np.profile);
      const std::vector<mc::Inbound> scenario = gen.generate(kMessagesPerScenario);
      EXPECT_TRUE(mp::is_deterministic(scenario))
          << "non-deterministic: profile=" << np.name << " seed=" << seed;
    }
  }
}

// ----- coverage sanity: the interesting profiles actually do the thing --------

TEST(PropSuite, CrossingHeavyProducesTrades) {
  mp::ScenarioGen gen(1, mp::Profile::CrossingHeavy);
  const auto scenario = gen.generate(kMessagesPerScenario);
  EXPECT_GT(count_events<mc::Trade>(scenario), 0u) << "crossing_heavy generated no trades";
}

TEST(PropSuite, CancelHeavyActuallyCancelsRestingOrders) {
  // Proves the generator is state-aware: cancels hit live orders (not just
  // unknown random ids), so real OrderCanceled(ByRequest) events are produced.
  mp::ScenarioGen gen(1, mp::Profile::CancelHeavy);
  const auto scenario = gen.generate(kMessagesPerScenario);
  std::size_t by_request = 0;
  me::Sequencer<microsim::book::ReferenceBook> seqr{mp::make_property_venue({}),
                                                    mp::instrument_config({})};
  mc::SimTime clock = mc::SimTime::zero();
  for (const mc::Inbound& msg : scenario) {
    for (const mc::SequencedEvent& e : seqr.submit(msg, clock)) {
      if (const auto* c = std::get_if<mc::OrderCanceled>(&e.event);
          c != nullptr && c->reason == mc::CancelReason::ByRequest) {
        ++by_request;
      }
    }
    clock += mc::Duration{1};
  }
  EXPECT_GT(by_request, 0u) << "cancel_heavy never canceled a live order";
}
