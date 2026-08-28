#pragma once

/// \file
/// The venue's registered reference data (task R1-10): the instruments and
/// participants that exist for a simulation. Registration happens before the
/// session starts (R-1.1 instruments immutable, R-2.1 participants pre-
/// registered); the gateway validates every inbound message against it (R-3.3
/// items 1–2). Lookups are by id and never iterated in an order that reaches
/// output, so determinism (R-10.3) is preserved.

#include <unordered_map>

#include "microsim/core/config.hpp"
#include "microsim/core/types.hpp"

namespace microsim::engine {

class Venue {
 public:
  /// Register an instrument (R-1.1). Its config must already be validated by the
  /// caller; the venue stores it verbatim. A duplicate id overwrites.
  void add_instrument(const core::InstrumentConfig& instrument) {
    instruments_.insert_or_assign(instrument.id, instrument);
  }

  /// Register a participant (R-2.1).
  void add_participant(const core::ParticipantConfig& participant) {
    participants_.insert_or_assign(participant.id, participant);
  }

  /// The instrument with this id, or nullptr if none is registered (drives
  /// UNKNOWN_INSTRUMENT, R-3.3 item 1).
  [[nodiscard]] const core::InstrumentConfig* find_instrument(core::InstrumentId id) const {
    auto it = instruments_.find(id);
    return it == instruments_.end() ? nullptr : &it->second;
  }

  /// The participant with this id, or nullptr if unregistered (drives
  /// UNKNOWN_PARTICIPANT, R-3.3 item 2).
  [[nodiscard]] const core::ParticipantConfig* find_participant(core::ParticipantId id) const {
    auto it = participants_.find(id);
    return it == participants_.end() ? nullptr : &it->second;
  }

 private:
  std::unordered_map<core::InstrumentId, core::InstrumentConfig> instruments_;
  std::unordered_map<core::ParticipantId, core::ParticipantConfig> participants_;
};

}  // namespace microsim::engine
