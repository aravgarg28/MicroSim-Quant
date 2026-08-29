/// \file
/// `microsim_run` (v1, task R1-21) — drive the scripted demo scenario through
/// the sequencer and print the resulting trades and final book in human-readable
/// dollars. Given an output directory, it also tees two logs: `input.log` (the
/// sequenced inbound messages with their logical timestamps) and `events.log`
/// (every SequencedEvent the engine emitted). `microsim_replay` reads `input.log`
/// back and must reproduce `events.log` byte for byte — the on-disk face of
/// INV-10. Determinism: no clock reads, no randomness; the logical clock steps
/// one nanosecond per message, so the output is a pure function of the script.

#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
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

int main(int argc, char** argv) {
  const mc::InstrumentConfig instr = md::demo_instrument();

  // Optional output directory: when given, tee the input and event logs into it.
  std::optional<std::filesystem::path> out_dir;
  if (argc > 1) {
    out_dir = std::filesystem::path(argv[1]);
    std::error_code ec;
    std::filesystem::create_directories(*out_dir, ec);
    if (ec) {
      std::cerr << "microsim_run: cannot create output directory '" << out_dir->string()
                << "': " << ec.message() << "\n";
      return 1;
    }
  }

  std::ofstream input_file;
  std::ofstream event_file;
  std::optional<mp::LogWriter> input_log;
  std::optional<mp::LogWriter> event_log;
  if (out_dir) {
    input_file.open(*out_dir / "input.log", std::ios::binary | std::ios::trunc);
    event_file.open(*out_dir / "events.log", std::ios::binary | std::ios::trunc);
    if (!input_file || !event_file) {
      std::cerr << "microsim_run: cannot open log files in '" << out_dir->string() << "'\n";
      return 1;
    }
    input_log.emplace(input_file, mp::LogKind::Input);
    event_log.emplace(event_file, mp::LogKind::Event);
  }

  me::Sequencer<microsim::book::FastBook> seqr{md::demo_venue(), instr};

  std::cout << "MicroSim — deterministic exchange simulator\n";
  std::cout << "Instrument " << instr.symbol << "   tick $0.01   band "
            << md::usd(instr.min_price, instr) << "-" << md::usd(instr.max_price, instr)
            << "   fees: taker " << md::usd(instr.fees.taker_fee_per_lot) << "/lot, maker rebate "
            << md::usd(instr.fees.maker_rebate_per_lot) << "/lot\n\n";

  md::Tally tally;
  mc::SimTime clock = mc::SimTime::zero();
  for (const mc::Inbound& msg : md::demo_script()) {
    md::print_inbound(msg, instr);

    if (input_log) {
      input_log->write_record(mp::encode_input(clock, msg));
    }
    const std::vector<mc::SequencedEvent> stamped = seqr.submit(msg, clock);
    clock += mc::Duration{1};

    std::vector<mc::Outbound> payloads;
    payloads.reserve(stamped.size());
    for (const mc::SequencedEvent& ev : stamped) {
      if (event_log) {
        event_log->write_record(mp::encode_event(ev));
      }
      payloads.push_back(ev.event);
    }
    md::print_events(payloads, instr);
    md::tally(payloads, tally);
  }

  md::print_book(seqr.engine());
  std::cout << "\n  " << tally.trades << " trades, " << tally.lots << " lots matched, "
            << seqr.engine().registry().size() << " orders processed.\n";
  if (out_dir) {
    std::cout << "  logs: " << (*out_dir / "input.log").string() << ", "
              << (*out_dir / "events.log").string() << "\n";
  }
  return 0;
}
