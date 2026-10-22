#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Warns when the committed glue scene bake is stale. Warn-only: always exits 0.

The bake (data/client/Interface/GlueUI/GlueBake/GlueBake.json, written by
mmo_edit --bake-glue-scene) records an FNV-1a 64 hash of every source file it was made
from, the hash of GlueScene.json, and the bake rules version of the baker. This prints one
WARNING line per kind of drift and nothing when the bake is fresh. The hash must match
HashGlueBakeBytes in src/shared/game_common/glue_bake.cpp.

    python tools/glue_bake_check.py [--root <repo>]
"""

import argparse
import json
import os
import re
import sys

REBAKE = "rerun: mmo_edit --bake-glue-scene data/client/Interface/GlueUI/GlueScene.json"
MANIFEST = "data/client/Interface/GlueUI/GlueBake/GlueBake.json"
SCENE = "data/client/Interface/GlueUI/GlueScene.json"
HEADER = "src/shared/game_common/glue_bake.h"
CLIENT_DIR = "data/client"

_RULES_RE = re.compile(r"GlueBakeRulesVersion\s*=\s*(\d+)")


def fnv1a64(data):
	"""FNV-1a 64 of the bytes as 16 lowercase hex characters."""
	value = 14695981039346656037
	for byte in data:
		value ^= byte
		value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
	return "%016x" % value


def _hash_file(path):
	try:
		with open(path, "rb") as handle:
			return fnv1a64(handle.read())
	except OSError:
		return None


def _read_rules_version(root):
	try:
		with open(os.path.join(root, HEADER), "r", encoding="utf-8") as handle:
			match = _RULES_RE.search(handle.read())
	except OSError:
		return None
	return int(match.group(1)) if match else None


def check(root):
	"""Returns the warning lines for the tree at root (empty when the bake is fresh)."""
	warnings = []
	manifest_path = os.path.join(root, MANIFEST)
	if not os.path.isfile(manifest_path):
		return ["WARNING: glue bake missing: %s not found - %s" % (MANIFEST, REBAKE)]

	try:
		with open(manifest_path, "r", encoding="utf-8") as handle:
			manifest = json.load(handle)
	except (OSError, ValueError):
		return ["WARNING: glue bake unreadable: %s is not valid JSON - %s" % (MANIFEST, REBAKE)]

	client = os.path.join(root, CLIENT_DIR)
	stale = []
	for entry in manifest.get("inputs", []):
		path = entry.get("path", "")
		if _hash_file(os.path.join(client, path)) != entry.get("hash"):
			stale.append(path)
	if stale:
		listed = ", ".join(stale[:5])
		if len(stale) > 5:
			listed += ", ..."
		warnings.append("WARNING: glue bake stale: %d input(s) changed (%s) — %s" % (len(stale), listed, REBAKE))

	scene_hash = _hash_file(os.path.join(root, SCENE))
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
	args = parser.parse_args(argv)

	if hasattr(sys.stdout, "reconfigure"):
		sys.stdout.reconfigure(encoding="utf-8")
	for line in check(args.root):
		print(line)
	return 0


if __name__ == "__main__":
	sys.exit(main())
