#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Warns when the committed glue scene bake is stale. Warn-only: always exits 0.

The bake (data/client/Interface/GlueUI/GlueBake/GlueBake.json, written by
mmo_edit --bake-glue-scene) records an FNV-1a 64 hash of every source file it was made
from, the hash of GlueScene.json, and the bake rules version of the baker. This prints one
WARNING line per kind of drift and nothing when the bake is fresh. It also warns when a file the
bake wrote (listed in the manifest under Interface/GlueUI/GlueBake/) is missing. The hash must
match HashGlueBakeBytes in src/shared/game_common/glue_bake.cpp.

Hashes are cached in tools/gate/reports/glue_bake_hash_cache.json (gitignored), keyed by path,
size and modification time, so only changed files are hashed again. A missing or unreadable
cache is simply rebuilt.

    python tools/glue_bake_check.py [--root <repo>] [--cache <file>]
"""

import argparse
import json
import os
import re
import sys
import time

REBAKE = "rerun: mmo_edit --bake-glue-scene data/client/Interface/GlueUI/GlueScene.json"
MANIFEST = "data/client/Interface/GlueUI/GlueBake/GlueBake.json"
SCENE = "data/client/Interface/GlueUI/GlueScene.json"
HEADER = "src/shared/game_common/glue_bake.h"
CLIENT_DIR = "data/client"
CACHE = "tools/gate/reports/glue_bake_hash_cache.json"
OUTPUT_DIR = "Interface/GlueUI/GlueBake/"

# Files modified this recently are not cached: a write in the same timestamp tick could change the
# content without changing size or modification time.
_RACY_NS = 2 * 1000 * 1000 * 1000

_RULES_RE = re.compile(r"GlueBakeRulesVersion\s*=\s*(\d+)")


def fnv1a64(data):
	"""FNV-1a 64 of the bytes as 16 lowercase hex characters."""
	value = 14695981039346656037
	for byte in data:
		value ^= byte
		value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
	return "%016x" % value


class HashCache:
	"""FNV-1a hashes of files, reused while a file's size and modification time stay the same."""

	def __init__(self, path):
		self.path = path
		self.entries = {}
		self.used = {}
		self.dirty = False
		if not path:
			return
		try:
			with open(path, "r", encoding="utf-8") as handle:
				loaded = json.load(handle)
		except (OSError, ValueError):
			return
		if isinstance(loaded, dict) and isinstance(loaded.get("files"), dict):
			self.entries = loaded["files"]

	def hash_file(self, path):
		"""Hash of the file at path, or None when it cannot be read."""
		key = os.path.normcase(os.path.abspath(path))
		try:
			stat = os.stat(path)
		except OSError:
			return None

		entry = self.entries.get(key)
		if (isinstance(entry, dict) and entry.get("size") == stat.st_size
				and entry.get("mtime_ns") == stat.st_mtime_ns and isinstance(entry.get("hash"), str)):
			self.used[key] = entry
			return entry["hash"]

		try:
			with open(path, "rb") as handle:
				value = fnv1a64(handle.read())
		except OSError:
			return None

		self.dirty = True
		if stat.st_mtime_ns < time.time_ns() - _RACY_NS:
			self.used[key] = {"size": stat.st_size, "mtime_ns": stat.st_mtime_ns, "hash": value}
		return value

	def save(self):
		"""Writes the entries used by this run. Failure is ignored: the cache only saves time."""
		if not self.path or (not self.dirty and len(self.used) == len(self.entries)):
			return
		temp = self.path + ".tmp"
		try:
			os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
			with open(temp, "w", encoding="utf-8") as handle:
				json.dump({"version": 1, "files": self.used}, handle)
			os.replace(temp, self.path)
		except OSError:
			try:
				os.remove(temp)
			except OSError:
				pass


