#pragma once

/// \file
/// Strong value types for the whole engine (task R1-03).
///
/// Every authoritative quantity in MicroSim is an exact 64-bit integer wearing a
/// type that makes unit mistakes a compile error. This is the C++ expression of
/// docs/numerics/NUMERIC_REPRESENTATION.md:
///
///   * Price  — int64 *ticks*        (book indexing and comparison are on ticks)
///   * Qty    — int64 *lots*
///   * Cash   — int64 *minor units*  (e.g. cents)
///   * SimTime/Duration — int64 *nanoseconds* of logical time
///   * identifiers (OrderId, Seq, ...) — opaque integer handles
///
/// The operator sets below are deliberately narrow. Allowed operations compile;
/// meaningless ones (Price + Qty, Price * Qty, implicit int <-> Price) do not.
/// The rules are pinned by static_assert in tests/unit/core and by
/// compile-fail tests. Nothing here allocates, throws, or reads a clock.

#include <cstdint>
#include <format>
#include <functional>
#include <ostream>

namespace microsim::core {

// =============================================================================
// Dimensioned value types
// =============================================================================

/// A price, stored as a signed count of ticks (docs/numerics R-1.2).
///
/// Allowed: Price +/- int64 (tick offset) -> Price; Price - Price -> int64
/// (tick difference); ordering and equality. Banned by omission: Price + Price
/// (adding two prices is meaningless), Price * anything (notional needs the
/// instrument's tick size — see Notional in a later task).
class Price {
 public:
  constexpr Price() noexcept = default;

  constexpr explicit Price(std::int64_t ticks) noexcept : ticks_(ticks) {}

  [[nodiscard]] constexpr std::int64_t ticks() const noexcept { return ticks_; }

  friend constexpr Price operator+(Price p, std::int64_t d) noexcept { return Price{p.ticks_ + d}; }

  friend constexpr Price operator+(std::int64_t d, Price p) noexcept { return Price{d + p.ticks_}; }

  friend constexpr Price operator-(Price p, std::int64_t d) noexcept { return Price{p.ticks_ - d}; }

  /// Difference of two prices is a tick count, not a price.
  friend constexpr std::int64_t operator-(Price a, Price b) noexcept { return a.ticks_ - b.ticks_; }

  constexpr Price& operator+=(std::int64_t d) noexcept {
    ticks_ += d;
    return *this;
  }

  constexpr Price& operator-=(std::int64_t d) noexcept {
    ticks_ -= d;
    return *this;
  }

  friend constexpr bool operator==(Price, Price) noexcept = default;
  friend constexpr auto operator<=>(Price, Price) noexcept = default;

 private:
  std::int64_t ticks_{0};
};

/// A quantity, stored as a signed count of lots.
///
/// Allowed: Qty +/- Qty -> Qty; ordering and equality (so std::min/max work for
/// the matching loop). Banned by omission: Qty * Qty, Qty + Price, Qty * scalar
/// (per-lot fee arithmetic that produces Cash lives with the fee model).
class Qty {
 public:
  constexpr Qty() noexcept = default;

  constexpr explicit Qty(std::int64_t lots) noexcept : lots_(lots) {}

  [[nodiscard]] constexpr std::int64_t lots() const noexcept { return lots_; }

  friend constexpr Qty operator+(Qty a, Qty b) noexcept { return Qty{a.lots_ + b.lots_}; }

  friend constexpr Qty operator-(Qty a, Qty b) noexcept { return Qty{a.lots_ - b.lots_}; }

  constexpr Qty& operator+=(Qty o) noexcept {
    lots_ += o.lots_;
    return *this;
  }

  constexpr Qty& operator-=(Qty o) noexcept {
    lots_ -= o.lots_;
    return *this;
  }

  friend constexpr bool operator==(Qty, Qty) noexcept = default;
  friend constexpr auto operator<=>(Qty, Qty) noexcept = default;

 private:
  std::int64_t lots_{0};
};

/// A cash amount, stored as signed minor units (docs/numerics R-1.3).
///
/// Allowed: Cash +/- Cash -> Cash; unary minus (P&L sign); ordering and
/// equality. String/currency formatting needs a scale and arrives with the
/// boundary-conversion task (R1-06); here Cash prints as raw minor units.
class Cash {
 public:
  constexpr Cash() noexcept = default;

  constexpr explicit Cash(std::int64_t minor) noexcept : minor_(minor) {}

  [[nodiscard]] constexpr std::int64_t minor() const noexcept { return minor_; }

  friend constexpr Cash operator+(Cash a, Cash b) noexcept { return Cash{a.minor_ + b.minor_}; }

  friend constexpr Cash operator-(Cash a, Cash b) noexcept { return Cash{a.minor_ - b.minor_}; }

