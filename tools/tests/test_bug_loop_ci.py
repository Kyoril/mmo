#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for reading the GitHub nightly the bug loop's circuit breaker reacts to."""

import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import ci, state as loop_state  # noqa: E402


def run(run_id, conclusion, sha, status="completed"):
	return {"databaseId": run_id, "status": status, "conclusion": conclusion, "headSha": sha}


class NewestNightlyTests(unittest.TestCase):
	def setUp(self):
		self.spans = []

	def log(self, green, head):
		self.spans.append((green, head))
		return ["e89eb837 Merge bugfix/6462d2bc (bug-loop, maintainer decision)", "dbd41c88 Merge feature/x (gate green at 1)"]

	def test_red_run_lists_the_merges_since_the_last_green_run(self):
		name, report = ci.newest_nightly([run(3, "failure", "c"), run(2, "failure", "b"), run(1, "success", "a")], self.log)
		self.assertEqual(name, "nightly run 3")
		self.assertEqual(self.spans, [("a", "c")])
		self.assertFalse(report["passed"])
		self.assertTrue(loop_state.red_nightly_blames_loop(report))

	def test_green_run_blames_nobody(self):
		name, report = ci.newest_nightly([run(5, "success", "e"), run(4, "failure", "d")], self.log)
		self.assertEqual(name, "nightly run 5")
		self.assertTrue(report["passed"])
		self.assertFalse(loop_state.red_nightly_blames_loop(report))
		self.assertEqual(self.spans, [])

	def test_running_cancelled_and_skipped_runs_are_ignored(self):
		runs = [run(9, "", "i", status="in_progress"), run(8, "cancelled", "h"), run(7, "skipped", "g"), run(6, "failure", "f")]
		name, report = ci.newest_nightly(runs, self.log)
		self.assertEqual(name, "nightly run 6")
		self.assertFalse(report["passed"])
		self.assertEqual(self.spans, [(None, "f")])

	def test_no_finished_run(self):
		self.assertEqual(ci.newest_nightly([run(1, "", "a", status="queued")], self.log), (None, None))


if __name__ == "__main__":
	unittest.main()
