#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Keeps the setup's uninstall list in sync with the patch manifest.

The launcher downloads every top-level entry of deploy/patch/source.txt into the install
folder. Uninstall must remove each of them, but it may never delete the folder recursively
(a player may have installed into C:\\Games). So setup.iss lists them one by one, and this
test fails when the manifest gains a top-level entry the list does not cover.

    python tools/tests/test_installer_uninstall_list.py
"""

import importlib.util
import os
import re
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE_TXT = os.path.join(REPO_ROOT, "deploy", "patch", "source.txt")
SETUP_ISS = os.path.join(REPO_ROOT, "deploy", "installer", "setup.iss")


def _load_package_client():
	path = os.path.join(REPO_ROOT, "tools", "release", "package_client.py")
	spec = importlib.util.spec_from_file_location("package_client", path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


# A top-level directory entry sits one tab deep under root: (type = "fs", ..., to = "Data", entries =
_TOP_LEVEL_DIR = re.compile(r'^\t\(type = "fs", from = "[^"]*", to = "([^"/\\]+)", entries', re.MULTILINE)
# An [UninstallDelete] line: Type: files|filesandordirs; Name: "{app}\<name>"
_UNINSTALL_ENTRY = re.compile(r'^Type:\s*(files|filesandordirs);\s*Name:\s*"\{app\}\\([^"]+)"', re.MULTILINE)


def top_level_entries(source_text):
	binaries = _load_package_client().binaries_from_source(source_text)
	directories = _TOP_LEVEL_DIR.findall(source_text)
	return sorted(set(binaries) | set(directories))


def uninstall_names(iss_text):
	section = iss_text.split("[UninstallDelete]", 1)[1].split("\n[", 1)[0]
	return {name for _, name in _UNINSTALL_ENTRY.findall(section)}


class UninstallList(unittest.TestCase):
	def setUp(self):
		with open(SOURCE_TXT, encoding="utf-8") as handle:
			self.source = handle.read()
		with open(SETUP_ISS, encoding="utf-8-sig") as handle:
			self.iss = handle.read()

	def test_manifest_parser_sees_the_known_entries(self):
		# Guards the regexes: if they silently matched nothing, the sync test would pass vacuously.
		entries = top_level_entries(self.source)
		for expected in ("Launcher.exe", "mmo_client.exe", "fmod.dll", "Data"):
			self.assertIn(expected, entries)

	def test_every_downloaded_entry_is_removed_on_uninstall(self):
		listed = uninstall_names(self.iss)
		# Launcher.exe is installed by [Files], so Inno removes it on its own.
		missing = [name for name in top_level_entries(self.source) if name != "Launcher.exe" and name not in listed]
		self.assertEqual([], missing, "add these to [UninstallDelete] in deploy/installer/setup.iss")

	def test_client_runtime_folders_are_removed(self):
		listed = uninstall_names(self.iss)
		for folder in ("Logs", "Config", "Cache"):
			self.assertIn(folder, listed)

	def test_install_folder_is_never_deleted_recursively(self):
		self.assertNotRegex(self.iss, r'(?m)^Type:\s*filesandordirs;\s*Name:\s*"\{app\}"')
		self.assertRegex(self.iss, r'(?m)^Type:\s*dirifempty;\s*Name:\s*"\{app\}"')


if __name__ == "__main__":
	unittest.main()
