#include "microsim/persist/log.hpp"

#include <array>
#include <cstddef>

namespace microsim::persist {

namespace {

// 8-byte magic + u16 version + u16 kind. The trailing 0x1A ("end of file" in
// old text terminals) makes a log opened as text stop early, a mild guard
// against treating a binary log as text.
constexpr std::array<char, 8> kMagic = {'M', 'S', 'I', 'M', 'L', 'O', 'G', '\x1a'};
constexpr std::uint16_t kVersion = 1;

// One-time CRC-32 table (reflected, poly 0xEDB88320).
const std::array<std::uint32_t, 256>& crc_table() {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
      }
      t[n] = c;
    }
    return t;
  }();
  return table;
}

}  // namespace

std::uint32_t crc32(std::string_view bytes) noexcept {
  const std::array<std::uint32_t, 256>& table = crc_table();
  std::uint32_t c = 0xFFFFFFFFU;
  for (const char ch : bytes) {
    const std::uint8_t byte = static_cast<std::uint8_t>(ch);
    c = table[(c ^ byte) & 0xFFU] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFFU;
}

// ----- ByteWriter -------------------------------------------------------------

void ByteWriter::u8(std::uint8_t v) {
  buf_.push_back(static_cast<char>(v));
}

void ByteWriter::u16(std::uint16_t v) {
  u8(static_cast<std::uint8_t>(v));
  u8(static_cast<std::uint8_t>(v >> 8));
}

void ByteWriter::u32(std::uint32_t v) {
  u16(static_cast<std::uint16_t>(v));
  u16(static_cast<std::uint16_t>(v >> 16));
}

void ByteWriter::u64(std::uint64_t v) {
  u32(static_cast<std::uint32_t>(v));
  u32(static_cast<std::uint32_t>(v >> 32));
}

void ByteWriter::i64(std::int64_t v) {
  u64(static_cast<std::uint64_t>(v));
}

// ----- ByteReader -------------------------------------------------------------

std::uint8_t ByteReader::u8() {
  if (pos_ + 1 > buf_.size()) {
    ok_ = false;
    return 0;
  }
  return static_cast<std::uint8_t>(buf_[pos_++]);
}

std::uint16_t ByteReader::u16() {
  const std::uint16_t lo = u8();
  const std::uint16_t hi = u8();
  return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint32_t ByteReader::u32() {
  const std::uint32_t lo = u16();
  const std::uint32_t hi = u16();
  return lo | (hi << 16);
}

std::uint64_t ByteReader::u64() {
  const std::uint64_t lo = u32();
  const std::uint64_t hi = u32();
  return lo | (hi << 32);
}

std::int64_t ByteReader::i64() {
  return static_cast<std::int64_t>(u64());
}

// ----- LogWriter --------------------------------------------------------------

LogWriter::LogWriter(std::ostream& out, LogKind kind) : out_(out) {
  ByteWriter header;
  for (const char c : kMagic) {
    header.u8(static_cast<std::uint8_t>(c));
  }
  header.u16(kVersion);
  header.u16(static_cast<std::uint16_t>(kind));
  out_.write(header.bytes().data(), static_cast<std::streamsize>(header.bytes().size()));
}

void LogWriter::write_record(std::string_view payload) {
  ByteWriter frame;
  frame.u32(static_cast<std::uint32_t>(payload.size()));
  out_.write(frame.bytes().data(), static_cast<std::streamsize>(frame.bytes().size()));
  out_.write(payload.data(), static_cast<std::streamsize>(payload.size()));

  ByteWriter tail;
  tail.u32(crc32(payload));
  out_.write(tail.bytes().data(), static_cast<std::streamsize>(tail.bytes().size()));
}

// ----- LogReader --------------------------------------------------------------

namespace {

// Read exactly n bytes, or fewer at end of stream; returns how many were read.
std::size_t read_bytes(std::istream& in, char* dst, std::size_t n) {
  in.read(dst, static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount());
}

}  // namespace

LogReader::LogReader(std::istream& in) : in_(in) {
  std::array<char, 12> header{};
  if (read_bytes(in_, header.data(), header.size()) != header.size()) {
    throw LogError("log header is truncated");
  }
  for (std::size_t i = 0; i < kMagic.size(); ++i) {
    if (header[i] != kMagic[i]) {
      throw LogError("log magic mismatch (not a MicroSim log)");
    }
  }
  ByteReader r(std::string_view(header.data() + kMagic.size(), header.size() - kMagic.size()));
  const std::uint16_t version = r.u16();
  if (version != kVersion) {
    throw LogError("unsupported log version " + std::to_string(version));
  }
  kind_ = static_cast<LogKind>(r.u16());
}

std::optional<std::string> LogReader::next_record() {
  std::array<char, 4> len_bytes{};
  const std::size_t got = read_bytes(in_, len_bytes.data(), len_bytes.size());
  if (got == 0) {
    return std::nullopt;  // clean end of stream, on a record boundary
  }
  if (got != len_bytes.size()) {
    throw LogError("log truncated in the record length");
  }
  ByteReader len_reader(std::string_view(len_bytes.data(), len_bytes.size()));
  const std::uint32_t len = len_reader.u32();

  std::string payload(len, '\0');
  if (len > 0 && read_bytes(in_, payload.data(), len) != len) {
    throw LogError("log truncated in a record payload");
  }

  std::array<char, 4> crc_bytes{};
  if (read_bytes(in_, crc_bytes.data(), crc_bytes.size()) != crc_bytes.size()) {
    throw LogError("log truncated in a record checksum");
  }
  ByteReader crc_reader(std::string_view(crc_bytes.data(), crc_bytes.size()));
  const std::uint32_t stored = crc_reader.u32();
  if (stored != crc32(payload)) {
    throw LogError("log record failed its CRC check (corrupt log)");
  }
  return payload;
}

}  // namespace microsim::persist
