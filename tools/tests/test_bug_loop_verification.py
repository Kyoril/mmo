#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the regression proof and gate runs, with fake processes and a fake worktree."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import guard, verification  # noqa: E402


class FakeTree:
	def __init__(self, path="wt", events=None, deleted=()):
		self.path = path
		self.commands = []
		self.events = events if events is not None else []
		self.deleted = set(deleted)

	def git(self, *args, cwd=None, check=True):
		if args[0] == "ls-tree":
			files = args[args.index("--") + 1:]
			return "\0".join(path for path in files if path not in self.deleted)
		self.commands.append(("git",) + args)
		self.events.append(("git",) + args)
		return ""

	def checkout(self, ref):
		self.commands.append(("checkout", ref))
		self.events.append(("checkout", ref))


class FakeRun:
	def __init__(self, codes, events=None):
		self.codes = list(codes)
		self.commands = []
		self.events = events if events is not None else []

	def __call__(self, command, **kwargs):
		self.commands.append(command)
		# Record build targets and test runs in event log
		if len(command) >= 4 and command[:3] == ["cmake", "--build", "build"]:
			# Build command: extract targets
			targets = command[command.index("-t") + 1:] if "-t" in command else []
			self.events.append(("build",) + tuple(targets))
		else:
			# Test command
			self.events.append(("test", command[-1] if command else ""))

		code = self.codes.pop(0)
		if code is None:
			raise subprocess.TimeoutExpired(command, kwargs.get("timeout"))
		return subprocess.CompletedProcess(command, code, "", "")


UNIT = {"kind": "unit", "suite": "game_server_tests", "filter": "[quest]"}


def verifier(codes, tree=None):
	if tree is None:
		events = []
		tree = FakeTree(events=events)
	else:
		# Use the tree's events list if it has one, otherwise create a new one
		if not hasattr(tree, 'events'):
			tree.events = []
		events = tree.events
	run = FakeRun(codes, events=events)
	return verification.Verifier(tree, "H:/mmo/build", run=run), run, events


class ProofTests(unittest.TestCase):
	def test_fails_before_passes_after(self):
		tree = FakeTree()
		events = []
		tree.events = events
		check, run, _ = verifier([0, 1, 0, 0], tree)
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/game_server_tests/test_q.cpp"])
		self.assertTrue(result["ok"], result)
		self.assertEqual((result["before"], result["after"]), ("failed", "passed"))
		self.assertEqual(run.commands[0], ["cmake", "--build", "build", "--config", "Debug", "-t", "game_server_tests"])
		self.assertEqual(run.commands[1], [os.path.abspath(os.path.join("wt", "bin", "Debug", "game_server_tests.exe")), "[quest]"])
		self.assertEqual(tree.commands, [
			("checkout", "base"),
			("git", "checkout", "bugfix/x", "--", "src/tests/game_server_tests/test_q.cpp"),
			("checkout", "bugfix/x"),
		])

	def test_after_attempt_on_fix_branch(self):
		"""Verify that the after attempt runs on the full fix branch, not the partial branch."""
		events = []
		tree = FakeTree(events=events)
		check, run, _ = verifier([0, 1, 0, 0], tree)
		# Codes: 0 (before build ok), 1 (before test fail), 0 (after build ok), 0 (after test pass)
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/game_server_tests/test_q.cpp"])
		self.assertTrue(result["ok"], result)

		# Verify exact sequence of tree checkouts and git operations interspersed with build/test
		# Expected: checkout base -> git checkout files -> before build -> before test -> checkout branch -> after build -> after test
		expected_sequence = [
			("checkout", "base"),
			("git", "checkout", "bugfix/x", "--", "src/tests/game_server_tests/test_q.cpp"),
			("build", "game_server_tests"),
			("test", "[quest]"),
			("checkout", "bugfix/x"),  # THIS IS THE KEY: before this is before attempt, after is after attempt
			("build", "game_server_tests"),
			("test", "[quest]"),
		]
		self.assertEqual(events, expected_sequence,
			f"Expected:\n{expected_sequence}\nGot:\n{events}")

	def test_test_passing_before_the_fix_proves_nothing(self):
		check, _, _ = verifier([0, 0, 0, 0])
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertFalse(result["ok"])
		self.assertIn("before=passed", result["reason"])

	def test_test_that_does_not_compile_before_counts_as_failing(self):
		check, _, _ = verifier([1, 0, 0])
		self.assertTrue(check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])["ok"])

	def test_failing_after_the_fix(self):
		check, _, _ = verifier([0, 1, 0, 1])
		self.assertFalse(check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])["ok"])

	def test_no_test_files_runs_nothing(self):
		check, run, _ = verifier([])
		result = check.proof(UNIT, "base", "bugfix/x", [])
		self.assertFalse(result["ok"])
		self.assertEqual(run.commands, [])

	def test_e2e_scenario_command(self):
		check, run, _ = verifier([0, 1, 0, 0])
		check.proof({"kind": "e2e", "scenario": "quest_kill_credit"}, "base", "bugfix/x", ["e2e/scenarios/quest_kill_credit.lua"])
		self.assertEqual(run.commands[0][-4:], list(verification.E2E_TARGETS))
		self.assertEqual(run.commands[1][-3:], [os.path.join("tools", "e2e", "e2e_run.ps1"), "-Scenario", "quest_kill_credit"])

	def test_timeout_before_does_not_prove_anything(self):
		check, _, _ = verifier([0, None, 0, 0])
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertFalse(result["ok"])
		self.assertIn("before=timeout", result["reason"])

	def test_proof_restores_branch_on_error(self):
		tree = FakeTree()
		listing = tree.git

		def failing_git(*args, **kwargs):
			if args[0] == "ls-tree":
				return listing(*args, **kwargs)
			raise RuntimeError("git error")
		tree.git = failing_git
		check, _, _ = verifier([0, 1, 0, 0], tree)
		with self.assertRaises(RuntimeError):
			check.proof(UNIT, "base", "head1", ["src/tests/a.cpp"])
		self.assertEqual(tree.commands[-1], ("checkout", "head1"))

	def test_deleted_test_files_are_not_checked_out(self):
		tree = FakeTree(deleted=["src/tests/old.cpp"])
		check, _, _ = verifier([0, 1, 0, 0], tree)
		self.assertTrue(check.proof(UNIT, "base", "head1", ["src/tests/old.cpp", "src/tests/new.cpp"])["ok"])
		self.assertIn(("git", "checkout", "head1", "--", "src/tests/new.cpp"), tree.commands)
		tree = FakeTree(deleted=["src/tests/old.cpp"])
		check, _, _ = verifier([0, 1, 0, 0], tree)
		check.proof(UNIT, "base", "head1", ["src/tests/old.cpp"])
		self.assertEqual(tree.commands, [("checkout", "base"), ("checkout", "head1")])

	def test_missing_executable_is_a_failed_step(self):
		def missing(command, **kwargs):
			raise FileNotFoundError(2, "The system cannot find the file specified")
		check = verification.Verifier(FakeTree(), "H:/mmo/build", run=missing)
		self.assertFalse(check.build(["game_server_tests"]))
		self.assertIn("cannot find the file", check.last_output)
		self.assertFalse(check.last_timed_out)

	def test_proof_includes_output_tail_in_result(self):
		check, run, _ = verifier([0, 1, 0, 0])
		result = check.proof(UNIT, "base", "bugfix/x", ["src/tests/a.cpp"])
		self.assertIn("output_tail", result)
		self.assertIsInstance(result["output_tail"], str)


