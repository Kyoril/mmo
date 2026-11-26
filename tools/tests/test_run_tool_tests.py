#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the gate's parallel tool-test runner (tools/gate/run_tool_tests.py): how it cuts the
discovered tests into shards, and that a failing shard fails the step."""

import os
import subprocess
import sys
import tempfile
import textwrap
import unittest

GATE_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "gate"))
sys.path.insert(0, GATE_DIR)

import run_tool_tests  # noqa: E402


class Independent(unittest.TestCase):
	def test_a(self):
		pass

	def test_b(self):
		pass

	def test_c(self):
		pass

	def test_d(self):
		pass


class SharedFixture(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.value = 1

	def test_a(self):
		pass

	def test_b(self):
		pass

	def test_c(self):
		pass

	def test_d(self):
		pass


class PlanShardsTests(unittest.TestCase):
	def plan(self, *classes, chunk=3):
		loader = unittest.defaultTestLoader
		return run_tool_tests.plan_shards(unittest.TestSuite(loader.loadTestsFromTestCase(cls) for cls in classes), chunk)

	def test_a_class_without_a_fixture_is_split_into_chunks(self):
		shards = self.plan(Independent)
		self.assertEqual([len(shard) for shard in shards], [3, 1])
		self.assertTrue(all(".Independent.test_" in test_id for shard in shards for test_id in shard))

	def test_a_class_with_set_up_class_stays_one_shard(self):
		# Its fixture is built once per shard; splitting would build it once per chunk.
		self.assertEqual([len(shard) for shard in self.plan(SharedFixture)], [4])

	def test_every_test_is_planned_once(self):
		shards = self.plan(Independent, SharedFixture, chunk=2)
		ids = [test_id for shard in shards for test_id in shard]
		self.assertEqual(len(ids), 8)
		self.assertEqual(len(set(ids)), 8)


class RunnerTests(unittest.TestCase):
	def run_runner(self, files):
		with tempfile.TemporaryDirectory() as root:
			start = os.path.join(root, "tests")
			os.makedirs(start)
			for name, text in files.items():
				with open(os.path.join(start, name), "w", encoding="utf-8") as handle:
					handle.write(textwrap.dedent(text))
			return subprocess.run([sys.executable, os.path.join(GATE_DIR, "run_tool_tests.py"), "--start-dir", "tests",
				"--jobs", "2"], cwd=root, capture_output=True, text=True)

	def test_green_tests_pass(self):
		proc = self.run_runner({"test_ok.py": """
			import unittest
			class T(unittest.TestCase):
				def test_one(self):
					pass
			"""})
		self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
		self.assertIn("Ran 1 tests", proc.stdout)

	def test_a_failure_or_an_import_error_fails_the_step(self):
		proc = self.run_runner({
			"test_ok.py": """
				import unittest
				class T(unittest.TestCase):
					def test_one(self):
						pass
				""",
			"test_bad.py": """
				import unittest
				class T(unittest.TestCase):
					def test_one(self):
						self.assertEqual(1, 2)
				""",
			"test_broken.py": "import no_such_module_anywhere\n"})
		self.assertEqual(proc.returncode, 1)
		self.assertIn("test_bad.T.test_one", proc.stdout)
		self.assertIn("no_such_module_anywhere", proc.stdout)


if __name__ == "__main__":
	unittest.main()
