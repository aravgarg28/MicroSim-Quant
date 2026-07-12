# Numeric Representation

How MicroSim represents money, prices, quantities, time, and statistics — and why binary
floating point is banned from the authoritative path.

## The one-sentence policy

**Everything the exchange asserts to be true (prices, quantities, cash, fees, P&L) is exact
64-bit integer arithmetic; everything the researcher estimates (mids, volatilities,
probabilities, statistics) is `double`, computed outside the engine.**

## Why not floating point for money

`0.1 + 0.2 ≠ 0.3` in binary floating point. An exchange that accumulates fees as doubles will
eventually fail reconciliation (INV-11b) by a few ulps, and then the question "is this a
rounding artifact or a real accounting bug?" poisons every test. Integer arithmetic makes
INV-11 *exact*: any imbalance, even one minor unit, is a bug. This is also how real venues and
trading systems represent prices (ticks / scaled decimals), so it is the correct habit to
demonstrate.

## Representations compared

| Option | Exact? | Speed | Notes |
|---|---|---|---|
| **int64 tick/lot/minor-unit counts** | yes | fastest (native) | needs explicit unit discipline → strong types |
| Fixed-point via scaled int64 with runtime scale | yes | fast | flexible but every operation carries scale-matching logic; overkill when instruments declare tick/lot up front |
| Decimal floating point (e.g. IEEE 754-2008 decimal64) | yes for ≤16 digits | slow (software on ARM/x86) | solves a generality problem MicroSim doesn't have |
| double | no | fast | fine for analysis; banned for authority |

**Chosen:** int64 counts with compile-time strong types.

## The strong types (`core`)

- `Price` — int64 **ticks** (not minor units): all book indexing and comparisons are on ticks.
- `Qty` — int64 **lots**.
- `Cash` — int64 **minor units** (e.g. cents).
- `TickSize`, `LotSize` — int64 minor units / units per lot, per instrument (R-1.1).
- `Notional(price, qty, instr) → Cash = price.ticks × instr.tick_size × qty.lots × instr.lot_size`.
- Type rules (compile-time): `Price ± int → Price`; `Price − Price → int ticks`;
  `Qty ± Qty → Qty`; `Cash ± Cash → Cash`; `Price × Qty` is ill-formed without the instrument
  (must go through `Notional`); no implicit conversions to/from raw integers; comparisons only
  within a type. `static_assert` tests pin these.

## Overflow analysis (documented bound, enforced by config validation)

Worst-case notional per trade: `max_price_ticks × tick_size × max_order_qty_lots × lot_size`.
Config validation requires this (and its session-aggregate bound: × max fills, a config
parameter with a generous default) to fit int64 with ≥ 100× headroom; violating configs are
rejected at construction. Debug/property builds additionally use a checked-arithmetic wrapper
(UBSan's signed-overflow trap covers release-adjacent builds). There is no silent wraparound
path.

## Conversion and rounding rules (all at the boundary, none inside)

1. **Config/Python → engine:** decimal strings or (value, scale) pairs → exact integers;
  non-representable values (price not a multiple of tick) are **rejected** (`INVALID_TICK`),
  never rounded. Binary `float` inputs for prices are rejected outright in the Python API
  (must pass strings/ints/Decimal) — silently accepting 10.03f would smuggle FP error in.
2. **Engine → research (doubles):** exact int64 → double conversion is lossless for
  |value| < 2⁵³, which config bounds guarantee; conversion helpers assert it anyway.
3. **The one authoritative rounding rule in the system:** session-end mark price mid
  (R-12.4). To keep Cash exact, unrealized P&L marks use `2 × mid` internally:
  `uPnL = position_lots × (2·mark_numerator − 2·entry) × units / 2` computed to preserve
  integrality — full formula and worked example in `POSITION_AND_PNL.md`; the mid itself, when
  *reported*, is a double (analysis value). No other rounding exists (fees are flat per-lot by
  D15 exactly to make this true).

## Time

- `SimTime` — int64 **nanoseconds** of logical simulation time, starting at 0. Strong type;
  `Duration` (int64 ns) for differences. No floating-point time anywhere (accumulating double
  seconds drifts).
- Wall-clock time appears only in benchmark harnesses (`steady_clock`) and run metadata
  (ISO-8601 start time for provenance).

## Probabilities, volatility, statistics

`double`, Python/metrics side. Rules of hygiene: Welford/Kahan-style streaming moments in the
C++ metrics collectors (numerically stable single pass); Python analysis uses
NumPy/SciPy/statsmodels defaults (float64); no float32 anywhere; confidence intervals and
tests report to sensible precision (no 10-significant-digit theater).

## Print/display

Reporting helpers format `Price`/`Cash` as decimal strings via integer division/modulo on
tick/lot/minor-unit scales — never via double formatting. `$10.03` printed from ticks is
exact; `printf("%f")` on money does not exist in this codebase.
