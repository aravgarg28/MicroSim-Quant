#!/usr/bin/env python3
"""Unit tests for scripts/bench_compare.py (task R1-23).

Drives the comparator over committed fixture JSONs (tests/bench/fixtures/) that
stand in for Google Benchmark output: a baseline, a p50 regression, an
allocs/op regression, a within-noise change, a single-run (no aggregates) file,
and a renamed set (new/removed benchmarks). Verifies both the per-row verdicts
and the process exit status that gates CI, and that the allocs/op column is
present in the rendered table.
"""

import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_FIXTURES = os.path.join(_HERE, "fixtures")
sys.path.insert(0, os.path.join(_HERE, "..", "..", "scripts"))

import bench_compare  # noqa: E402


def fixture(name: str) -> str:
    return os.path.join(_FIXTURES, name)


def verdicts(rows) -> dict[str, str]:
    return {row.name: row.verdict for row in rows}


class LoadResultsTest(unittest.TestCase):
    def test_uses_median_aggregate_and_cv(self):
        results = bench_compare.load_results(fixture("baseline.json"))
        self.assertIn("book_insert/1000/0/manual_time", results)
        insert = results["book_insert/1000/0/manual_time"]
        self.assertEqual(insert.p50, 100.0)
        self.assertEqual(insert.unit, "ns")
        self.assertAlmostEqual(insert.cv, 0.02)
        self.assertEqual(insert.allocs_per_op, 1.0)

    def test_falls_back_to_single_iteration_run(self):
        # A file with no aggregates (repetitions=1) still yields p50 = real_time.
        results = bench_compare.load_results(fixture("single_run.json"))
        self.assertEqual(results["book_insert/1000/0/manual_time"].p50, 100.0)
        self.assertEqual(results["book_cancel/1000/0/manual_time"].p50, 50.0)


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.baseline = bench_compare.load_results(fixture("baseline.json"))

    def _compare(self, new_fixture: str, threshold: float = 0.05):
        new = bench_compare.load_results(fixture(new_fixture))
        return bench_compare.compare(self.baseline, new, threshold)

    def test_identical_runs_have_no_regression(self):
        rows, regression = self._compare("baseline.json")
        self.assertFalse(regression)
        self.assertEqual(set(verdicts(rows).values()), {"no change"})

    def test_p50_regression_is_flagged(self):
        rows, regression = self._compare("regressed.json")  # book_insert +30%
        self.assertTrue(regression)
        v = verdicts(rows)
        self.assertEqual(v["book_insert/1000/0/manual_time"], "REGRESSION")
        self.assertEqual(v["book_cancel/1000/0/manual_time"], "no change")

    def test_allocation_increase_is_a_regression_even_at_equal_time(self):
        rows, regression = self._compare("allocs_up.json")  # 1 -> 2 allocs/op, same time
        self.assertTrue(regression)
        self.assertEqual(
            verdicts(rows)["book_insert/1000/0/manual_time"], "REGRESSION"
        )

    def test_change_within_noise_floor_is_not_a_regression(self):
        # +8% p50 but cv is 10%, so the change is inside the noise floor.
        rows, regression = self._compare("within_noise.json")
        self.assertFalse(regression)
        self.assertEqual(
            verdicts(rows)["book_insert/1000/0/manual_time"], "no change"
        )

    def test_improvement_is_reported_not_flagged(self):
        # Comparing the regressed set as the *old* baseline against the faster one.
        old = bench_compare.load_results(fixture("regressed.json"))
        new = bench_compare.load_results(fixture("baseline.json"))
        rows, regression = bench_compare.compare(old, new, 0.05)
        self.assertFalse(regression)
        self.assertEqual(
            verdicts(rows)["book_insert/1000/0/manual_time"], "improvement"
        )

    def test_new_and_removed_benchmarks(self):
        rows, regression = self._compare("renamed_set.json")
        v = verdicts(rows)
        self.assertEqual(v["book_cancel/1000/0/manual_time"], "removed")
        self.assertEqual(v["book_modify/1000/0/manual_time"], "new")
        self.assertFalse(regression)  # a removed/new benchmark is not itself a regression


class TableAndMainTest(unittest.TestCase):
    def test_table_has_allocs_column(self):
        old = bench_compare.load_results(fixture("baseline.json"))
        new = bench_compare.load_results(fixture("regressed.json"))
        rows, _ = bench_compare.compare(old, new, 0.05)
        table = bench_compare.format_table(rows)
        self.assertIn("allocs/op", table)
        self.assertIn("1->1", table)  # allocs unchanged shown old->new

    def test_main_exit_status(self):
        clean = bench_compare.main([fixture("baseline.json"), fixture("baseline.json")])
        self.assertEqual(clean, 0)
        regressed = bench_compare.main([fixture("baseline.json"), fixture("regressed.json")])
        self.assertEqual(regressed, 1)


if __name__ == "__main__":
    unittest.main()
