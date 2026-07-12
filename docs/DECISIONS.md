# MicroSim — Decision Log

Every major decision is recorded here with its alternatives and rationale. Decisions marked
**LOCKED** were approved by the project owner (Arav) or fall under his delegated authority to
Fable ("use your judgment, log it"). Decisions marked **OPEN** still need his approval.

Format: ID · Date · Decision · Alternatives considered · Rationale · Status.

---

## Product decisions

### D1 — Project emphasis: systems-leaning balance (~60% engineering / 40% research)
- **Date:** 2026-07-12
- **Alternatives:** pure systems engineering; research-leaning; even 50/50.
- **Rationale:** Primary targets are new-grad quantitative developer / trading-systems roles.
  Those interviews probe C++, data structures, and performance first, and use research to test
  judgment and honesty. A 60/40 split keeps the engine deep while three rigorous experiments
  demonstrate statistical thinking.
- **Status:** LOCKED (owner choice).

### D2 — Docs must teach the domain; the interview guide is a first-class deliverable
- **Date:** 2026-07-12
- **Rationale:** Owner is new to market microstructure (solid statistics, coursework C++, basic
  Python). He must be able to explain every design decision in interviews, so specification
  documents double as teaching material, and `docs/presentation/INTERVIEW_GUIDE.md` is not an
  afterthought.
- **Status:** LOCKED (owner background, stated in interview).

### D3 — Implementation is delegated to Opus working from these specs
- **Date:** 2026-07-12
- **Rationale:** Owner reviews rather than writes code. The binding constraint is his review
  bandwidth, not build speed. Consequences: implementation tasks must be small (one focused PR),
  acceptance criteria must be machine-verifiable, and no task may require Opus to invent exchange
  rules or architecture.
- **Status:** LOCKED (owner choice).

### D4 — Deliverables: recruiter-grade repo, interview prep, dashboard last; no formal paper
- **Date:** 2026-07-12
- **Alternatives:** academic-style research report; dashboard-first demo.
- **Rationale:** Owner selected recruiter-grade README/repo, interview prep materials, and a web
  dashboard. The dashboard is strictly Release 6 (owner confirmed). Research findings are written
  up rigorously but live in `docs/research/` and the README rather than a standalone paper.
- **Status:** LOCKED (owner choice).

### D5 — MVP simulates one instrument with one order book
- **Date:** 2026-07-12
- **Alternatives:** multi-instrument from day one.
- **Rationale:** Multi-instrument adds breadth but no new conceptual depth, and slows the path to
  a provably correct engine. APIs avoid baking in single-instrument assumptions (instrument IDs
  exist from day one), but only one book is instantiated until Release 5.
- **Status:** LOCKED (owner choice).

### D6 — MVP exchange features: core set + fees/rebates + basic risk limits
- **Date:** 2026-07-12
- **Alternatives considered for MVP inclusion:** self-trade prevention; opening/closing auctions;
  trading halts; position limits beyond basic caps.
- **Rationale:** Core (limit/market/cancel/modify, partial fills, price-time priority, tick and
  lot sizes) is non-negotiable for credibility. Maker-taker fees are cheap to implement and
  without them market-making P&L is misleading. Basic pre-trade risk checks (max position, max
  order size, max open orders) are small and high-credibility. Self-trade prevention moves to
  Release 2 (realistic but not needed for the first experiments). Auctions and halts are deferred
  indefinitely: large spec surface, no research question needs them.
- **Status:** LOCKED (delegated to Fable, owner informed).

### D7 — MVP order flow: scripted scenarios + seeded Poisson arrivals + noise-trader agents
- **Date:** 2026-07-12
- **Alternatives:** state-dependent arrival rates in MVP; Hawkes processes in MVP; historical
  replay in MVP.
- **Rationale:** Poisson flow is simple to explain, calibrate honestly, and validate. Scripted
  deterministic scenarios drive tests. State-dependent flow (rates reacting to spread/imbalance)
  is Release 5; Hawkes is a Release 5 stretch goal; historical replay is out of MVP scope
  entirely (data licensing and format work would crowd out the engine).
- **Status:** LOCKED (delegated to Fable).

