/// \file
/// `microsim_replay` (task R1-21) — replay a recorded input log and prove the
/// engine is a pure function of its sequenced input (INV-10, strength 3). It
/// reads `input.log` (inbound messages with their original logical timestamps),
/// feeds them straight into a fresh sequencer built from the same demo config,
/// and regenerates the event stream. Given the run's `events.log` it verifies
/// the regenerated stream is byte-identical and exits non-zero on any mismatch;
/// either way it prints the final book. No generation, no randomness — replay
/// reproduces exchange behavior, not decision-making (DATA_FLOW.md §"Replay").

#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "microsim/core/messages.hpp"
#include "microsim/core/types.hpp"
#include "microsim/engine/sequencer.hpp"
#include "microsim/persist/log.hpp"
#include "microsim/persist/wire.hpp"

#include "demo.hpp"

namespace md = microsim::demo;
namespace mc = microsim::core;
namespace me = microsim::engine;
namespace mp = microsim::persist;

namespace {

// Read a whole file into a string, or nullopt if it cannot be opened.
std::optional<std::string> slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: microsim_replay <input.log> [expected-events.log]\n";
    return 2;
  }
  const std::string input_path = argv[1];
  const std::optional<std::string> expected_path =
      argc > 2 ? std::optional<std::string>{argv[2]} : std::nullopt;

  std::ifstream input_file(input_path, std::ios::binary);
  if (!input_file) {
    std::cerr << "microsim_replay: cannot open input log '" << input_path << "'\n";
    return 1;
  }

  me::Sequencer<microsim::book::FastBook> seqr{md::demo_venue(), md::demo_instrument()};

  // Regenerate the event log into memory so it can be byte-compared to the
  // recorded one (a fresh LogWriter reproduces the same header + records).
  std::ostringstream regenerated;
  mp::LogWriter event_log(regenerated, mp::LogKind::Event);

  std::uint64_t message_count = 0;
  try {
    mp::LogReader reader(input_file);
    if (reader.kind() != mp::LogKind::Input) {
      std::cerr << "microsim_replay: '" << input_path << "' is not an input log\n";
      return 1;
    }
    while (const std::optional<std::string> rec = reader.next_record()) {
      mc::SimTime ts{};
      mc::Inbound msg;
      if (!mp::decode_input(*rec, ts, msg)) {
        std::cerr << "microsim_replay: undecodable input record #" << message_count << "\n";
        return 1;
      }
      ++message_count;
      const std::vector<mc::SequencedEvent> stamped = seqr.submit(msg, ts);  // original timestamps
      for (const mc::SequencedEvent& ev : stamped) {
        event_log.write_record(mp::encode_event(ev));
      }
    }
  } catch (const mp::LogError& e) {
    std::cerr << "microsim_replay: " << e.what() << "\n";
    return 1;
  }

  std::cout << "MicroSim replay — " << message_count << " messages from " << input_path << "\n";
  md::print_book(seqr.engine());

  int exit_code = 0;
  if (expected_path) {
    const std::optional<std::string> expected = slurp(*expected_path);
    if (!expected) {
      std::cerr << "microsim_replay: cannot open expected event log '" << *expected_path << "'\n";
      return 1;
    }
    if (regenerated.str() == *expected) {
      std::cout << "\n  replay verified: event log reproduced byte-identically (INV-10).\n";
    } else {
      std::cout << "\n  REPLAY MISMATCH: regenerated event log differs from " << *expected_path
                << "\n";
      exit_code = 1;
    }
  }
  return exit_code;
}
