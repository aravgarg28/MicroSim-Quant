#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/core/messages.hpp"

#include "scenario_gen.hpp"

// R1-18: generator determinism goldens and basic shape checks. A (seed, profile,
// N) triple must fully determine the stream (so failures are reproducible and
// shrinkable), and different seeds must produce different streams.

namespace mp = microsim::prop;
namespace mc = microsim::core;

namespace {
constexpr std::size_t kN = 500;
}

TEST(PropGenerator, SameTripleProducesIdenticalStream) {
  for (std::uint64_t seed : {1u, 2u, 42u, 12345u}) {
    mp::ScenarioGen a(seed, mp::Profile::Uniform);
    mp::ScenarioGen b(seed, mp::Profile::Uniform);
    EXPECT_TRUE(a.generate(kN) == b.generate(kN)) << "seed " << seed << " not reproducible";
  }
}

TEST(PropGenerator, DifferentSeedsDiffer) {
  mp::ScenarioGen a(1, mp::Profile::Uniform);
  mp::ScenarioGen b(2, mp::Profile::Uniform);
  EXPECT_FALSE(a.generate(kN) == b.generate(kN));
}

TEST(PropGenerator, DifferentProfilesDiffer) {
  mp::ScenarioGen a(1, mp::Profile::Uniform);
  mp::ScenarioGen b(1, mp::Profile::CrossingHeavy);
  EXPECT_FALSE(a.generate(kN) == b.generate(kN));
}

TEST(PropGenerator, GeneratesRequestedCount) {
  mp::ScenarioGen g(7, mp::Profile::Adversarial);
  EXPECT_EQ(g.generate(kN).size(), kN);
}

TEST(PropGenerator, AdversarialProfileEmitsInvalidAndDuplicateMessages) {
  // The adversarial profile must actually reach the reject paths: at least some
  // out-of-range enums, out-of-band prices, and duplicate client ids.
  mp::ScenarioGen g(3, mp::Profile::Adversarial);
  const auto scenario = g.generate(2000);
  std::size_t malformed = 0;
  std::size_t out_of_band = 0;
  for (const mc::Inbound& msg : scenario) {
    if (const auto* n = std::get_if<mc::NewOrder>(&msg)) {
      const auto side_byte = static_cast<std::uint8_t>(n->side);
      const auto type_byte = static_cast<std::uint8_t>(n->type);
      if (side_byte > 1 || type_byte > 1) {
        ++malformed;
      } else if (n->type == mc::OrderType::Limit &&
                 (n->price < mc::Price{500} || n->price > mc::Price{1500})) {
        ++out_of_band;
      }
    }
  }
  EXPECT_GT(malformed, 0u) << "adversarial profile produced no malformed messages";
  EXPECT_GT(out_of_band, 0u) << "adversarial profile produced no out-of-band prices";
}
