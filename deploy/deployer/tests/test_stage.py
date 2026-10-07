# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from mmo_deployer.patchdir import PatchDir
from mmo_deployer.stage import COMPILER_TIMEOUT_S, GIT_TIMEOUT_S, StageError, Stager, select_release
from mmo_deployer.state import State

from .fakes import CAN_SYMLINK, FakeGitHub, make_config, make_git_repo, make_launcher_assets, make_release_assets

FAKE_COMPILER = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_update_compiler.py")]
COMMIT = "c" * 40


def _releases(*commits):
	return [{"tag_name": "nightly-{}".format(c[:8]), "target_commitish": c, "created_at": "2026-10-0{}".format(i)} for i, c in enumerate(commits)][::-1]


class SelectRelease(unittest.TestCase):
	def test_newest(self):
		self.assertEqual(select_release(_releases("a", "b"), State())["target_commitish"], "b")

	def test_nothing_published(self):
		self.assertIsNone(select_release([], State()))

	def test_newest_already_live_or_staged(self):
		self.assertIsNone(select_release(_releases("a", "b"), State(live="b")))
		self.assertIsNone(select_release(_releases("a", "b"), State(staged="b")))

	def test_bad_newest_release_does_not_fall_back_to_older(self):
		self.assertIsNone(select_release(_releases("a", "b"), State(live="z", bad=["b"])))


class PatchDirTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.dir = PatchDir(self.tmp.name)
		for sha in ("one", "two", "three", "four"):
			self.dir.release_path(sha).mkdir(parents=True)
			time.sleep(0.01)
		self.dir.tmp_path("five").mkdir()

	def tearDown(self):
		self.tmp.cleanup()

	def test_prune_keeps_newest_and_protected(self):
		removed = self.dir.prune(2, {"one"})
		self.assertEqual(sorted(removed), ["two"])
		self.assertTrue(self.dir.has_release("one"))
		self.assertTrue(self.dir.tmp_path("five").exists())

	def test_flip_missing_release(self):
		with self.assertRaises(FileNotFoundError):
			self.dir.flip_current("nope")

	def test_no_current_link(self):
		self.assertIsNone(self.dir.current_sha())

	@unittest.skipUnless(CAN_SYMLINK, "needs symlink support")
	def test_flip_is_relative_and_replaces(self):
		self.dir.flip_current("one")
		self.dir.flip_current("two")
		self.assertEqual(self.dir.current_sha(), "two")
		self.assertEqual(os.readlink(os.path.join(self.tmp.name, "current")), os.path.join("releases", "two"))
		self.assertFalse(os.path.lexists(os.path.join(self.tmp.name, "current.new")))


class StagerTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		root = Path(self.tmp.name)
		self.data_commit = make_git_repo(root / "mmo-data", {"Worlds/map.txt": "terrain"})
		self.cfg = make_config(root, data_repo_url=str(root / "mmo-data"))
		self.github = FakeGitHub()
		self.patchdir = PatchDir(self.cfg.patch_root)
		self.stager = Stager(self.cfg, self.github, self.patchdir, FAKE_COMPILER)

	def tearDown(self):
		self.tmp.cleanup()

	def _release(self, **kwargs):
		assets = make_release_assets(kwargs.pop("commit", COMMIT), kwargs.pop("data_commit", self.data_commit), **kwargs)
		return self.github.add_release("nightly-20261007-cccccccc", COMMIT, assets, "2026-10-07T00:50:00Z")

	def _assert_nothing_left(self):
		self.assertFalse(self.patchdir.has_release(COMMIT))
		self.assertFalse(self.patchdir.tmp_path(COMMIT).exists())

	def test_stage_builds_release(self):
		manifest = self.stager.stage(self._release())
		self.assertEqual(manifest["commit"], COMMIT)
		listing = (self.patchdir.release_path(COMMIT) / "list.txt").read_text(encoding="utf-8")
		self.assertIn('"mmo_client.exe"', listing)
		self.assertIn('"source.txt"', listing)
		self.assertFalse(self.patchdir.tmp_path(COMMIT).exists())
		self.assertTrue((self.stager.data_checkout / "Worlds" / "map.txt").is_file())

	def test_nightly_launcher_ships_until_a_launcher_release_exists(self):
		manifest = self.stager.stage(self._release())
		self.assertIsNone(manifest["launcher_version"])
		self.assertEqual((self.stager.patch_source / "Launcher.exe").read_bytes(), b"launcher")

	def test_newest_launcher_release_replaces_the_nightly_build(self):
		self.github.add_release("launcher-v1.0.0", "e" * 40, make_launcher_assets("1.0.0", b"old launcher"), "2026-09-01T00:00:00Z")
		self.github.add_release("launcher-v1.1.0", "f" * 40, make_launcher_assets("1.1.0"), "2026-10-01T00:00:00Z")
		manifest = self.stager.stage(self._release())
		self.assertEqual(manifest["launcher_version"], "1.1.0")
		self.assertEqual((self.stager.patch_source / "Launcher.exe").read_bytes(), b"pinned launcher")
		self.assertTrue(self.patchdir.has_release(COMMIT))

	def test_launcher_checksum_mismatch_fails_staging(self):
		self.github.add_release("launcher-v1.0.0", "f" * 40, make_launcher_assets("1.0.0", sha_override="0" * 64), "2026-10-01T00:00:00Z")
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release())
		self.assertIn("Launcher.exe", str(ctx.exception))
		self._assert_nothing_left()

	def test_restage_replaces_existing_release(self):
		self.stager.stage(self._release())
		marker = self.patchdir.release_path(COMMIT) / "stale"
		marker.write_text("x")
		self.stager.stage(self.github.releases[0])
		self.assertFalse(marker.exists())

	def test_checksum_mismatch(self):
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release(sha_override="0" * 64))
		self.assertIn("checksum", str(ctx.exception))
		self._assert_nothing_left()

	def test_manifest_commit_mismatch(self):
		with self.assertRaises(StageError):
			self.stager.stage(self._release(commit="d" * 40))
		self._assert_nothing_left()

	def test_compiler_failure(self):
		with mock.patch.dict(os.environ, {"FAKE_COMPILER_FAIL": "1"}):
			with self.assertRaises(StageError) as ctx:
				self.stager.stage(self._release())
		self.assertIn("simulated compiler failure", str(ctx.exception))
		self._assert_nothing_left()

	def test_list_missing_a_binary(self):
		with mock.patch.dict(os.environ, {"FAKE_COMPILER_OMIT": "Launcher.exe"}):
			with self.assertRaises(StageError) as ctx:
				self.stager.stage(self._release())
		self.assertIn("Launcher.exe", str(ctx.exception))
		self._assert_nothing_left()

	def test_unknown_data_commit_fails_cleanly(self):
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release(data_commit="e" * 40))
		self.assertIn("git", str(ctx.exception))
		self._assert_nothing_left()

	def test_zip_without_source_txt(self):
		with self.assertRaises(StageError):
			self.stager.stage(self._release(source_txt=None))
		self._assert_nothing_left()

	def _recording_run(self, timeout_on=None):
		calls = []

		def run(command, **kwargs):
			calls.append((command, kwargs))
			if timeout_on and timeout_on in command:
				raise subprocess.TimeoutExpired(command, kwargs.get("timeout"))
			return subprocess.run(command, **kwargs)

		self.stager.run = run
		return calls

	def test_git_runs_without_prompts_and_with_timeouts(self):
		calls = self._recording_run()
		self.stager.stage(self._release())
		git_calls = [(c, k) for c, k in calls if c[0] == "git"]
		self.assertGreaterEqual(len(git_calls), 3)
		for command, kwargs in git_calls:
			self.assertEqual(command[1:5], ["-c", "http.lowSpeedLimit=1000", "-c", "http.lowSpeedTime=300"])
			self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")
			self.assertEqual(kwargs["timeout"], GIT_TIMEOUT_S)
		compiler = [(c, k) for c, k in calls if c[0] != "git"]
		self.assertEqual(len(compiler), 1)
		self.assertEqual(compiler[0][1]["timeout"], COMPILER_TIMEOUT_S)

	def test_git_timeout_is_a_stage_error(self):
		self._recording_run(timeout_on="fetch")
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release())
		self.assertIn("git fetch timed out after {} s".format(GIT_TIMEOUT_S), str(ctx.exception))
		self._assert_nothing_left()

	def test_compiler_timeout_is_a_stage_error(self):
		self._recording_run(timeout_on="zlib")
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release())
		self.assertIn("timed out after {} s".format(COMPILER_TIMEOUT_S), str(ctx.exception))
		self._assert_nothing_left()

	def test_ungated_release_is_refused(self):
		import json as json_module
		assets = make_release_assets(COMMIT, self.data_commit)
		release = self.github.add_release("nightly-20261007-cccccccc", COMMIT, assets, "2026-10-07T00:50:00Z")
		# Patch the release.json bytes to have no gate
		manifest = json_module.loads(assets["release.json"].decode("utf-8"))
		manifest["gate"] = None
		self.github.assets[(release["tag_name"], "release.json")] = json_module.dumps(manifest).encode("utf-8")
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(release)
		self.assertIn("gate", str(ctx.exception))
		self._assert_nothing_left()


if __name__ == "__main__":
	unittest.main()
