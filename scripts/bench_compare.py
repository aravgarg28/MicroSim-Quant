#!/usr/bin/env python3
"""Compare two Google Benchmark JSON runs and flag regressions (task R1-23).

Per docs/performance/METHODOLOGY.md, a benchmark only guides work if it is
compared against a *stored* baseline from the same machine and flags (rule 10).
This is the comparator behind that rule: it reads the JSON that
``scripts/run_benchmarks.sh`` writes and flags any benchmark whose p50 (the
Google Benchmark ``median`` aggregate) regresses beyond a threshold, or whose
``allocs/op`` column rose at all (allocations are a hard, countable budget --
MEMORY_MODEL.md).

Noise floor (rule 10): a change smaller than the benchmark's own run-to-run
spread is "no change", never a regression. Google Benchmark reports that spread
as the ``cv`` (coefficient of variation) aggregate; when present it raises the
effective threshold for that row.

Percentiles: Google Benchmark's built-in aggregates give p50 (median), mean,
stddev, and cv -- not p95/p99/p99.9. Those tail percentiles need the
manual-timing latency-histogram harness (a later refinement); this comparator
gates on p50 with the cv noise floor and on allocs/op, which is what the R1
microbenchmark JSON carries.

Usage:
    bench_compare.py OLD.json NEW.json [--p50-threshold 0.05] [--json]

Exit status is non-zero if any regression is found, so it can gate CI.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from typing import Optional


@dataclass
class Result:
    """One benchmark's summary: its p50 time, spread, and allocations/op."""

    name: str
    p50: float  # median real_time, in the benchmark's time unit
    unit: str
    cv: float  # coefficient of variation (fractional), 0.0 if not reported
    allocs_per_op: Optional[float]


def load_results(path: str) -> dict[str, Result]:
    """Parse a Google Benchmark JSON file into {benchmark name: Result}.

    Uses the ``median`` aggregate for p50 when repetitions produced one;
    otherwise falls back to the single run's ``real_time``. Benchmarks are keyed
    by ``run_name`` (the name without the aggregate suffix).
    """
    with open(path, encoding="utf-8") as handle:
        doc = json.load(handle)

    medians: dict[str, dict] = {}
    cvs: dict[str, float] = {}
    singles: dict[str, dict] = {}

    for entry in doc.get("benchmarks", []):
        name = entry.get("run_name", entry.get("name", ""))
        aggregate = entry.get("aggregate_name", "")
        run_type = entry.get("run_type", "")
        if aggregate == "median":
            medians[name] = entry
        elif aggregate == "cv":
            cvs[name] = float(entry.get("real_time", 0.0))
        elif run_type == "iteration" and name not in singles:
            singles[name] = entry  # first (or only) repetition, when no aggregates exist

    results: dict[str, Result] = {}
    for name in set(medians) | set(singles):
        entry = medians.get(name, singles.get(name))
        assert entry is not None
        results[name] = Result(
            name=name,
            p50=float(entry.get("real_time", 0.0)),
            unit=entry.get("time_unit", "ns"),
            cv=cvs.get(name, 0.0),
            allocs_per_op=_counter(entry, "allocs/op"),
        )
    return results


def _counter(entry: dict, key: str) -> Optional[float]:
    value = entry.get(key)
    return float(value) if value is not None else None


@dataclass
class Row:
    """A single line of the comparison report."""

    name: str
    old_p50: Optional[float]
    new_p50: Optional[float]
    unit: str
    delta: Optional[float]  # fractional change in p50, new vs old
    old_allocs: Optional[float]
    new_allocs: Optional[float]
    verdict: str  # REGRESSION | improvement | no change | new | removed


# Allocation counts are integers per op; treat a rise beyond this as real.
_ALLOC_EPSILON = 1e-6


def compare(
    old: dict[str, Result], new: dict[str, Result], p50_threshold: float
) -> tuple[list[Row], bool]:
    """Compare two result sets. Returns (rows, any_regression)."""
    rows: list[Row] = []
    regression = False

    for name in sorted(set(old) | set(new)):
        o = old.get(name)
        n = new.get(name)

        if o is None:
            rows.append(Row(name, None, n.p50, n.unit, None, None, n.allocs_per_op, "new"))
            continue
        if n is None:
            rows.append(Row(name, o.p50, None, o.unit, None, o.allocs_per_op, None, "removed"))
            continue

        delta = (n.p50 - o.p50) / o.p50 if o.p50 > 0 else 0.0
        floor = max(p50_threshold, o.cv, n.cv)

        alloc_regressed = (
            o.allocs_per_op is not None
            and n.allocs_per_op is not None
            and n.allocs_per_op > o.allocs_per_op + _ALLOC_EPSILON
        )

        if delta > floor or alloc_regressed:
            verdict = "REGRESSION"
            regression = True
        elif delta < -floor:
            verdict = "improvement"
        else:
            verdict = "no change"

        rows.append(
            Row(name, o.p50, n.p50, n.unit, delta, o.allocs_per_op, n.allocs_per_op, verdict)
        )

    return rows, regression


def format_table(rows: list[Row]) -> str:
    """Render the comparison as a fixed-width table with an allocs/op column."""
    header = f"{'benchmark':<52}{'p50 old':>12}{'p50 new':>12}{'delta':>9}  {'allocs/op':>16}  verdict"
    lines = [header, "-" * len(header)]
    for r in rows:
        old_p50 = f"{r.old_p50:.1f}" if r.old_p50 is not None else "-"
        new_p50 = f"{r.new_p50:.1f}" if r.new_p50 is not None else "-"
        delta = f"{r.delta * 100:+.1f}%" if r.delta is not None else "-"
        oa = f"{r.old_allocs:g}" if r.old_allocs is not None else "-"
        na = f"{r.new_allocs:g}" if r.new_allocs is not None else "-"
        allocs = f"{oa}->{na}"
        lines.append(
            f"{r.name:<52}{old_p50:>12}{new_p50:>12}{delta:>9}  {allocs:>16}  {r.verdict}"
        )
    return "\n".join(lines)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Compare two Google Benchmark JSON runs.")
    parser.add_argument("old", help="baseline JSON (results/benchmarks/...)")
    parser.add_argument("new", help="new run JSON")
    parser.add_argument(
        "--p50-threshold",
        type=float,
        default=0.05,
        help="fractional p50 regression threshold before the cv noise floor (default 0.05)",
    )
    parser.add_argument("--json", action="store_true", help="emit the rows as JSON")
    args = parser.parse_args(argv)

    old = load_results(args.old)
    new = load_results(args.new)
    rows, regression = compare(old, new, args.p50_threshold)

    if args.json:
        print(json.dumps([row.__dict__ for row in rows], indent=2))
    else:
        print(format_table(rows))
        print()
        print("REGRESSION" if regression else "no regressions")

    return 1 if regression else 0


if __name__ == "__main__":
    sys.exit(main())
