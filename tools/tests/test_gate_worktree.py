#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the report-selection helpers in tools/gate/gate_worktree.ps1.

/ship, /release and the nightly gate decide "is this commit covered?" through these
helpers. A wrong answer either blocks a release for nothing or, worse, waves through a
commit whose E2E never ran -- so the edge cases (fast-tier reports, red reports, legacy
reports written before the tier field existed) are pinned down here.

Windows only: the helpers are Windows PowerShell 5.1 code.

    python tools/tests/test_gate_worktree.py
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HELPERS = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "gate", "gate_worktree.ps1"))


def same_path(a, b):
	return os.path.normcase(os.path.realpath(a)) == os.path.normcase(os.path.realpath(b))


def write_report(directory, name, **fields):
	path = os.path.join(directory, name)
	with open(path, "w", encoding="utf-8-sig") as f:
		json.dump(fields, f)
	return path


@unittest.skipUnless(sys.platform == "win32", "gate helpers are Windows PowerShell")
class GateWorktreeTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.reports = os.path.join(self._tmp.name, "reports")
		self.nightly = os.path.join(self._tmp.name, "nightly")
		os.makedirs(self.reports)
		os.makedirs(os.path.join(self.nightly, "tools", "gate"))

	def tearDown(self):
		self._tmp.cleanup()

	def ps(self, script):
		env = dict(os.environ, MMO_GATE_REPORT_DIR=self.reports, MMO_NIGHTLY_WORKTREE=self.nightly)
		result = subprocess.run(
			["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ". '{}'; {}".format(HELPERS, script)],
			capture_output=True, text=True, env=env)
		self.assertEqual(result.returncode, 0, result.stderr)
		return result.stdout.strip()

	def green(self, path):
		return self.ps("Test-GreenFullReport -Report (Read-GateReport -Path '{}')".format(path))

	def test_full_green_report_counts(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=True, tier="full", e2e_skipped=False)
		self.assertEqual(self.green(path), "True")

	def test_fast_report_does_not_count(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=True, tier="fast", e2e_skipped=True)
		self.assertEqual(self.green(path), "False")

	def test_red_full_report_does_not_count(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=False, tier="full", e2e_skipped=False)
		self.assertEqual(self.green(path), "False")

	def test_legacy_report_without_tier_counts_when_e2e_ran(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=True, e2e_skipped=False)
		self.assertEqual(self.green(path), "True")

	def test_skipped_nightly_does_not_count(self):
		path = write_report(self.reports, "a.json", skipped=True, passed=None)
		self.assertEqual(self.green(path), "False")

	def test_find_matches_commit_in_report_dir(self):
		write_report(self.reports, "nightly-2026-10-01.json", commit="other", passed=True, tier="full")
		expected = write_report(self.reports, "release-c1.json", commit="c1", passed=True, tier="full")
		self.assertTrue(same_path(self.ps("Find-GreenFullReport -Commit 'c1'"), expected))

	def test_find_checks_nightly_worktree_last_report(self):
		expected = write_report(os.path.join(self.nightly, "tools", "gate"), "last_report.json",
			commit="c2", passed=True, tier="full")
		self.assertTrue(same_path(self.ps("Find-GreenFullReport -Commit 'c2'"), expected))

	def test_find_returns_nothing_for_uncovered_commit(self):
		write_report(self.reports, "nightly-2026-10-01.json", commit="c1", passed=True, tier="fast")
		self.assertEqual(self.ps("Find-GreenFullReport -Commit 'c1'"), "")

	def test_last_green_nightly_skips_red_and_skipped_runs(self):
		write_report(self.reports, "nightly-2026-10-01.json", commit="old", passed=True, tier="full")
		write_report(self.reports, "nightly-2026-10-02.json", commit="good", passed=True, tier="full")
		write_report(self.reports, "nightly-2026-10-03.json", commit="bad", passed=False, tier="full")
		write_report(self.reports, "nightly-2026-10-04.json", skipped=True, passed=None)
		write_report(self.reports, "release-zzz.json", commit="rel", passed=True, tier="full")
		self.assertEqual(self.ps("(Get-LastGreenNightly).commit"), "good")

	def test_last_green_nightly_none(self):
		write_report(self.reports, "nightly-2026-10-03.json", commit="bad", passed=False, tier="full")
		self.assertEqual(self.ps("$null -eq (Get-LastGreenNightly)"), "True")


if __name__ == "__main__":
	unittest.main()
