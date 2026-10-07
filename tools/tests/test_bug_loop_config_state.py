#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the bug loop's configuration and persistent state (tools/bugs/bugloop)."""

import datetime
import json
import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import config as loop_config  # noqa: E402
from bugloop import state as loop_state  # noqa: E402


def utc(text):
	return datetime.datetime.strptime(text, "%Y-%m-%d %H:%M").replace(tzinfo=datetime.timezone.utc)


class ConfigTests(unittest.TestCase):
	def test_missing_file_gives_defaults(self):
		config = loop_config.load_config(os.path.join(tempfile.gettempdir(), "bug-loop-missing.json"))
		self.assertEqual(config.autoship_cap_per_day, 5)
		self.assertEqual(config.max_changed_lines, 150)
		self.assertEqual(config.freeze_start_utc, "21:30")

	def test_unknown_key_is_an_error(self):
		with tempfile.TemporaryDirectory() as folder:
			path = os.path.join(folder, "config.json")
			with open(path, "w", encoding="utf-8") as handle:
				json.dump({"autoship_cap": 9}, handle)
			with self.assertRaises(ValueError):
				loop_config.load_config(path)

	def test_checked_in_config_loads(self):
		config = loop_config.load_config(os.path.join(REPO_ROOT, "tools", "bugs", "bug_loop.json"))
		# Machine paths never live in the repository; the worktree is resolved at startup.
		self.assertEqual(config.worktree, "")
		self.assertEqual(config.worker, "bug-loop")


class StateTests(unittest.TestCase):
	def setUp(self):
		self.folder = tempfile.TemporaryDirectory()
		self.addCleanup(self.folder.cleanup)
		self.path = os.path.join(self.folder.name, "state.json")

	def test_day_roll_resets_counters_but_keeps_attempts(self):
		state = loop_state.LoopState(self.path, "2026-10-07")
		state.count_invocation()
		state.count_autoship()
		state.mark_attempted("a", "shipped")
		state.record("a", "shipped")
		state.save()
		state = loop_state.LoopState(self.path, "2026-10-08")
		self.assertEqual(state.data["invocations"], 0)
		self.assertEqual(state.data["autoships"], 0)
		self.assertEqual(state.data["outcomes"], [])
		self.assertTrue(state.attempted("a"))

	def test_budget_and_cap(self):
		state = loop_state.LoopState(self.path, "2026-10-07")
		state.count_invocation()
		self.assertTrue(state.budget_left(2))
		self.assertFalse(state.budget_left(2, needed=2))
		self.assertTrue(state.autoship_left(1))
		state.count_autoship()
		self.assertFalse(state.autoship_left(1))

	def test_fix_queue_orders_by_severity_then_age(self):
		state = loop_state.LoopState(self.path, "2026-10-07")
		state.enqueue_fix("old-low", "low", "2026-01-01")
		state.enqueue_fix("new-high", "high", "2026-03-01")
		state.enqueue_fix("old-high", "high", "2026-02-01")
		state.enqueue_fix("old-high", "high", "2026-02-01")
		self.assertEqual(len(state.data["fix_queue"]), 3)
		self.assertEqual(state.next_fix(), "old-high")
		state.drop_fix("old-high")
		self.assertEqual(state.next_fix(), "new-high")

	def test_forget_clears_attempt_and_triage_failures(self):
		state = loop_state.LoopState(self.path, "2026-10-07")
		self.assertEqual(state.triage_failed("a"), 1)
		self.assertEqual(state.triage_failed("a"), 2)
		state.mark_attempted("a", "needs-info")
		state.forget("a")
		self.assertFalse(state.attempted("a"))
		self.assertEqual(state.triage_failed("a"), 1)

	def test_ship_queue_is_taken_once(self):
		state = loop_state.LoopState(self.path, "2026-10-07")
		state.enqueue_ship("a", "bugfix/a", "summary", "head")
		self.assertEqual([item["bug"] for item in state.take_ship_queue()], ["a"])
		self.assertEqual(state.take_ship_queue(), [])


class FreezeTests(unittest.TestCase):
	def test_inside_and_outside(self):
		self.assertTrue(loop_state.in_freeze(utc("2026-10-07 21:30"), "21:30", "23:59"))
		self.assertTrue(loop_state.in_freeze(utc("2026-10-07 23:59"), "21:30", "23:59"))
		self.assertFalse(loop_state.in_freeze(utc("2026-10-07 21:29"), "21:30", "23:59"))
		self.assertFalse(loop_state.in_freeze(utc("2026-10-08 00:00"), "21:30", "23:59"))

	def test_window_across_midnight(self):
		self.assertTrue(loop_state.in_freeze(utc("2026-10-08 00:30"), "23:00", "01:00"))
		self.assertFalse(loop_state.in_freeze(utc("2026-10-08 12:00"), "23:00", "01:00"))


class BreakerTests(unittest.TestCase):
	def test_trip_and_reset(self):
		with tempfile.TemporaryDirectory() as folder:
			self.assertFalse(loop_state.breaker_active(folder))
			loop_state.trip_breaker(folder, "test", utc("2026-10-07 10:00"))
			self.assertTrue(loop_state.breaker_active(folder))
			with open(os.path.join(folder, loop_state.BREAKER_FILE), encoding="utf-8") as handle:
				self.assertIn("test", handle.read())
			loop_state.reset_breaker(folder)
			self.assertFalse(loop_state.breaker_active(folder))

	def test_red_nightly_with_loop_merge_blames_loop(self):
		loop_merge = "abc123 Merge bugfix/0a1b2c3d (bug-loop, gate green at 1234abcd)"
		self.assertTrue(loop_state.red_nightly_blames_loop({"passed": False, "merges_since_last_green": [loop_merge]}))
		self.assertFalse(loop_state.red_nightly_blames_loop({"passed": False, "merges_since_last_green": ["abc Merge feature/x (gate green at 1)"]}))
		self.assertFalse(loop_state.red_nightly_blames_loop({"passed": True, "merges_since_last_green": [loop_merge]}))
		self.assertFalse(loop_state.red_nightly_blames_loop({"passed": None, "merges_since_last_green": [loop_merge]}))
		self.assertFalse(loop_state.red_nightly_blames_loop(None))

	def test_newest_nightly_reads_reports_with_bom(self):
		with tempfile.TemporaryDirectory() as folder:
			for day, passed in (("2026-10-06", True), ("2026-10-07", False)):
				with open(os.path.join(folder, "nightly-{}.json".format(day)), "w", encoding="utf-8-sig") as handle:
					json.dump({"passed": passed}, handle)
			name, report = loop_state.newest_nightly(folder)
			self.assertEqual(name, "nightly-2026-10-07.json")
			self.assertFalse(report["passed"])

	def test_newest_nightly_without_reports(self):
		with tempfile.TemporaryDirectory() as folder:
			self.assertEqual(loop_state.newest_nightly(folder), (None, None))


if __name__ == "__main__":
	unittest.main()
