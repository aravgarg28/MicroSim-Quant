# Optional Dashboard (Release 6 — strictly last, D13)

## Does a dashboard materially improve the demonstration?

**Yes, narrowly.** Recruiters and non-specialist interviewers respond to a moving order book
in a way they cannot to a Parquet file; three of the demo script's beats land better visually
(book dynamics, latency A/B, inventory skew in action). But every hour here is an hour not
spent on engine depth — hence the hard gate: R6 starts only after R5's success criteria and
the three findings docs are done. If recruiting timing forces a cut, the dashboard is the cut
(D4/D13), and the demo falls back to notebook figures + terminal replay, which is fully
acceptable.

## Architecture (fixed to keep it cheap)

Read-only replay viewer over **files** — no live engine coupling, no websocket-to-C++
plumbing: FastAPI serves recorded replay logs + Parquet results; React/Next.js front end
renders. `dashboard/` has zero build coupling to the C++ tree (COMPONENT_BOUNDARIES). Local
only (`localhost`), no auth, no hosting, no paid services.

## Screens (complete list — anything more is scope creep)

1. **Book replay** — depth ladder animating through a recorded session; playback controls
   (speed, pause, jump-to-time); trade tape alongside; event-log cursor readout.
2. **Strategy view** — inventory, equity, and quote-state time series for chosen
   participants, synced to the replay cursor.
3. **Latency A/B** — two RQ1 arms side by side on the same seed (same flow!): slow MM vs
   fast MM quotes/fills around identical market moments; the money screen for interviews.
4. **Experiment browser** — table of stored experiments/runs (manifests), headline metric
   tables with CIs, links to findings docs.
5. **Benchmark viewer** — the stored benchmark JSONs as sortable tables + trend sparklines.

Explicitly excluded: live simulation control, config editing, strategy tuning UI, auth,
multi-user anything, deployment.

## Tests

API contract tests (FastAPI TestClient over golden fixture files); one Playwright smoke
(canned replay renders and plays). That's all — UI test depth is not where this project's
credibility lives.

## Effort box

~2 weeks of Opus tasks, timeboxed: if screens 1–3 aren't demo-ready in that box, ship what
renders and stop. The Release-6 exit criterion is the three-minute demo script running, not
screen completeness.