  friend constexpr Cash operator-(Cash a) noexcept { return Cash{-a.minor_}; }

  constexpr Cash& operator+=(Cash o) noexcept {
    minor_ += o.minor_;
    return *this;
  }

  constexpr Cash& operator-=(Cash o) noexcept {
    minor_ -= o.minor_;
    return *this;
  }

  friend constexpr bool operator==(Cash, Cash) noexcept = default;
  friend constexpr auto operator<=>(Cash, Cash) noexcept = default;

 private:
  std::int64_t minor_{0};
};

class SimTime;  // forward

/// A span of logical time, in nanoseconds (docs/simulation/SIMULATION_CLOCK.md).
class Duration {
 public:
  constexpr Duration() noexcept = default;

  constexpr explicit Duration(std::int64_t ns) noexcept : ns_(ns) {}

  [[nodiscard]] constexpr std::int64_t ns() const noexcept { return ns_; }

  friend constexpr Duration operator+(Duration a, Duration b) noexcept {
    return Duration{a.ns_ + b.ns_};
  }

  friend constexpr Duration operator-(Duration a, Duration b) noexcept {
    return Duration{a.ns_ - b.ns_};
  }

  friend constexpr Duration operator-(Duration a) noexcept { return Duration{-a.ns_}; }

  // Scaling a delay by an integer factor is meaningful (e.g. base latency * n).
  friend constexpr Duration operator*(Duration a, std::int64_t k) noexcept {
    return Duration{a.ns_ * k};
  }

  friend constexpr Duration operator*(std::int64_t k, Duration a) noexcept {
    return Duration{k * a.ns_};
  }

  constexpr Duration& operator+=(Duration o) noexcept {
    ns_ += o.ns_;
    return *this;
  }

  constexpr Duration& operator-=(Duration o) noexcept {
    ns_ -= o.ns_;
    return *this;
  }

  friend constexpr bool operator==(Duration, Duration) noexcept = default;
  friend constexpr auto operator<=>(Duration, Duration) noexcept = default;

 private:
  std::int64_t ns_{0};
};

/// A point in logical simulation time, nanoseconds from 0.
///
/// Allowed: SimTime +/- Duration -> SimTime; SimTime - SimTime -> Duration;
/// ordering and equality (this is the event-queue key). Banned by omission:
/// SimTime + SimTime (adding two instants is meaningless).
class SimTime {
 public:
  constexpr SimTime() noexcept = default;

  constexpr explicit SimTime(std::int64_t ns) noexcept : ns_(ns) {}

  [[nodiscard]] constexpr std::int64_t ns() const noexcept { return ns_; }

  [[nodiscard]] static constexpr SimTime zero() noexcept { return SimTime{0}; }

  friend constexpr SimTime operator+(SimTime t, Duration d) noexcept {
    return SimTime{t.ns_ + d.ns()};
  }

  friend constexpr SimTime operator+(Duration d, SimTime t) noexcept {
    return SimTime{d.ns() + t.ns_};
  }

  friend constexpr SimTime operator-(SimTime t, Duration d) noexcept {
    return SimTime{t.ns_ - d.ns()};
  }

  /// Difference of two instants is a Duration, not an instant.
  friend constexpr Duration operator-(SimTime a, SimTime b) noexcept {
    return Duration{a.ns_ - b.ns_};
  }

  constexpr SimTime& operator+=(Duration d) noexcept {
    ns_ += d.ns();
    return *this;
  }

  constexpr SimTime& operator-=(Duration d) noexcept {
    ns_ -= d.ns();
    return *this;
  }

  friend constexpr bool operator==(SimTime, SimTime) noexcept = default;
  friend constexpr auto operator<=>(SimTime, SimTime) noexcept = default;

 private:
  std::int64_t ns_{0};
};

// =============================================================================
// Side
// =============================================================================

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

/// +1 for Buy, -1 for Sell — the sign a fill applies to a position.
[[nodiscard]] constexpr std::int64_t sign_of(Side s) noexcept {
  return s == Side::Buy ? 1 : -1;
}

[[nodiscard]] const char* to_cstr(Side s) noexcept;

// =============================================================================
// Identifiers
// =============================================================================
//
// Opaque integer handles. They compare and hash (so they can key maps and be
// ordered for the monotonicity invariants, INV-9) but carry no arithmetic:
// there is no such thing as OrderId + OrderId. The `Sequential` flag adds
// next()/first() for the ids an authoritative counter hands out in increasing
// order (order ids, trade ids, sequence numbers); client-chosen or externally
// assigned ids (client order id, participant, instrument) omit it.

namespace detail {

template <class Tag, class Underlying, bool Sequential>
class Id {
 public:
  using value_type = Underlying;

  constexpr Id() noexcept = default;

  constexpr explicit Id(Underlying v) noexcept : v_(v) {}