### D8 — MVP strategies: fixed-spread quoter + inventory-skewed quoter
- **Date:** 2026-07-12
- **Alternatives:** add order-book-imbalance quoting to MVP; include Avellaneda–Stoikov in MVP.
- **Rationale:** The pair forms a clean A/B comparison that directly answers research question
  RQ2 and is fully explainable by the owner. Volatility-adaptive, imbalance-based, and
  A-S-inspired quoting arrive in Release 3 once the engine is battle-tested.
- **Status:** LOCKED (delegated to Fable).

### D9 — Three research questions; imbalance-prediction question dropped
- **Date:** 2026-07-12
- **Decision:** The project answers exactly three questions rigorously:
  1. **RQ1 (latency):** How does added market-data and order-entry latency degrade a market
     maker's fill quality, adverse selection, and P&L?
  2. **RQ2 (inventory):** When does inventory-skewed quoting outperform fixed-spread quoting?
  3. **RQ3 (queue):** How does queue position affect fill probability and time-to-fill?
- **Alternatives:** "Does order-book imbalance predict short-horizon mid-price movement?"
- **Rationale:** In a simulated market the imbalance question partly measures the simulator's own
  flow model — a predictability result would be circular, and a sharp interviewer would attack it.
  RQ3 is purely mechanical (answerable with high rigor, no contested modeling assumptions); RQ1
  showcases the latency laboratory; RQ2 is a clean strategy A/B.
- **Status:** LOCKED (owner selected the three recommended; drop rationale accepted).

### D10 — Benchmarks: Apple Silicon Mac authoritative; Linux CI for sanitizers and smoke tests
- **Date:** 2026-07-12
- **Alternatives:** Linux-in-Docker benchmarks on the Mac; dedicated Linux hardware.
- **Rationale:** Development machine is macOS/ARM, where `perf`, Valgrind, and MemorySanitizer
  are unavailable. Virtualized timing inside Docker on macOS is noisy and its counters
  unreliable — numbers would look more rigorous while being less honest. So: authoritative
  numbers are measured natively on the Mac with hardware fully disclosed; GitHub Actions Linux
  runners run the sanitizer matrix and benchmark *smoke* tests (regression detection only, never
  headline numbers, and labeled as such).
- **Status:** LOCKED (owner choice).

### D11 — No hard deadline; release gates define "presentable"
- **Date:** 2026-07-12
- **Rationale:** Owner has no fixed recruiting date. Release 3 (engine + benchmarks + first
  research results) is the first resume-worthy state.
- **Status:** LOCKED (owner choice).

### D12 — Process: checkpoint approvals per phase group
- **Date:** 2026-07-12
- **Alternatives:** full autonomy with decision log; approvals only for the five biggest docs.
- **Rationale:** Owner wants to learn the domain as the specs are written. Spec work proceeds in
  eight checkpoint groups (CP1 product → CP8 implementation tasks); each ends with a committed
  doc batch, a plain-English summary, and owner approval before the next begins.
- **Status:** LOCKED (owner choice).

### D13 — Dashboard is strictly last (Release 6)
- **Date:** 2026-07-12
- **Rationale:** Owner confirmed UI must not delay engine, tests, benchmarks, or research. A
  cheap static order-book replay visualization may appear earlier as a demo aid, but interactive
  dashboard work is gated behind Release 5 completion.
- **Status:** LOCKED (owner choice).

### D15 — Exchange-rule judgment calls (CP2)
- **Date:** 2026-07-12
- **Decisions made while writing `docs/domain/EXCHANGE_RULES.md`** (delegated authority, most
  significant listed):
  1. **Fees are flat per-lot integers** (futures-style), not basis points of notional
     (equities-style). Rationale: exact integer arithmetic, zero rounding rules, INV-11(b)
     reconciliation stays exact. Bps fees documented as an extension.
  2. **Market-order remainder is canceled (`NO_LIQUIDITY`), never rests**; a market order into
     an empty opposite side is accepted then canceled with zero fills (uniform event flow,
     deterministic, testable).
  3. **Modify semantics:** price change or quantity increase loses time priority; quantity
     decrease keeps it; modify-to-≤-filled cancels remainder. Mirrors common exchange practice
     (e.g. CME-style cancel/replace) and makes queue-priority rules testable.
  4. **Self-trades are permitted in Release 1**; Release 2 adds per-participant STP with
     `CANCEL_NEWEST` default for strategies. Keeps R1 matching minimal.
  5. **Risk position check uses worst-case exposure** (position + same-side open + new order),
     not fill-optimistic. Conservative, deterministic, standard practice.
  6. **No IOC/FOK in MVP** — DAY-resting limits and immediate markets only.