class RealProcessTests(unittest.TestCase):
	"""The unit-test executable must be found with the real process API while the loop's own cwd
	is somewhere else (C1: CreateProcess resolves a relative program against the parent's cwd)."""

	def test_unit_test_exe_runs_from_another_cwd(self):
		with tempfile.TemporaryDirectory() as folder:
			exe = os.path.join(folder, "bin", "Debug", "x_tests.exe")
			os.makedirs(os.path.dirname(exe))
			if os.name == "nt":
				shutil.copyfile(os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "hostname.exe"), exe)
			else:
				with open(exe, "w", encoding="utf-8") as handle:
					handle.write("#!/bin/sh\nexit 0\n")
				os.chmod(exe, 0o755)
			self.assertNotEqual(os.path.abspath(os.getcwd()), os.path.abspath(folder))
			check = verification.Verifier(FakeTree(folder), "H:/mmo/build")
			command, _ = check._test({"kind": "unit", "suite": "x_tests"})
			self.assertTrue(os.path.isabs(command[0]))
			self.assertEqual(os.path.dirname(os.path.dirname(os.path.dirname(command[0]))), os.path.abspath(folder))
			self.assertTrue(check._run(command), check.last_output)


class GateTests(unittest.TestCase):
	def test_gate_removes_stale_report_and_runs_tier(self):
		with tempfile.TemporaryDirectory() as folder:
			report = os.path.join(folder, "tools", "gate", "last_report.json")
			os.makedirs(os.path.dirname(report))
			with open(report, "w") as handle:
				handle.write("{}")
			check, run, _ = verifier([0], FakeTree(folder))
			result = check.gate("full")
			self.assertTrue(result["ok"])
			self.assertFalse(os.path.exists(report))
			self.assertIn(os.path.join(folder, "tools", "gate", "verify.ps1"), run.commands[0])
			self.assertEqual(run.commands[0][-2:], ["-Tier", "full"])

	def test_red_gate(self):
		check, _, _ = verifier([1])
		self.assertFalse(check.gate("fast")["ok"])


class RegressionFileTests(unittest.TestCase):
	def test_only_test_paths(self):
		changes = [guard.FileChange("src/tests/a_tests/t.cpp", 1, 0, False), guard.FileChange("src/a.cpp", 1, 0, False),
			guard.FileChange("e2e/scenarios/s.lua", 1, 0, False)]
		self.assertEqual(verification.regression_files(changes), ["src/tests/a_tests/t.cpp", "e2e/scenarios/s.lua"])


if __name__ == "__main__":
	unittest.main()
