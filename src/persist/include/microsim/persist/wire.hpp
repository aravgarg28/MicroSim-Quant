#pragma once

/// \file
/// Field-wise encoding of the two log payloads (task R1-21): an **input record**
/// (a logical timestamp plus one inbound message — the sequencer's tee) and an
/// **event record** (one SequencedEvent the engine emitted). Every field is
/// written by name in a fixed order, so the on-disk form owns no struct padding
/// and round-trips exactly — the property replay depends on (INV-10).
///
/// A leading tag byte selects the variant alternative; it is the variant index,
/// so the message and event enumerations here must stay in step with
/// messages.hpp / events.hpp (a decode of an unknown tag fails rather than
/// guesses). These build on the byte primitives in log.hpp and know nothing
/// about framing — the LogWriter/LogReader own that.

#include <string>
#include <string_view>

#include "microsim/core/events.hpp"
#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"

namespace microsim::persist {

/// Encode one input-log record: the message's logical time then the message.
[[nodiscard]] std::string encode_input(core::SimTime ts, const core::Inbound& msg);

/// Decode an input-log record. Returns false if the bytes are short or carry an
/// unknown tag (a corrupt or wrong-version record).
[[nodiscard]] bool decode_input(std::string_view bytes, core::SimTime& ts, core::Inbound& out);

/// Encode one event-log record: the sequencing header then the event payload.
[[nodiscard]] std::string encode_event(const core::SequencedEvent& ev);

/// Decode an event-log record. Returns false on a short or unknown-tag record.
[[nodiscard]] bool decode_event(std::string_view bytes, core::SequencedEvent& out);

}  // namespace microsim::persist