def _owned_outputs(manifest):
	"""Paths of the files the bake wrote that the manifest lists (relative to data/client)."""
	paths = []

	def add(value):
		if isinstance(value, str) and value.replace("\\", "/").startswith(OUTPUT_DIR):
			paths.append(value)
			if value.lower().endswith(".hmi"):
				# The far albedo texture written next to its material instance
				paths.append(value[:-4] + ".htex")

	for section, fields in (("meshes", ("mesh",)), ("terrainTiles", ("mesh", "material", "splatting")),
							("water", ("mesh",))):
		entries = manifest.get(section, [])
		if not isinstance(entries, list):
			continue
		for entry in entries:
			if isinstance(entry, dict):
				for name in fields:
					add(entry.get(name))
	return list(dict.fromkeys(paths))


def _read_rules_version(root):
	try:
		with open(os.path.join(root, HEADER), "r", encoding="utf-8") as handle:
			match = _RULES_RE.search(handle.read())
	except OSError:
		return None
	return int(match.group(1)) if match else None


def check(root, cache_path=None):
	"""Returns the warning lines for the tree at root (empty when the bake is fresh).

	cache_path is the hash cache file; None uses the default cache inside root, "" disables it.
	"""
	cache = HashCache(os.path.join(root, CACHE) if cache_path is None else cache_path)
	try:
		return _check(root, cache)
	finally:
		cache.save()


def _check(root, cache):
	warnings = []
	manifest_path = os.path.join(root, MANIFEST)
	if not os.path.isfile(manifest_path):
		return ["WARNING: glue bake missing: %s not found - %s" % (MANIFEST, REBAKE)]

	try:
		with open(manifest_path, "r", encoding="utf-8") as handle:
			manifest = json.load(handle)
	except (OSError, ValueError):
		return ["WARNING: glue bake unreadable: %s is not valid JSON - %s" % (MANIFEST, REBAKE)]

	malformed = "WARNING: glue bake manifest malformed: %s - %s" % (MANIFEST, REBAKE)
	if not isinstance(manifest, dict):
		return [malformed]

	inputs = manifest.get("inputs", [])
	if not isinstance(inputs, list):
		return [malformed]

	client = os.path.join(root, CLIENT_DIR)
	stale = []
	bad_entries = 0
	for entry in inputs:
		if not isinstance(entry, dict) or not isinstance(entry.get("path"), str) or not isinstance(entry.get("hash"), str):
			bad_entries += 1
			continue
		path = entry["path"]
		if cache.hash_file(os.path.join(client, path)) != entry["hash"]:
			stale.append(path)
	if bad_entries:
		warnings.append(malformed)
	if stale:
		listed = ", ".join(stale[:5])
		if len(stale) > 5:
			listed += ", ..."
		warnings.append("WARNING: glue bake stale: %d input(s) changed (%s) — %s" % (len(stale), listed, REBAKE))

	missing = [path for path in _owned_outputs(manifest) if not os.path.isfile(os.path.join(client, path))]
	if missing:
		listed = ", ".join(missing[:5])
		if len(missing) > 5:
			listed += ", ..."
		warnings.append("WARNING: glue bake incomplete: %d output(s) missing (%s) — %s" % (len(missing), listed, REBAKE))

	scene_hash = cache.hash_file(os.path.join(root, SCENE))
	if scene_hash != manifest.get("sceneHash"):
		warnings.append("WARNING: glue bake stale: GlueScene.json changed since the bake — %s" % REBAKE)

	expected = _read_rules_version(root)
	if expected is not None and manifest.get("rulesVersion") != expected:
		warnings.append("WARNING: glue bake stale: bake rules version %s != %s — %s"
						% (manifest.get("rulesVersion"), expected, REBAKE))

	return warnings


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
						help="repository root (default: this checkout)")
	parser.add_argument("--cache", default=None,
						help="hash cache file (default: <root>/%s; empty disables it)" % CACHE)
	args = parser.parse_args(argv)

	if hasattr(sys.stdout, "reconfigure"):
		sys.stdout.reconfigure(encoding="utf-8")
	for line in check(args.root, args.cache):
		print(line)
	return 0


if __name__ == "__main__":
	sys.exit(main())
