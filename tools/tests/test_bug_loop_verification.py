#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the regression proof and gate runs, with fake processes and a fake worktree."""

import os
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import guard, verification  # noqa: E402


class FakeTree:
	def __init__(self, path="wt"):
		self.path = path
		self.commands = []

	def git(self, *args, cwd=None, check=True):
		self.commands.append(("git",) + args)
		return ""

	def checkout(self, ref):
		self.commands.append(("checkout", ref))


class FakeRun:
	def __init__(self, codes):
		self.codes = list(codes)
		self.commands = []

	def __call__(self, command, **kwargs):
		self.commands.append(command)
		code = self.codes.pop(0)
		if code is None:
			raise subprocess.TimeoutExpired(command, kwargs.get("timeout"))
		return subprocess.CompletedProcess(command, code, "", "")


UNIT = {"kind": "unit", "suite": "game_server_tests", "filter": "[quest]"}


def verifier(codes, tree=None):
	run = FakeRun(codes)
	return verification.Verifier(tree or FakeTree(), "H:/mmo/build", run=run), run


class ProofTests(unittest.TestCase):
	def test_fails_before_passes_after(self):
		tree = FakeTree()
		check, run = verifier([0, 1, 0, 0], tree)
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/game_server_tests/test_q.cpp"])
		self.assertTrue(result["ok"], result)
		self.assertEqual((result["before"], result["after"]), ("failed", "passed"))
		self.assertEqual(run.commands[0], ["cmake", "--build", "build", "--config", "Debug", "-t", "game_server_tests"])
		self.assertEqual(run.commands[1], [os.path.join("bin", "Debug", "game_server_tests.exe"), "[quest]"])
		self.assertEqual(tree.commands, [
			("checkout", "base"),
			("git", "checkout", "bugfix/x", "--", "src/tests/game_server_tests/test_q.cpp"),
			("checkout", "bugfix/x"),
		])

	def test_test_passing_before_the_fix_proves_nothing(self):
		check, _ = verifier([0, 0, 0, 0])
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertFalse(result["ok"])
		self.assertIn("before=passed", result["reason"])

	def test_test_that_does_not_compile_before_counts_as_failing(self):
		check, _ = verifier([1, 0, 0])
		self.assertTrue(check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])["ok"])

	def test_failing_after_the_fix(self):
		check, _ = verifier([0, 1, 0, 1])
		self.assertFalse(check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])["ok"])

	def test_no_test_files_runs_nothing(self):
		check, run = verifier([])
		result = check.proof(UNIT, "base", "bugfix/x", [])
		self.assertFalse(result["ok"])
		self.assertEqual(run.commands, [])

	def test_e2e_scenario_command(self):
		check, run = verifier([0, 1, 0, 0])
		check.proof({"kind": "e2e", "scenario": "quest_kill_credit"}, "base", "bugfix/x", ["e2e/scenarios/quest_kill_credit.lua"])
		self.assertEqual(run.commands[0][-4:], list(verification.E2E_TARGETS))
		self.assertEqual(run.commands[1][-3:], [os.path.join("tools", "e2e", "e2e_run.ps1"), "-Scenario", "quest_kill_credit"])

	def test_timeout_before_does_not_prove_anything(self):
		check, _ = verifier([0, None, 0, 0])
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertFalse(result["ok"])
		self.assertIn("before=timeout", result["reason"])

	def test_proof_restores_branch_on_error(self):
		tree = FakeTree()
		tree.git = lambda *args, **kwargs: (_ for _ in ()).throw(RuntimeError("git error"))
		check, _ = verifier([0, 1, 0, 0], tree)
		with self.assertRaises(RuntimeError):
			check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertEqual(tree.commands[-1], ("checkout", "bugfix/x"))

	def test_proof_includes_output_tail_in_result(self):
		check, run = verifier([0, 1, 0, 0])
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertIn("output_tail", result)
		self.assertIsInstance(result["output_tail"], str)


class GateTests(unittest.TestCase):
	def test_gate_removes_stale_report_and_runs_tier(self):
		with tempfile.TemporaryDirectory() as folder:
			report = os.path.join(folder, "tools", "gate", "last_report.json")
			os.makedirs(os.path.dirname(report))
			with open(report, "w") as handle:
				handle.write("{}")
			check, run = verifier([0], FakeTree(folder))
			result = check.gate("full")
			self.assertTrue(result["ok"])
			self.assertFalse(os.path.exists(report))
			self.assertIn(os.path.join(folder, "tools", "gate", "verify.ps1"), run.commands[0])
			self.assertEqual(run.commands[0][-2:], ["-Tier", "full"])

	def test_red_gate(self):
		check, _ = verifier([1])
		self.assertFalse(check.gate("fast")["ok"])


class RegressionFileTests(unittest.TestCase):
	def test_only_test_paths(self):
		changes = [guard.FileChange("src/tests/a_tests/t.cpp", 1, 0, False), guard.FileChange("src/a.cpp", 1, 0, False),
			guard.FileChange("e2e/scenarios/s.lua", 1, 0, False)]
		self.assertEqual(verification.regression_files(changes), ["src/tests/a_tests/t.cpp", "e2e/scenarios/s.lua"])


if __name__ == "__main__":
	unittest.main()
