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
from datetime import date

GATE_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "gate"))
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


@unittest.skipUnless(sys.platform == "win32", "gate scripts are Windows PowerShell")
class GateScriptTests(unittest.TestCase):
	"""release_check.ps1 / nightly_gate.ps1 end to end, on paths that never touch a worktree."""

	@classmethod
	def setUpClass(cls):
		cls.sha = subprocess.run(["git", "-C", GATE_DIR, "rev-parse", "HEAD"],
			capture_output=True, text=True, check=True).stdout.strip()

	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.reports = os.path.join(self._tmp.name, "reports")
		self.nightly = os.path.join(self._tmp.name, "nightly")
		os.makedirs(self.reports)
		os.makedirs(self.nightly)

	def tearDown(self):
		self._tmp.cleanup()

	def run_script(self, name, *args):
		env = dict(os.environ, MMO_GATE_REPORT_DIR=self.reports, MMO_NIGHTLY_WORKTREE=self.nightly)
		return subprocess.run(
			["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(GATE_DIR, name)] + list(args),
			capture_output=True, text=True, env=env)

	def today_report(self):
		path = os.path.join(self.reports, "nightly-{}.json".format(date.today().isoformat()))
		with open(path, encoding="utf-8-sig") as f:
			return json.load(f)

	def test_release_check_covered_commit_is_green(self):
		write_report(self.reports, "release-x.json", commit=self.sha, passed=True, tier="full")
		self.assertEqual(self.run_script("release_check.ps1", "-Ref", self.sha, "-NoRun").returncode, 0)

	def test_release_check_uncovered_commit_is_red(self):
		write_report(self.reports, "release-x.json", commit=self.sha, passed=True, tier="fast")
		self.assertEqual(self.run_script("release_check.ps1", "-Ref", self.sha, "-NoRun").returncode, 1)

	def test_release_check_unknown_ref(self):
		self.assertEqual(self.run_script("release_check.ps1", "-Ref", "no-such-ref-xyz", "-NoRun").returncode, 2)

	def test_nightly_unknown_ref_writes_red_report(self):
		self.assertEqual(self.run_script("nightly_gate.ps1", "-Ref", "no-such-ref-xyz").returncode, 1)
		report = self.today_report()
		self.assertIs(report["passed"], False)
		self.assertTrue(report["setup_error"])

	def test_nightly_unchanged_commit_is_green_with_list_merges(self):
		write_report(self.reports, "nightly-2026-01-01.json", commit=self.sha, passed=True, tier="full")
		result = self.run_script("nightly_gate.ps1", "-Ref", self.sha)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		report = self.today_report()
		self.assertIs(report["passed"], True)
		self.assertIs(report["unchanged"], True)
		self.assertEqual(report["merges_since_last_green"], [])


if __name__ == "__main__":
	unittest.main()
