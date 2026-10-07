#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Writes release.json, the contract between the nightly release workflow and the deployer."""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys

DEFAULT_CHANGE_LIMIT = 200
FIRST_RELEASE_CHANGES = 20


def _git(repo, *args):
	return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True, text=True).stdout


def submodule_commit(repo, commit, path):
	# "160000 commit <sha>\t<path>"
	return _git(repo, "ls-tree", commit, path).split()[2]


def collect_changes(repo, previous, commit, limit=DEFAULT_CHANGE_LIMIT):
	"""Non-merge commit subjects since `previous` (newest first); the last few if there is none."""
	if previous:
		args = ["log", "--no-merges", "--format=%s", "{}..{}".format(previous, commit)]
	else:
		args = ["log", "--no-merges", "--format=%s", "-n", str(FIRST_RELEASE_CHANGES), commit]
	lines = [line.strip() for line in _git(repo, *args).splitlines() if line.strip()]
	return lines[:limit]


def sha256_file(path):
	digest = hashlib.sha256()
	with open(path, "rb") as handle:
		for chunk in iter(lambda: handle.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def build_manifest(repo, commit, previous, assets, gate_report, created_at, version=None, patch_notes=None):
	gate = None
	if gate_report is not None:
		gate = {
			"passed": bool(gate_report.get("passed")),
			"steps": [{"name": s["name"], "passed": s["passed"], "duration_s": s["duration_s"]} for s in gate_report.get("steps", [])],
		}
	return {
		"schema": 1,
		"commit": commit,
		"image_tag": commit,
		"data_client_commit": submodule_commit(repo, commit, "data/client"),
		"data_editor_commit": submodule_commit(repo, commit, "data/editor"),
		"created_at": created_at,
		"assets": {name: {"sha256": sha256_file(path), "size": os.path.getsize(path)} for name, path in assets.items()},
		"gate": gate,
		"changes": collect_changes(repo, previous, commit),
		# Optional for the deployer: older releases carry neither.
		"version": version or None,
		"patch_notes": patch_notes,
	}


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", default=".")
	parser.add_argument("--commit", required=True)
	parser.add_argument("--previous", default="")
	parser.add_argument("--asset", action="append", default=[], help="NAME=PATH")
	parser.add_argument("--gate-report")
	parser.add_argument("--version", help="game version, tools/release/version.py")
	parser.add_argument("--patch-notes", help="patch_notes.json from tools/release/patch_notes.py")
	parser.add_argument("--out", required=True)
	args = parser.parse_args(argv)

	assets = dict(item.split("=", 1) for item in args.asset)
	report = None
	if args.gate_report:
		with open(args.gate_report, encoding="utf-8-sig") as handle:
			report = json.load(handle)
		if not report.get("passed"):
			print("gate report is not green; refusing to write a release manifest", file=sys.stderr)
			return 1
	created_at = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
	notes = None
	if args.patch_notes:
		with open(args.patch_notes, encoding="utf-8") as handle:
			notes = json.load(handle)
	manifest = build_manifest(args.repo, args.commit, args.previous, assets, report, created_at, args.version, notes)
	with open(args.out, "w", encoding="utf-8") as handle:
		json.dump(manifest, handle, indent=2)
	print("wrote {} ({} changes)".format(args.out, len(manifest["changes"])))
	return 0


if __name__ == "__main__":
	sys.exit(main())
