#pragma once

/// \file
/// The sequencer (task R1-11): the single point that turns a stream of inbound
/// messages into a sequenced stream of outbound events, implementing the
/// ordering and determinism rules of EXCHANGE_RULES.md §10.
///
/// Responsibilities (R-10):
///   * R-10.1 — assign every inbound message a strictly-increasing `seq` in
///     arrival order (the order it is submitted).
///   * R-10.2 — stamp every outbound event the engine produces with a gap-free
///     `seq_out`, the triggering inbound `seq`, and `ts_event` (the logical time
///     the message was processed).
///   * R-10.3 — because the engine is a pure function of the sequenced stream
///     and the sequencer reads no wall clock and no unseeded randomness, two
///     sequencers fed the same (message, time) sequence emit equal event
///     streams. `SequencedEvent` equality (events.hpp) is exactly that check.
///
/// This is the minimal Tier-A/B sequencer: it owns the counters and the logical
/// clock and drives one MatchingEngine. Logical time is supplied per message by
/// the caller (the demo CLI or a test harness) — the standalone simulation clock
/// (R1-08) and the on-disk input/event log with CRC framing (the persist half of
/// R1-11) are separate, later tasks; nothing here reads time or state from
/// outside the sequenced stream, so adding them cannot change the output.

#include <cassert>
#include <utility>
#include <vector>

#include "microsim/book/order_book.hpp"
#include "microsim/core/config.hpp"
#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/matching_engine.hpp"
#include "microsim/engine/venue.hpp"

namespace microsim::engine {

/// Sequences one engine over one book type. Templated on the same OrderBookLike
/// concept as the engine so the identical sequencing runs over the ReferenceBook
/// oracle now and FastBook (R1-19) later — the differential harness (R1-20)
/// compares the SequencedEvent streams of two such sequencers.
template <class Book>
  requires book::OrderBookLike<Book>
class Sequencer {
 public:
  Sequencer(Venue venue, core::InstrumentConfig instrument)
      : engine_(std::move(venue), std::move(instrument)) {}

  /// Submit one inbound message stamped at logical time `ts`. Assigns it the
  /// next inbound `seq` (R-10.1), runs the engine to completion (R-5.1), and
  /// returns every resulting event stamped with a gap-free `seq_out`, the
  /// triggering `seq_in`, and `ts_event = ts` (R-10.2), in emission order.
  ///
  /// Logical time must not move backwards between calls: arrival order is fully
  /// determined by the clock (R-10.1), so an out-of-order timestamp would be a
  /// caller bug. Equal timestamps are allowed (several messages at one instant,
  /// ordered by submission).
  std::vector<core::SequencedEvent> submit(const core::Inbound& msg, core::SimTime ts) {
    assert(ts >= clock_ && "logical time must be non-decreasing (R-10.1)");
    clock_ = ts;

    const core::Seq seq_in = next_seq_;
    next_seq_ = next_seq_.next();

    std::vector<core::Outbound> payloads = engine_.process(msg);

    std::vector<core::SequencedEvent> stamped;
    stamped.reserve(payloads.size());
    for (auto& payload : payloads) {
      stamped.push_back(core::SequencedEvent{
          .header = core::EventHeader{.seq_out = next_seq_out_, .seq_in = seq_in, .ts_event = ts},
          .event = std::move(payload)});
      next_seq_out_ = next_seq_out_.next();
    }
    return stamped;
  }

  // ----- read-only views -------------------------------------------------------

  [[nodiscard]] const MatchingEngine<Book>& engine() const noexcept { return engine_; }

  /// The `seq` the next submitted message will receive (R-10.1).
  [[nodiscard]] core::Seq next_seq() const noexcept { return next_seq_; }

  /// The `seq_out` the next emitted event will receive (R-10.2).
  [[nodiscard]] core::Seq next_seq_out() const noexcept { return next_seq_out_; }

  /// The current logical time (the timestamp of the most recent submission).
  [[nodiscard]] core::SimTime clock() const noexcept { return clock_; }

 private:
  MatchingEngine<Book> engine_;
  core::Seq next_seq_{core::Seq::first()};      ///< inbound counter (R-10.1)
  core::Seq next_seq_out_{core::Seq::first()};  ///< outbound counter (R-10.2)
  core::SimTime clock_{core::SimTime::zero()};  ///< logical time at last submit
};

}  // namespace microsim::engine