  [[nodiscard]] constexpr Underlying value() const noexcept { return v_; }

  [[nodiscard]] static constexpr Id first() noexcept
    requires Sequential
  {
    return Id{Underlying{1}};
  }

  [[nodiscard]] constexpr Id next() const noexcept
    requires Sequential
  {
    return Id{static_cast<Underlying>(v_ + 1)};
  }

  friend constexpr bool operator==(Id, Id) noexcept = default;
  friend constexpr auto operator<=>(Id, Id) noexcept = default;

 private:
  Underlying v_{0};
};

}  // namespace detail

// Tag types double as the display name used by the formatter/printer.
struct OrderIdTag {
  static constexpr const char* name = "order";
};

struct TradeIdTag {
  static constexpr const char* name = "trade";
};

struct SeqTag {
  static constexpr const char* name = "seq";
};

struct ClientOrderIdTag {
  static constexpr const char* name = "clord";
};

struct ParticipantIdTag {
  static constexpr const char* name = "party";
};

struct InstrumentIdTag {
  static constexpr const char* name = "instr";
};

using OrderId = detail::Id<OrderIdTag, std::uint64_t, /*Sequential=*/true>;
using TradeId = detail::Id<TradeIdTag, std::uint64_t, /*Sequential=*/true>;
using Seq = detail::Id<SeqTag, std::uint64_t, /*Sequential=*/true>;
using ClientOrderId = detail::Id<ClientOrderIdTag, std::uint64_t, /*Sequential=*/false>;
using ParticipantId = detail::Id<ParticipantIdTag, std::uint32_t, /*Sequential=*/false>;
using InstrumentId = detail::Id<InstrumentIdTag, std::uint32_t, /*Sequential=*/false>;

// =============================================================================
// Debug printing (ostream + std::format)
// =============================================================================
//
// Formatting uses integer scale math only, at the level available here: a Price
// prints its raw tick count, Cash its raw minor units. Human-readable currency
// (which needs the instrument's tick size) is the boundary layer's job (R1-06).

std::ostream& operator<<(std::ostream& os, Price p);
std::ostream& operator<<(std::ostream& os, Qty q);
std::ostream& operator<<(std::ostream& os, Cash c);
std::ostream& operator<<(std::ostream& os, SimTime t);
std::ostream& operator<<(std::ostream& os, Duration d);
std::ostream& operator<<(std::ostream& os, Side s);

template <class Tag, class U, bool S>
std::ostream& operator<<(std::ostream& os, detail::Id<Tag, U, S> id) {
  return os << Tag::name << '#' << id.value();
}

}  // namespace microsim::core

// ----- std::format support -----------------------------------------------------
//
// These are debug formatters: they accept no format spec and write via
// std::format_to on built-in types only. This is deliberately the most portable
// form — inheriting from std::formatter<int64_t> and re-entering its format()
// is rejected by older libc++ (e.g. Xcode 15.4 on the CI runner), which is the
// compatibility floor. Keep them spec-less and primitive-only.

namespace microsim::core::detail {

// Minimal spec-less parse shared by every core formatter below.
struct NoSpecParse {
  constexpr auto parse(std::format_parse_context& ctx) const { return ctx.begin(); }
};

}  // namespace microsim::core::detail

template <>
struct std::formatter<microsim::core::Price> : microsim::core::detail::NoSpecParse {
  auto format(microsim::core::Price p, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}t", p.ticks());
  }
};

template <>
struct std::formatter<microsim::core::Qty> : microsim::core::detail::NoSpecParse {
  auto format(microsim::core::Qty q, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}lot", q.lots());
  }
};

template <>
struct std::formatter<microsim::core::Cash> : microsim::core::detail::NoSpecParse {
  auto format(microsim::core::Cash c, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}mu", c.minor());
  }
};

template <>
struct std::formatter<microsim::core::SimTime> : microsim::core::detail::NoSpecParse {
  auto format(microsim::core::SimTime t, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}ns", t.ns());
  }
};

template <>
struct std::formatter<microsim::core::Duration> : microsim::core::detail::NoSpecParse {
  auto format(microsim::core::Duration d, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}ns", d.ns());
  }
};

template <class Tag, class U, bool S>
struct std::formatter<microsim::core::detail::Id<Tag, U, S>> : microsim::core::detail::NoSpecParse {
  auto format(microsim::core::detail::Id<Tag, U, S> id, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}#{}", Tag::name, id.value());
  }
};

// ----- hashing (ids key unordered containers, e.g. the order registry) --------

template <class Tag, class U, bool S>
struct std::hash<microsim::core::detail::Id<Tag, U, S>> {
  std::size_t operator()(microsim::core::detail::Id<Tag, U, S> id) const noexcept {
    return std::hash<U>{}(id.value());
  }
};
