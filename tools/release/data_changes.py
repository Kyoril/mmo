#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""What changed in the game data repos between two releases, for the patch notes.

Everything here is best-effort: a release never fails because the data could not be read.
The data repos are public, so a blobless clone fetches only the commits and the few .data
blobs we decode.
"""

import importlib
import os
import subprocess
import sys
import tempfile

# catalog -> (python module, container message, data file)
CATALOGS = {
	"quests": ("quests_pb2", "Quests", "quests.data"),
	"items": ("items_pb2", "Items", "items.data"),
	"spells": ("spells_pb2", "Spells", "spells.data"),
	"units": ("units_pb2", "Units", "units.data"),
	"zones": ("zones_pb2", "Zones", "zones.data"),
}
MAX_NAMES = 25
GIT_TIMEOUT_S = 900


def _git(repo, *args, binary=False):
	result = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, timeout=GIT_TIMEOUT_S)
	if result.returncode != 0:
		raise RuntimeError("git {} failed: {}".format(args[0], result.stderr.decode("utf-8", "replace").strip()[-300:]))
	return result.stdout if binary else result.stdout.decode("utf-8", "replace")


def open_repo(url, dest):
	"""A blobless, checkout-less clone of `url` at `dest` (reused if present)."""
	if not os.path.isdir(os.path.join(dest, ".git")) and not os.path.isfile(os.path.join(dest, "HEAD")):
		os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
		subprocess.run(["git", "clone", "--quiet", "--filter=blob:none", "--no-checkout", url, dest],
			check=True, capture_output=True, timeout=GIT_TIMEOUT_S)
	return dest


def _strip_trailers(body):
	lines = [line for line in body.splitlines() if not line.lower().startswith(("co-authored-by:", "signed-off-by:"))]
	return "\n".join(lines).strip()


def commit_messages(repo, old, new, limit=40):
	"""[{subject, body}] for old..new (newest first); the last few commits without `old`."""
	revs = "{}..{}".format(old, new) if old else new
	text = _git(repo, "log", "--no-merges", "--format=%s%x1f%b%x1e", "-n", str(limit), revs)
	messages = []
	for record in text.split("\x1e"):
		if not record.strip():
			continue
		subject, _, body = record.strip("\n").partition("\x1f")
		messages.append({"subject": subject.strip(), "body": _strip_trailers(body)})
	return messages


def compile_schemas(proto_dir, out_dir):
	"""Compiles the .proto schemas with grpc_tools and returns {catalog: module}, or None if unavailable."""
	try:
		from grpc_tools import protoc
	except ImportError:
		return None
	protos = sorted(name for name in os.listdir(proto_dir) if name.endswith(".proto"))
	args = ["protoc", "-I" + proto_dir, "-I" + os.path.join(os.path.dirname(protoc.__file__), "_proto"),
		"--python_out=" + out_dir] + [os.path.join(proto_dir, name) for name in protos]
	if protoc.main(args) != 0:
		return None
	if out_dir not in sys.path:
		sys.path.insert(0, out_dir)
	return {key: importlib.import_module(module) for key, (module, _, _) in CATALOGS.items()}


def _names(modules, key, blob):
	_, container, _ = CATALOGS[key]
	message = getattr(modules[key], container)()
	message.ParseFromString(blob)
	return {entry.id: entry.name for entry in message.entry}


def diff_names(old, new):
	"""Compares {id: name} maps: {"added": [names], "renamed": [[old, new]]}."""
	# Spell ranks share a name, so names are de-duplicated in id order.
	added = list(dict.fromkeys(new[key] for key in sorted(new) if key not in old))
	renamed = list(dict.fromkeys((old[key], new[key]) for key in sorted(new) if key in old and old[key] != new[key]))
	renamed = [list(pair) for pair in renamed]
	return {"added": added[:MAX_NAMES], "renamed": renamed[:MAX_NAMES], "added_total": len(added)}


def catalog_changes(repo, old, new, modules):
	"""{catalog: diff_names(...)} for every catalog that changed between two data repo commits."""
	changes = {}
	for key, (_, _, file_name) in CATALOGS.items():
		path = "data/" + file_name
		try:
			new_blob = _git(repo, "show", "{}:{}".format(new, path), binary=True)
		except RuntimeError:
			continue
		try:
			old_blob = _git(repo, "show", "{}:{}".format(old, path), binary=True) if old else b""
		except RuntimeError:
			old_blob = b""
		if old_blob == new_blob:
			continue
		diff = diff_names(_names(modules, key, old_blob), _names(modules, key, new_blob))
		if diff["added"] or diff["renamed"]:
			changes[key] = diff
	return changes


def collect(repo_root, previous, commit, workdir, log=print):
	"""Data-repo commit messages and catalog changes between two main-repo commits.

	Returns {"editor_commits": [...], "client_commits": [...], "catalogs": {...}}; any part that
	cannot be read is left empty and logged.
	"""
	from make_release_manifest import submodule_commit

	result = {"editor_commits": [], "client_commits": [], "catalogs": {}}
	urls = _submodule_urls(repo_root)
	for path, key in (("data/editor", "editor_commits"), ("data/client", "client_commits")):
		try:
			new = submodule_commit(repo_root, commit, path)
			old = submodule_commit(repo_root, previous, path) if previous else ""
			if old == new:
				continue
			repo = open_repo(urls[path], os.path.join(workdir, path.replace("/", "_")))
			_git(repo, "fetch", "--quiet", "--filter=blob:none", "origin", new)
			if old:
				_git(repo, "fetch", "--quiet", "--filter=blob:none", "origin", old)
			result[key] = commit_messages(repo, old, new)
			if path == "data/editor":
				modules = compile_schemas(os.path.join(repo_root, "src", "shared", "proto_data"), tempfile.mkdtemp(dir=workdir))
				if modules is None:
					log("data_changes: grpc_tools unavailable, skipping data names")
				else:
					result["catalogs"] = catalog_changes(repo, old, new, modules)
		except Exception as error:  # Best-effort by contract: notes fall back to commit text.
			log("data_changes: {} skipped: {}".format(path, error))
	return result


def _submodule_urls(repo_root):
	text = subprocess.run(["git", "-C", repo_root, "config", "-f", ".gitmodules", "--get-regexp", r"submodule\..*\.url"],
		capture_output=True, text=True).stdout
	urls = {}
	for line in text.splitlines():
		key, _, url = line.partition(" ")
		urls[key[len("submodule."):-len(".url")]] = url.strip()
	return urls
