#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the fast gate's selection by changed files (tools/gate/select_steps.py). A wrong
"skip" lets a change merge untested, so the cases that must run everything are pinned down."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "gate")))

import select_steps  # noqa: E402


class SelectTests(unittest.TestCase):
	def setUp(self):
		folder = tempfile.TemporaryDirectory()
		self.addCleanup(folder.cleanup)
		self.tests = folder.name
		self.write("test_bug_loop_x.py", 'sys.path.insert(0, os.path.join(ROOT, "tools", "bugs"))\n')
		self.write("test_world_y.py", "import world_fixtures as fx\n")
		self.write("world_fixtures.py", 'sys.path.insert(0, str(REPO / "tools" / "world"))\n')
		self.write("test_checker.py", "import protocol_version_check\n")
		self.write("test_other.py", "import json\n")

	def write(self, name, text):
		with open(os.path.join(self.tests, name), "w", encoding="utf-8") as handle:
			handle.write(text)

	def select(self, *files):
		return select_steps.select(list(files), self.tests)

	def test_cpp_and_unknown_files_run_every_step(self):
		for path in ("src/shared/game/spell.cpp", "CMakeLists.txt", "cmake/mmo_macros.cmake", "deps/json",
				".github/workflows/ccpp.yml", "e2e/scenarios/a.lua", "bruno/x.bru"):
			result = self.select("tools/bugs/bugloop/loop.py", path)
			self.assertTrue(result["cpp"], path)
			self.assertEqual(result["tool_tests"], "all", path)
			self.assertTrue(result["deployer"], path)

	def test_git_trouble_runs_every_step(self):
		result = select_steps.select(None, self.tests)
		self.assertEqual((result["cpp"], result["tool_tests"], result["deployer"]), (True, "all", True))

	def test_documentation_alone_runs_nothing_more(self):
		result = self.select("docs/bug-loop.md", "README.md", "src/shared/game/README.md")
		self.assertEqual((result["cpp"], result["tool_tests"], result["deployer"]), (False, "none", False))

	def test_a_tool_area_runs_its_tests(self):
		self.assertEqual(self.select("tools/bugs/bugloop/loop.py")["tool_tests"], ["test_bug_loop_x"])
		self.assertFalse(self.select("tools/bugs/bugloop/loop.py")["cpp"])

	def test_a_helper_counts_for_its_importers(self):
		self.assertEqual(self.select("tools/world/worldkit/query.py")["tool_tests"], ["test_world_y"])
		self.assertEqual(self.select("tools/tests/world_fixtures.py")["tool_tests"], ["test_world_y"])

	def test_a_top_level_script_counts_for_its_importers(self):
		self.assertEqual(self.select("tools/protocol_version_check.py")["tool_tests"], ["test_checker"])

	def test_a_changed_test_runs_itself(self):
		self.assertEqual(self.select("tools/tests/test_other.py")["tool_tests"], ["test_other"])

	def test_data_and_the_gate_run_every_tool_test(self):
		for path in ("data/client", "data/editor", "tools/gate/verify.ps1"):
			result = self.select(path)
			self.assertEqual(result["tool_tests"], "all", path)
			self.assertFalse(result["cpp"], path)

	def test_deploy_runs_the_deployer_tests(self):
		result = self.select("deploy/deployer/mmo_deployer/app.py")
		self.assertEqual((result["cpp"], result["tool_tests"], result["deployer"]), (False, "none", True))


class ChangedFilesTests(unittest.TestCase):
	def test_unknown_base_means_no_answer(self):
		self.assertIsNone(select_steps.changed_files("refs/heads/does-not-exist-anywhere"))


if __name__ == "__main__":
	unittest.main()
