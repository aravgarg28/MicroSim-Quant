#pragma once

/// \file
/// Deterministic scenario generator for the property suite (task R1-18, decision
/// S3: custom generators over RapidCheck). A `(seed, profile, N)` triple fully
/// determines the message stream, so any failure is reproducible from three
/// numbers and a stream can be shrunk to a committed fixture.
///
/// The generator is *state-aware*: it tracks the ids of orders it has created so
/// cancels and modifies target real orders instead of almost-always-dead random
/// ids (the classic naive-fuzzing gap). It does not run the engine — it predicts
/// order-id assignment (the engine hands out ids 1..N to accepted orders in
/// arrival order) by counting the orders it emits that a correct engine accepts.
/// The property venue registers these participants with unlimited risk, so a
/// well-formed order is always accepted and the prediction stays exact; malformed
/// / out-of-band / duplicate orders are rejected and deliberately not counted.

#include <cstdint>
#include <utility>
#include <vector>

#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"

namespace microsim::prop {

namespace mc = microsim::core;

/// The instrument and participant set every generated scenario is written
/// against; the property venue must be built to match (see make_property_venue).
struct ScenarioConfig {
  mc::InstrumentId instrument{1};
  std::int64_t min_price = 500;
  std::int64_t max_price = 1500;
  std::int64_t max_order_qty = 100;
  std::uint32_t num_participants = 4;  ///< participant ids 1..num_participants
  std::int64_t reference_mid = 1000;   ///< used only to steer marketability
};

/// Generation bias. Each profile is a set of action weights plus price steering,
/// matching PROPERTY_TESTS.md.
enum class Profile {
  Uniform,        ///< an even mix of every action
  CancelHeavy,    ///< realistic: mostly rests and cancels
  CrossingHeavy,  ///< orders overlap around the mid → deep matching
  Adversarial,    ///< duplicates, malformed enums, out-of-band values
  Boundary,       ///< prices at band edges, qty at max, invalid extremes
  EmptyBook,      ///< markets + cancels that repeatedly drain the book
  GapBook,        ///< orders at spread-apart prices, then markets through them
};

/// A tiny, platform-stable PRNG (SplitMix64) — integer-only, so the same seed
/// yields the same stream on every OS/compiler (a hard R1-18 requirement). This
/// is the minimal slice of the deferred RNG task (R1-07) the generator needs.
class SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

  std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  /// Uniform-ish in [0, n) (modulo bias is irrelevant for scenario shaping).
  std::uint64_t below(std::uint64_t n) noexcept { return n == 0 ? 0 : next() % n; }

  /// Uniform-ish integer in [lo, hi].
  std::int64_t between(std::int64_t lo, std::int64_t hi) noexcept {
    if (hi <= lo) {
      return lo;
    }
    return lo + static_cast<std::int64_t>(below(static_cast<std::uint64_t>(hi - lo + 1)));
  }

  bool chance(std::uint64_t percent) noexcept { return below(100) < percent; }

 private:
  std::uint64_t state_;
};

class ScenarioGen {
 public:
  ScenarioGen(std::uint64_t seed, Profile profile, ScenarioConfig cfg = {}) noexcept
      : rng_(seed), profile_(profile), cfg_(cfg) {}

