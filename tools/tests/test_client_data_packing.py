#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Every data/client folder the client loads has to reach players.

Locally the client reads loose files from data/client, so a folder that is missing from
deploy/patch/source.txt or from the client's archive list works fine on a dev machine and is
simply absent in every launcher install (that is how Kits went missing). This pins both lists
to the folders that actually exist.

    python tools/tests/test_client_data_packing.py
"""

import os
import re
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLIENT_DATA = os.path.join(ROOT, "data", "client")
PATCH_SOURCE = os.path.join(ROOT, "deploy", "patch", "source.txt")
CLIENT_CONSOLE = os.path.join(ROOT, "src", "mmo_client", "console", "console.cpp")

# Cache is filled by the client at runtime, Editor holds editor-only assets.
NOT_SHIPPED = {"Cache", "Editor"}

# Packed as a folder of per-locale archives that the client picks by cvar.
LOCALES = "Locales"


def read(path):
	with open(path, encoding="utf-8") as f:
		return f.read()


def patch_archives():
	"""Maps each packed top-level folder to the archive it is packed into."""
	entries = re.findall(r'type = "hpak2", from = "([^"]+)", to = "([^"]+)", sub = "[^"]+"', read(PATCH_SOURCE))
	return dict(entries)


def client_archives():
	match = re.search(r"AssetRegistry::Initialize\([^{]*\{(.*?)\}\);", read(CLIENT_CONSOLE), re.S)
	return set(re.findall(r'"([^"]+\.hpak)"', match.group(1))) if match else set()


@unittest.skipUnless(os.path.isdir(CLIENT_DATA) and os.listdir(CLIENT_DATA), "data/client submodule not checked out")
class ClientDataPackingTests(unittest.TestCase):
	def shipped_folders(self):
		return sorted(name for name in os.listdir(CLIENT_DATA)
			if os.path.isdir(os.path.join(CLIENT_DATA, name)) and not name.startswith(".")
			and name not in NOT_SHIPPED and name != LOCALES)

	def test_every_folder_is_packed_into_the_patch(self):
		packed = patch_archives()
		missing = [name for name in self.shipped_folders() if name not in packed]
		self.assertEqual(missing, [], "data/client folders missing from deploy/patch/source.txt")

	def test_every_packed_archive_is_mounted_by_the_client(self):
		mounted = client_archives()
		self.assertTrue(mounted, "could not find the archive list in console.cpp")
		missing = sorted(archive for archive in patch_archives().values() if archive not in mounted)
		self.assertEqual(missing, [], "archives packed by the patch but not mounted in console.cpp")

	def test_every_locale_is_packed(self):
		source = read(PATCH_SOURCE)
		locales = sorted(name for name in os.listdir(os.path.join(CLIENT_DATA, LOCALES))
			if os.path.isdir(os.path.join(CLIENT_DATA, LOCALES, name)))
		missing = [name for name in locales if f'from = "{name}", to = "{name}.hpak"' not in source]
		self.assertEqual(missing, [], "locales missing from deploy/patch/source.txt")


if __name__ == "__main__":
	unittest.main()