- **Status:** LOCKED (delegated to Fable; owner may reopen any item at CP2 review).

---

## Technology-stack decisions

### S1 — C++20, not C++23
- **Alternatives:** C++23; C++17.
- **Rationale:** AppleClang and GitHub Actions toolchains support C++20 solidly; C++23 support is
  still uneven and its gains for this project (e.g. `std::expected`, `std::print`) are marginal.
  C++17 would forfeit concepts, `std::span`, designated initializers, and `<bit>`, all of which
  materially improve the engine's interfaces. Revisit if toolchains move.
- **Status:** LOCKED.

### S2 — Dependencies via CMake FetchContent
- **Alternatives:** vcpkg; Conan; git submodules; system packages.
- **Rationale:** Dependency count is tiny (GoogleTest, Google Benchmark, pybind11). FetchContent
  pins exact versions in-tree, needs zero external infrastructure, is free, and works identically
  on macOS and Linux CI. Package managers earn their complexity only with larger dependency sets.
- **Status:** LOCKED.

### S3 — Property-based testing via custom deterministic generators, not RapidCheck
- **Alternatives:** RapidCheck; fuzztest (Google); no property testing.
- **Rationale:** RapidCheck is lightly maintained; fuzztest's CMake integration is Linux-centric.
  A small in-repo generator framework (seeded scenario generation + invariant checkers on the
  GoogleTest harness) is ~equivalent effort, fully deterministic, and a better interview story
  because the owner can explain every line. libFuzzer (S4) covers coverage-guided exploration.
- **Status:** LOCKED.

### S4 — Fuzzing via libFuzzer only; AFL++ dropped
- **Rationale:** libFuzzer ships with clang on both macOS and Linux CI and integrates with
  sanitizers. AFL++ adds setup cost without finding a different class of bug at this scale.
- **Status:** LOCKED.

### S5 — Sanitizers: ASan + UBSan everywhere, TSan when threading arrives; MSan excluded
- **Rationale:** MemorySanitizer requires a fully MSan-instrumented libc++ (impractical to
  maintain, Linux-only). ASan+UBSan run on every CI build; TSan gets its own CI job starting with
  the first multithreaded release. Valgrind/heaptrack run on Linux CI only; on macOS, Instruments
  and allocation-counting hooks fill the gap.
- **Status:** LOCKED.

### S6 — Python bindings via pybind11
- **Alternatives:** nanobind; ctypes/cffi over a C API; SWIG.
- **Rationale:** pybind11 is the industry-recognizable choice, extensively documented, and fast
  enough given the binding boundary is coarse (run simulation → retrieve results), not per-event.
  nanobind's compile-time and call-overhead advantages don't matter at this boundary granularity.
- **Status:** LOCKED.

### S7 — Experiment storage: Parquet files + DuckDB, accessed from Python
- **Rationale:** Free, file-based, zero services, queryable, and standard in quant research.
  DuckDB is a Python-side dependency only — the C++ engine writes simple binary/CSV event logs
  and result summaries; Python converts to Parquet. Keeps the C++ dependency set minimal.
- **Status:** LOCKED.

### S8 — Docker optional (dev-container for Linux parity), not a required path
- **Rationale:** Everything builds natively on macOS and in CI. A devcontainer file is provided
  for contributors and for local Linux-tool access, but no workflow requires it.
- **Status:** LOCKED.

---

## Open decisions

### O1 — Repository visibility timing
- Repo is currently **private**. It must become public before recruiting use. Making it public
  is the owner's action (visibility changes are his to perform). Suggested timing: after Release
  1 lands with CI green, so first impressions include working code.
- **Status:** OPEN — owner decision.

### ~~O2~~ D14 — License: MIT
- **Date:** 2026-07-12
- **Alternatives:** Apache-2.0 (patent grant, longer), BSD-3.
- **Rationale:** Maximally permissive, recruiter-familiar, no obligations. Owner approved at
  CP1 review; `LICENSE` added at repo root.
- **Status:** LOCKED (owner choice).
