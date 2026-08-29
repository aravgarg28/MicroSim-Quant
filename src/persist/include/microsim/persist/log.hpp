#pragma once

/// \file
/// The on-disk run log: a length-prefixed, CRC-checked record stream, plus the
/// little-endian byte primitives every record is built from (task R1-21, the
/// persistence half deferred from R1-11). Two log kinds share the framing: the
/// **input log** (the sequencer's tee — every inbound message with its logical
/// timestamp, DATA_FLOW.md step 4) and the **event log** (every SequencedEvent
/// the engine emitted). Replaying the input log through a fresh engine must
/// reproduce the event log byte for byte — the on-disk face of INV-10 (R-10.3).
///
/// Records are written field-wise and little-endian (never a struct memcpy), so
/// the bytes carry no padding and are identical run to run and — within the
/// fixed-width integer contract — across machines. Each record is framed as
/// `[u32 length][payload][u32 crc32(payload)]`, so a truncated or corrupted log
/// is detected rather than silently mis-read.

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace microsim::persist {

/// Which stream a log holds. Stored in the header so a reader can reject a log
/// opened for the wrong purpose (an event log fed to the replay input, say).
enum class LogKind : std::uint16_t { Input = 0, Event = 1 };

/// Thrown on a malformed header or a record that fails its length/CRC check.
class LogError : public std::runtime_error {
 public:
  explicit LogError(const std::string& what) : std::runtime_error(what) {}
};

/// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) over the bytes. Used to catch
/// truncation and corruption of a log record.
[[nodiscard]] std::uint32_t crc32(std::string_view bytes) noexcept;

/// Appends little-endian primitives to an in-memory byte buffer.
class ByteWriter {
 public:
  void u8(std::uint8_t v);
  void u16(std::uint16_t v);
  void u32(std::uint32_t v);
  void u64(std::uint64_t v);
  void i64(std::int64_t v);

  [[nodiscard]] const std::string& bytes() const noexcept { return buf_; }

 private:
  std::string buf_;
};

/// Reads little-endian primitives from a byte view, tracking whether every read
/// stayed in bounds. A read past the end yields 0 and latches `ok()` to false,
/// so a caller decodes then checks `ok()` once rather than after every field.
class ByteReader {
 public:
  explicit ByteReader(std::string_view bytes) noexcept : buf_(bytes) {}

  std::uint8_t u8();
  std::uint16_t u16();
  std::uint32_t u32();
  std::uint64_t u64();
  std::int64_t i64();

  [[nodiscard]] bool ok() const noexcept { return ok_; }

  [[nodiscard]] bool at_end() const noexcept { return ok_ && pos_ == buf_.size(); }

 private:
  std::string_view buf_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

/// Writes a framed log to an ostream: a fixed header (magic + version + kind),
/// then each record as `[u32 length][payload][u32 crc32]`.
class LogWriter {
 public:
  /// Writes the header immediately. `out` must be opened binary and outlive the
  /// writer.
  LogWriter(std::ostream& out, LogKind kind);

  /// Frame and append one record.
  void write_record(std::string_view payload);

 private:
  std::ostream& out_;
};

/// Reads a framed log written by LogWriter, validating the header up front and
/// every record's length and CRC as it goes.
class LogReader {
 public:
  /// Reads and validates the header. Throws LogError on a bad magic or an
  /// unsupported version. `in` must be opened binary and outlive the reader.
  explicit LogReader(std::istream& in);

  [[nodiscard]] LogKind kind() const noexcept { return kind_; }

  /// The next record's payload, or std::nullopt at a clean end of stream. Throws
  /// LogError on a truncated record or a CRC mismatch.
  [[nodiscard]] std::optional<std::string> next_record();

 private:
  std::istream& in_;
  LogKind kind_{};
};

}  // namespace microsim::persist