  /// Produce exactly `n` inbound messages. Pure function of (seed, profile, n).
  std::vector<mc::Inbound> generate(std::size_t n) {
    std::vector<mc::Inbound> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      out.push_back(next_message());
    }
    return out;
  }

 private:
  // Action kinds, selected by per-profile weights.
  enum class Action {
    NewLimitRest,     // non-marketable limit (rests)
    NewLimitCross,    // marketable limit (likely trades)
    NewMarket,        // market order
    Cancel,           // cancel a created order
    Modify,           // modify a created order
    DuplicateClord,   // re-use a client_order_id (rejected)
    Malformed,        // out-of-range enum byte (rejected)
    OutOfBandPrice,   // price outside the instrument band (rejected)
    SessionEndInject  // close the session
  };

  [[nodiscard]] mc::ParticipantId some_participant() {
    return mc::ParticipantId{static_cast<std::uint32_t>(rng_.between(1, cfg_.num_participants))};
  }

  [[nodiscard]] mc::Price in_band_price() {
    return mc::Price{rng_.between(cfg_.min_price, cfg_.max_price)};
  }

  [[nodiscard]] mc::Qty some_qty() { return mc::Qty{rng_.between(1, cfg_.max_order_qty)}; }

  [[nodiscard]] mc::Price steered_price(mc::Side side, bool marketable) {
    const std::int64_t mid = cfg_.reference_mid;
    if (profile_ == Profile::Boundary) {
      return mc::Price{rng_.chance(50) ? cfg_.min_price : cfg_.max_price};
    }
    if (profile_ == Profile::GapBook) {
      // Cluster at a few spread-apart prices to leave gaps between levels.
      const std::int64_t buckets[] = {cfg_.min_price, mid - 100, mid + 100, cfg_.max_price};
      return mc::Price{buckets[rng_.below(4)]};
    }
    if (side == mc::Side::Buy) {
      return marketable ? mc::Price{rng_.between(mid, cfg_.max_price)}
                        : mc::Price{rng_.between(cfg_.min_price, mid - 1)};
    }
    return marketable ? mc::Price{rng_.between(cfg_.min_price, mid)}
                      : mc::Price{rng_.between(mid + 1, cfg_.max_price)};
  }

  /// A client_order_id unique to that participant (so dedup only fires when the
  /// DuplicateClord action deliberately re-uses one).
  [[nodiscard]] mc::ClientOrderId fresh_clord(mc::ParticipantId p) {
    return mc::ClientOrderId{++clord_counters_[p.value() - 1]};
  }

  [[nodiscard]] mc::NewOrder new_limit(bool marketable) {
    const mc::ParticipantId p = some_participant();
    const mc::Side side = rng_.chance(50) ? mc::Side::Buy : mc::Side::Sell;
    mc::NewOrder m{.participant = p,
                   .client_order_id = fresh_clord(p),
                   .instrument = cfg_.instrument,
                   .side = side,
                   .type = mc::OrderType::Limit,
                   .qty = profile_ == Profile::Boundary ? mc::Qty{cfg_.max_order_qty} : some_qty(),
                   .price = steered_price(side, marketable)};
    on_created(p);
    return m;
  }

  [[nodiscard]] mc::NewOrder new_market() {
    const mc::ParticipantId p = some_participant();
    mc::NewOrder m{.participant = p,
                   .client_order_id = fresh_clord(p),
                   .instrument = cfg_.instrument,
                   .side = rng_.chance(50) ? mc::Side::Buy : mc::Side::Sell,
                   .type = mc::OrderType::Market,
                   .qty = some_qty(),
                   .price = mc::Price{}};
    on_created(p);  // accepted markets also consume an order_id (create() runs)
    return m;
  }

  /// Pick an order to target. Usually one we created (so it exists in the
  /// registry); sometimes a random id (often unknown) for the adversarial path.
  [[nodiscard]] std::pair<mc::OrderId, mc::ParticipantId> pick_target() {
    if (!created_.empty() && rng_.chance(85)) {
      const auto& [id, owner] = created_[rng_.below(created_.size())];
      // Usually the true owner (a real cancel/modify); sometimes a wrong owner.
      const mc::ParticipantId who = rng_.chance(80) ? owner : some_participant();
      return {id, who};
    }
    const auto id = mc::OrderId{static_cast<std::uint64_t>(rng_.between(1, next_order_id_ + 3))};
    return {id, some_participant()};
  }

  [[nodiscard]] mc::CancelOrder cancel() {
    const auto [id, who] = pick_target();
    return mc::CancelOrder{.participant = who, .order_id = id};
  }

  [[nodiscard]] mc::ModifyOrder modify() {
    const auto [id, who] = pick_target();
    // A spread of R-7 cases: qty up/down, price up/down, no-op, qty→small.
    const std::int64_t new_qty = rng_.chance(15) ? rng_.between(0, 2)  // may hit ≤ filled / invalid
                                                 : rng_.between(1, cfg_.max_order_qty);
    return mc::ModifyOrder{.participant = who,
                           .order_id = id,
                           .new_qty = mc::Qty{new_qty},
                           .new_price = in_band_price()};
  }

  [[nodiscard]] mc::NewOrder duplicate_clord() {
    // Re-use a client_order_id the participant has already been issued so this is
    // a *guaranteed* duplicate (rejected, no order_id consumed — the prediction
    // stays exact). If nobody has ordered yet there is nothing to duplicate, so
    // fall back to a normal (accepted, counted) order.
    const mc::ParticipantId p{1};
    if (clord_counters_[0] == 0) {
      return new_limit(/*marketable=*/false);
    }
    return mc::NewOrder{.participant = p,
                        .client_order_id = mc::ClientOrderId{clord_counters_[0]},
                        .instrument = cfg_.instrument,
                        .side = mc::Side::Buy,
                        .type = mc::OrderType::Limit,
                        .qty = some_qty(),
                        .price = in_band_price()};  // duplicate: rejected, not counted
  }

  [[nodiscard]] mc::NewOrder malformed() {
    const mc::ParticipantId p = some_participant();
    mc::NewOrder m{.participant = p,
                   .client_order_id = fresh_clord(p),
                   .instrument = cfg_.instrument,
                   .side = static_cast<mc::Side>(7),       // out-of-range enum byte
                   .type = static_cast<mc::OrderType>(9),  // out-of-range enum byte
                   .qty = some_qty(),
                   .price = in_band_price()};
    return m;  // rejected MALFORMED, no id consumed
  }

  [[nodiscard]] mc::NewOrder out_of_band_price() {
    const mc::ParticipantId p = some_participant();
    const mc::Side side = rng_.chance(50) ? mc::Side::Buy : mc::Side::Sell;
    mc::NewOrder m{.participant = p,
                   .client_order_id = fresh_clord(p),
                   .instrument = cfg_.instrument,
                   .side = side,
                   .type = mc::OrderType::Limit,
                   .qty = some_qty(),
                   .price = mc::Price{rng_.chance(50) ? cfg_.max_price + rng_.between(1, 500)
                                                      : cfg_.min_price - rng_.between(1, 400)}};
    return m;  // rejected PRICE_OUT_OF_BANDS, no id consumed
  }

  void on_created(mc::ParticipantId p) {
    if (closed_) {
      return;  // post-close: rejected MARKET_CLOSED, no id consumed
    }
    const auto id = mc::OrderId{next_order_id_++};
    created_.push_back({id, p});
  }

  [[nodiscard]] Action pick_action() {
    // Per-profile weights over the nine actions, in the Action enum's order.
    // The session-end column is 0: a mid-stream close is permanent (R-12.1) and
    // would reject everything after it, collapsing coverage. Session end is
    // exercised by appending one SessionEnd to a scenario (the property suite does
    // this), not by injecting it into the body.
    static constexpr int kW[][9] = {
        // rest crs  mkt  can  mod  dup  mal  oob  end
        {25, 15, 10, 20, 20, 2, 2, 2, 0},    // Uniform
        {30, 8, 5, 35, 15, 2, 1, 2, 0},      // CancelHeavy
        {15, 45, 20, 8, 8, 1, 1, 1, 0},      // CrossingHeavy
        {12, 10, 8, 14, 14, 14, 12, 14, 0},  // Adversarial
        {18, 12, 10, 12, 12, 6, 6, 20, 0},   // Boundary
        {8, 10, 40, 30, 6, 1, 1, 3, 0},      // EmptyBook
        {30, 10, 25, 15, 12, 2, 2, 3, 0},    // GapBook
    };
    const int* w = kW[static_cast<std::size_t>(profile_)];
    int total = 0;
    for (int i = 0; i < 9; ++i) {
      total += w[i];
    }
    std::int64_t roll = rng_.between(0, total - 1);
    for (int i = 0; i < 9; ++i) {
      roll -= w[i];
      if (roll < 0) {
        return static_cast<Action>(i);
      }
    }
    return Action::NewLimitRest;
  }

  mc::Inbound next_message() {
    switch (pick_action()) {
      case Action::NewLimitRest:
        return new_limit(/*marketable=*/false);
      case Action::NewLimitCross:
        return new_limit(/*marketable=*/true);
      case Action::NewMarket:
        return new_market();
      case Action::Cancel:
        return cancel();
      case Action::Modify:
        return modify();
      case Action::DuplicateClord:
        return duplicate_clord();
      case Action::Malformed:
        return malformed();
      case Action::OutOfBandPrice:
        return out_of_band_price();
      case Action::SessionEndInject:
        closed_ = true;
        return mc::SessionEnd{};
    }
    return new_limit(false);
  }

  SplitMix64 rng_;
  Profile profile_;
  ScenarioConfig cfg_;
  std::uint64_t next_order_id_ = 1;
  bool closed_ = false;
  std::vector<std::pair<mc::OrderId, mc::ParticipantId>> created_;
  std::uint64_t clord_counters_[64] = {};  // per-participant next client_order_id
};

}  // namespace microsim::prop
