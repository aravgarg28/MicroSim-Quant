# Demo Scripts

Three rehearsed lengths. Each beat lists: what you show, what you say (spine), and the
fallback if the visual isn't available (pre-R6 = notebook/terminal versions — every demo
must work without the dashboard).

## 3-minute demo (recruiter / non-specialist)

1. **(30s) The pitch** — README top: one-liner, architecture diagram. "A simulated exchange
   I can rerun perfectly from a seed, and a lab for market-making questions."
2. **(60s) The moving book** — dashboard book-replay (fallback: terminal replay printing the
   ladder). Point at: spread forming, a market order sweeping two levels, the tape printing.
   "Every one of these events is deterministic — same seed, same everything, so any bug or
   result reproduces exactly."
3. **(60s) The money screen** — latency A/B (fallback: RQ1 notebook figure): same market,
   two market makers differing only in latency. "Slow one's quotes are stale; watch it get
   picked off — here's the P&L divergence, and here's the confidence interval across 200
   reruns."
4. **(30s) The close** — README benchmark table + findings sentences. "All measured, all
   reproducible from a clean clone — the repo shows how, not just what."

## 7-minute demo (engineer screen)

Beats 1–4 above compressed to 4 min, then:
5. **(90s) Correctness machinery** — run `ctest` property suite live; open
   ENGINE_INVARIANTS.md; show a differential test and explain the reference-oracle idea in
   two sentences. If they bite, show a shrunk regression fixture born from a property failure.
6. **(90s) The experiment pipeline** — `python -m microsim.experiments run rq2_micro.toml`
   (2-seed micro version, finishes in seconds), then the analyze output: same table shape as
   the findings doc. "Headline experiments are this, times 200 seeds, from hash-frozen
   configs."

## 15-minute technical walkthrough (onsite / final round)

1. **(2m)** Pitch + architecture diagram, tracing one order through the 11-step data flow.
2. **(3m)** Order book deep-dive: ORDER_BOOK_DESIGN comparison table → FastBook design →
   complexity table → the 128KB/L2 memory math. Whiteboard-ready without the doc.
3. **(2m)** Determinism: the ordering tuple, named RNG streams, the
   `std::normal_distribution` portability trap story.
4. **(3m)** Correctness: invariants → property generator profiles → differential oracle →
   fuzz targets inheriting the same oracles. Run the suite live if the room allows.
5. **(3m)** Research: RQ1 setup (CRN, paired design, pre-registration) → markout mechanism →
   findings figure with CIs → limitations *unprompted* (flow realism, simulated-market scope,
   the dropped circular question).
6. **(2m)** Performance: methodology-first story, baseline→optimized table, one optimization
   narrated end-to-end (profile → hypothesis → change → measurement), tried-didn't-pay table.
   Close on the roadmap and what you'd build next.

## Rehearsal rules

- Record and time each script; 3-minute must land ≤ 3:30 unhurried.
- Every demo runs offline (no network dependencies) from a checked-out tag; a `demo/`
  script pins the exact commands and pre-generated data for each beat.
- Have the three "if it breaks" lines ready: replay determinism means any demo crash is
  reproducible — saying that out loud turns a failure into a feature.
