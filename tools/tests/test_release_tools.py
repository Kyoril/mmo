#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/release/package_client.py and tools/release/make_release_manifest.py."""

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _load(name):
	path = os.path.join(REPO_ROOT, "tools", "release", name + ".py")
	spec = importlib.util.spec_from_file_location(name, path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


package_client = _load("package_client")
make_release_manifest = _load("make_release_manifest")


def _git(repo, *args):
	return subprocess.run(
		["git", "-C", repo, "-c", "user.name=Test", "-c", "user.email=test@example.com"] + list(args),
		check=True, capture_output=True, text=True).stdout.strip()


class PackageClient(unittest.TestCase):
	def test_real_source_txt_names_the_shipped_binaries(self):
		with open(os.path.join(REPO_ROOT, "deploy", "patch", "source.txt"), encoding="utf-8") as handle:
			names = package_client.binaries_from_source(handle.read())
		self.assertEqual(names, ["Launcher.exe", "fmod.dll", "mmo_client.exe", "mmo_error.exe"])

	def test_package_zips_source_and_binaries(self):
		with tempfile.TemporaryDirectory() as tmp:
			bin_dir = os.path.join(tmp, "bin")
			os.makedirs(bin_dir)
			for name in ("a.exe", "b.dll", "unrelated.exe"):
				with open(os.path.join(bin_dir, name), "wb") as handle:
					handle.write(name.encode())
			source = os.path.join(tmp, "source.txt")
			with open(source, "w", encoding="utf-8") as handle:
				handle.write('root = (entries = { (type = "fs", from = "a.exe") (type = "fs", from = "b.dll") (type = "fs", from = "../data", to = "Data") })')
			out = os.path.join(tmp, "client-bin.zip")

			names = package_client.package(bin_dir, source, out)

			self.assertEqual(names, ["a.exe", "b.dll"])
			with zipfile.ZipFile(out) as archive:
				self.assertEqual(sorted(archive.namelist()), ["a.exe", "b.dll", "source.txt"])

	def test_missing_binary_is_an_error(self):
		with tempfile.TemporaryDirectory() as tmp:
			source = os.path.join(tmp, "source.txt")
			with open(source, "w", encoding="utf-8") as handle:
				handle.write('(type = "fs", from = "missing.exe")')
			with self.assertRaises(FileNotFoundError) as ctx:
				package_client.package(tmp, source, os.path.join(tmp, "out.zip"))
			self.assertIn("missing.exe", str(ctx.exception))


class ReleaseManifest(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.repo = self.tmp.name
		_git(self.repo, "init", "-q")
		_git(self.repo, "commit", "-q", "--allow-empty", "-m", "first")
		self.first = _git(self.repo, "rev-parse", "HEAD")
		_git(self.repo, "update-index", "--add", "--cacheinfo", "160000," + "1" * 40 + ",data/client")
		_git(self.repo, "update-index", "--add", "--cacheinfo", "160000," + "2" * 40 + ",data/editor")
		_git(self.repo, "commit", "-q", "-m", "fix(loot): roll on the right table")
		_git(self.repo, "commit", "-q", "--allow-empty", "-m", "feat(chat): emotes")
		self.head = _git(self.repo, "rev-parse", "HEAD")

	def tearDown(self):
		self.tmp.cleanup()

	def test_changes_since_previous_exclude_older_commits(self):
		changes = make_release_manifest.collect_changes(self.repo, self.first, self.head)
		self.assertEqual(changes, ["feat(chat): emotes", "fix(loot): roll on the right table"])

	def test_changes_without_previous_are_capped(self):
		changes = make_release_manifest.collect_changes(self.repo, "", self.head, limit=2)
		self.assertEqual(len(changes), 2)

	def test_submodule_commits(self):
		self.assertEqual(make_release_manifest.submodule_commit(self.repo, self.head, "data/client"), "1" * 40)
		self.assertEqual(make_release_manifest.submodule_commit(self.repo, self.head, "data/editor"), "2" * 40)

	def test_build_manifest(self):
		asset = os.path.join(self.repo, "client-bin.zip")
		with open(asset, "wb") as handle:
			handle.write(b"zipdata")
		report = {"passed": True, "steps": [{"name": "e2e", "passed": True, "exit_code": 0, "log": "x", "duration_s": 12.5}]}

		manifest = make_release_manifest.build_manifest(
			self.repo, self.head, self.first, {"client-bin.zip": asset}, report, "2026-10-07T00:52:11Z")

		self.assertEqual(manifest["schema"], 1)
		self.assertEqual(manifest["commit"], self.head)
		self.assertEqual(manifest["image_tag"], self.head)
		self.assertEqual(manifest["data_client_commit"], "1" * 40)
		self.assertEqual(manifest["assets"]["client-bin.zip"]["sha256"], hashlib.sha256(b"zipdata").hexdigest())
		self.assertEqual(manifest["gate"], {"passed": True, "steps": [{"name": "e2e", "passed": True, "duration_s": 12.5}]})
		self.assertEqual(len(manifest["changes"]), 2)
		self.assertIsNone(manifest["version"])
		self.assertIsNone(manifest["patch_notes"])
		json.dumps(manifest)

	def test_main_embeds_version_and_patch_notes(self):
		notes = {"headline": "h", "sections": [{"title": "General", "bullets": ["a"]}], "source": "ai", "model": "m"}
		notes_path = os.path.join(self.repo, "patch_notes.json")
		with open(notes_path, "w", encoding="utf-8") as handle:
			json.dump(notes, handle)
		out = os.path.join(self.repo, "release.json")

		make_release_manifest.main(["--repo", self.repo, "--commit", self.head, "--previous", self.first,
			"--version", "0.3.0.4", "--patch-notes", notes_path, "--out", out])

		with open(out, encoding="utf-8") as handle:
			manifest = json.load(handle)
		self.assertEqual(manifest["version"], "0.3.0.4")
		self.assertEqual(manifest["patch_notes"], notes)


class SmokeTestUsesRealSource(unittest.TestCase):
	def test_smoke_script_embeds_source_txt(self):
		with open(os.path.join(REPO_ROOT, "deploy", "patch", "source.txt"), encoding="utf-8") as handle:
			source = handle.read().strip()
		with open(os.path.join(REPO_ROOT, "deploy", "deployer", "smoke_update_compiler.sh"), encoding="utf-8") as handle:
			script = handle.read()
		self.assertIn(source, script)


if __name__ == "__main__":
	unittest.main()
