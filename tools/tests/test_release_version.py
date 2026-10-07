#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/release/version.py and its agreement with cmake/mmo_version.cmake."""

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _load(name):
	path = os.path.join(REPO_ROOT, "tools", "release", name + ".py")
	spec = importlib.util.spec_from_file_location(name, path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


version = _load("version")


def _git(repo, *args):
	return subprocess.run(
		["git", "-C", repo, "-c", "user.name=Test", "-c", "user.email=test@example.com", "-c", "tag.gpgSign=false"] + list(args),
		check=True, capture_output=True, text=True).stdout.strip()


class ParseTag(unittest.TestCase):
	def test_parses_prefixed_semver(self):
		self.assertEqual(version.parse_tag("v0.3.12", "v"), (0, 3, 12))
		self.assertEqual(version.parse_tag("launcher-v1.0.0", "launcher-v"), (1, 0, 0))

	def test_rejects_other_shapes(self):
		for tag in ("v1.2", "build-5", "v1.2.3-rc1", "launcher-v1.0.0", ""):
			self.assertIsNone(version.parse_tag(tag, "v"), tag)


class FromRepo(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.repo = self.tmp.name
		_git(self.repo, "init", "-q")
		for index in range(3):
			_git(self.repo, "commit", "-q", "--allow-empty", "-m", "c{}".format(index))
		_git(self.repo, "tag", "v0.2.1", "HEAD~1")
		_git(self.repo, "tag", "launcher-v1.4.0", "HEAD~2")
		_git(self.repo, "tag", "build-39")

	def tearDown(self):
		self.tmp.cleanup()

	def test_game_version_is_tag_plus_commit_count(self):
		self.assertEqual(version.game_version(self.repo), "0.2.1.3")

	def test_launcher_version_ignores_game_tags_and_count(self):
		self.assertEqual(version.launcher_version(self.repo), "1.4.0")

	def test_newest_reachable_tag_wins(self):
		_git(self.repo, "commit", "-q", "--allow-empty", "-m", "c3")
		_git(self.repo, "tag", "v0.3.0")
		self.assertEqual(version.game_version(self.repo), "0.3.0.4")
		self.assertEqual(version.game_version(self.repo, "HEAD~1"), "0.2.1.3")

	def test_untagged_repo_is_zero(self):
		with tempfile.TemporaryDirectory() as other:
			_git(other, "init", "-q")
			_git(other, "commit", "-q", "--allow-empty", "-m", "only")
			self.assertEqual(version.game_version(other), "0.0.0.1")
			self.assertEqual(version.launcher_version(other), "0.0.0")

	@unittest.skipUnless(shutil.which("cmake"), "cmake not on PATH")
	def test_cmake_agrees(self):
		script = os.path.join(REPO_ROOT, "cmake", "mmo_version.cmake")
		result = subprocess.run(
			["cmake", "-DMMO_VERSION_PRINT_ONLY=ON", "-DMMO_VERSION_SOURCE_DIR=" + self.repo, "-P", script],
			check=True, capture_output=True, text=True)
		self.assertEqual(result.stderr.strip().splitlines()[-1], "0.2.1.3 1.4.0")


if __name__ == "__main__":
	unittest.main()
