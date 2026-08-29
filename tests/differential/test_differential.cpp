#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "microsim/book/fast_book.hpp"
#include "microsim/book/order_book.hpp"
#include "microsim/book/reference_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"

#include "differential_harness.hpp"
#include "scenario_gen.hpp"

// R1-20: the FastBook-vs-ReferenceBook differential suite (INV-15). Every
// generator profile is run at several seeds through both books in lockstep and
// asserted event-for-event and state-for-state identical. The MutantBook tests
// prove the harness is not vacuous: a book with a planted bug is caught.

namespace md = microsim::diff;
namespace mp = microsim::prop;
namespace mb = microsim::book;
namespace mc = microsim::core;

using mc::Price;
using mc::Qty;
using mc::Side;

namespace {

// The CI-per-PR budget (PROPERTY_TESTS.md §Budgets, differential slice): every
// profile, a few seeds, a few thousand messages each. Kept to seconds; the
// nightly all-profiles × 100-seeds × 10^6 run is a separate, larger job.
constexpr mp::Profile kProfiles[] = {
    mp::Profile::Uniform,     mp::Profile::CancelHeavy, mp::Profile::CrossingHeavy,
    mp::Profile::Adversarial, mp::Profile::Boundary,    mp::Profile::EmptyBook,
    mp::Profile::GapBook,
};

}  // namespace

// ----- FastBook agrees with the ReferenceBook oracle on every scenario --------

TEST(Differential, FastBookMatchesReferenceAcrossProfiles) {
  for (const mp::Profile profile : kProfiles) {
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
      const std::optional<std::string> divergence =
          md::check_fast_vs_reference(seed, profile, /*n=*/2000);
      EXPECT_FALSE(divergence.has_value())
          << "profile " << static_cast<int>(profile) << " seed " << seed << ": " << *divergence;
    }
  }
}

// A single long crossing-heavy run: maximal matching pressure, so any divergence
// in the trade sequence (the event-stream half) surfaces. The full nightly
// budget (all profiles × 100 seeds × 10^6 messages, ASan+UBSan on) is a separate
// job per PROPERTY_TESTS.md §Budgets; this in-suite run is the per-PR slice.
TEST(Differential, FastBookMatchesReferenceOnLongCrossingRun) {
  const std::optional<std::string> divergence =
      md::check_fast_vs_reference(/*seed=*/424242, mp::Profile::CrossingHeavy, /*n=*/8000);
  EXPECT_FALSE(divergence.has_value()) << (divergence ? *divergence : std::string{});
}

// ----- the harness catches a deliberately-broken book -------------------------

// A book that is correct in every operation the *engine* drives (so it never
// corrupts the run or trips an assertion) but whose canonical snapshot lies: it
// reverses the FIFO order within each price level. That is a real INV-15
// violation — a book reporting queue position wrong — and the harness's state-
// dump half must catch it the moment any level holds two or more orders. It
// wraps a real FastBook and delegates everything except dump_state, so the bug
// is exactly one planted mutation.
class MutantBook {
 public:
  explicit MutantBook(const mc::InstrumentConfig& instrument) : impl_(instrument) {}

  void add(const mb::RestingOrder& order) { impl_.add(order); }

  void reduce(mc::OrderId id, Qty new_remaining) { impl_.reduce(id, new_remaining); }

  void remove(mc::OrderId id) { impl_.remove(id); }

  void clear() { impl_.clear(); }

  [[nodiscard]] const mb::RestingOrder* find(mc::OrderId id) const { return impl_.find(id); }

  [[nodiscard]] const mb::RestingOrder* front(Side side) const { return impl_.front(side); }

  [[nodiscard]] std::optional<Price> best(Side side) const { return impl_.best(side); }

  [[nodiscard]] Qty depth(Side side, Price price) const { return impl_.depth(side, price); }

  [[nodiscard]] bool empty(Side side) const { return impl_.empty(side); }

  [[nodiscard]] mb::BookState dump_state() const {
    mb::BookState state = impl_.dump_state();
    for (mb::BookLevelState& level : state.bids) {
      std::reverse(level.orders.begin(), level.orders.end());  // BUG: FIFO reported backwards
    }
    for (mb::BookLevelState& level : state.asks) {
      std::reverse(level.orders.begin(), level.orders.end());
    }
    return state;
  }

 private:
  mb::FastBook impl_;
};

static_assert(mb::OrderBookLike<MutantBook>, "the mutant must still satisfy the interface");

TEST(Differential, HarnessCatchesMutantBook) {
  // Two limit orders rest at the same price: a level with two orders exists, so
  // the mutant's reversed dump_state diverges from the reference.
  const mp::ScenarioConfig cfg{};
  std::vector<mc::Inbound> scenario;
  const auto sell = [&](std::uint32_t party, std::uint64_t clord, std::int64_t px,
                        std::int64_t qty) {
    scenario.push_back(mc::NewOrder{.participant = mc::ParticipantId{party},
                                    .client_order_id = mc::ClientOrderId{clord},
                                    .instrument = cfg.instrument,
                                    .side = Side::Sell,
                                    .type = mc::OrderType::Limit,
                                    .qty = Qty{qty},
                                    .price = Price{px}});
  };
  sell(1, 1, 1200, 5);
  sell(2, 1, 1200, 7);  // same level -> two resting orders

  const std::optional<std::string> divergence =
      md::find_divergence<MutantBook, mb::ReferenceBook>(scenario, cfg);
  ASSERT_TRUE(divergence.has_value()) << "the harness failed to catch a book that lies about FIFO";
  EXPECT_NE(divergence->find("message 1"), std::string::npos);  // caught at the second order
}

TEST(Differential, HarnessPassesWhenBothBooksAreCorrect) {
  // Control: the same two-order scenario through two correct books diverges never
  // (so the mutant test above is discriminating, not always-failing).
  const mp::ScenarioConfig cfg{};
  const std::optional<std::string> divergence =
      md::check_fast_vs_reference(/*seed=*/1, mp::Profile::CancelHeavy, /*n=*/500, cfg);
  EXPECT_FALSE(divergence.has_value()) << (divergence ? *divergence : std::string{});
}
