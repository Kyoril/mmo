# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Field-level diff of the protobuf game data files (data/editor/data/*.data), so the guard
can tell a typo fix from a numeric change."""

import glob
import importlib
import os
import re
import subprocess
import sys
import tempfile

from google.protobuf.unknown_fields import UnknownFieldSet

# Repeated message elements are keyed by the first of these fields they have, so reordering a
# list is not reported as a change. LocalizedString entries are keyed by locale.
KEY_FIELDS = ("id", "key", "locale")
_ENTRY = re.compile(r"^[^.\[]+\[[^\]]*\]")
_LOC_PATH = re.compile(r"(^|\.)[A-Za-z0-9_]+_loc\[")


def _is_repeated(field):
	flag = getattr(field, "is_repeated", None)
	if flag is None:
		return field.label == field.LABEL_REPEATED
	return flag() if callable(flag) else flag


def _is_map(field):
	return field.message_type is not None and field.message_type.GetOptions().map_entry


def _element_key(element, index):
	fields = element.DESCRIPTOR.fields_by_name
	for name in KEY_FIELDS:
		if name in fields:
			return "{}={}".format(name, getattr(element, name))
	return str(index)


def _unknown_value(data):
	"""Unknown groups nest; flatten them to a deterministic tuple so they compare by value."""
	if isinstance(data, UnknownFieldSet):
		return tuple((entry.field_number, entry.wire_type, _unknown_value(entry.data)) for entry in data)
	return data


def leaf_items(message, prefix=""):
	"""Flattens the set fields of a protobuf message into {path: scalar value}."""
	items = {}
	for field, value in message.ListFields():
		path = prefix + field.name
		if _is_map(field):
			for key in value:
				element = value[key]
				if hasattr(element, "ListFields"):
					items.update(leaf_items(element, "{}[{}].".format(path, key)))
				else:
					items["{}[{}]".format(path, key)] = element
		elif _is_repeated(field):
			if field.type == field.TYPE_MESSAGE:
				keys = [_element_key(element, index) for index, element in enumerate(value)]
				if len(set(keys)) != len(keys):
					# Colliding keys would overwrite each other's leaves and hide changes.
					keys = [str(index) for index in range(len(keys))]
				for key, element in zip(keys, value):
					items.update(leaf_items(element, "{}[{}].".format(path, key)))
			else:
				for index, element in enumerate(value):
					items["{}[{}]".format(path, index)] = element
		elif field.type == field.TYPE_MESSAGE:
			items.update(leaf_items(value, path + "."))
		else:
			items[path] = value
	counts = {}
	for unknown in UnknownFieldSet(message):
		index = counts.get(unknown.field_number, 0)
		counts[unknown.field_number] = index + 1
		items["{}<unknown {}>[{}]".format(prefix, unknown.field_number, index)] = _unknown_value(unknown.data)
	return items


def _is_text(path, value):
	if "<unknown " in path:
		return False
	return isinstance(value, str) or _LOC_PATH.search(path) is not None


def diff_leaves(before, after):
	changes = []
	for path in sorted(set(before) | set(after)):
		old, new = before.get(path), after.get(path)
		if old == new:
			continue
		text = all(_is_text(path, value) for value in (old, new) if value is not None)
		changes.append({"path": path, "before": old, "after": new, "text": text})
	return changes


def changed_entries(changes):
	"""The top-level elements touched, e.g. {"entry[id=12]"}."""
	entries = set()
	for change in changes:
		match = _ENTRY.match(change["path"])
		entries.add(match.group(0) if match else change["path"])
	return entries


def message_name(proto_path):
	"""A data file's message is the last top-level `message` of its .proto."""
	with open(proto_path, "r", encoding="utf-8") as handle:
		names = re.findall(r"^message\s+(\w+)", handle.read(), flags=re.MULTILINE)
	return names[-1] if names else None


class DecoderUnavailable(RuntimeError):
	pass


def find_protoc(repo):
	"""protoc from the main checkout's build (never from a worktree a fixer can edit)."""
	base = os.path.join(repo, "build", "_deps", "protobuf-build")
	for config in ("Release", "RelWithDebInfo", "MinSizeRel", "Debug"):
		for name in ("protoc.exe", "protoc"):
			candidate = os.path.join(base, config, name)
			if os.path.isfile(candidate):
				return candidate
	found = sorted(glob.glob(os.path.join(base, "*", "protoc.exe")) + glob.glob(os.path.join(base, "*", "protoc")))
	return found[0] if found else None


class DataDecoder:
	"""Decodes data files with the protobuf schemas under schema_root/src/shared/proto_data. The
	loop passes its runtime snapshot of origin/develop as schema_root and protoc from the main
	checkout's build, and the schemas are compiled once, here, before any fix runs: nothing a
	fixer writes can change how later diffs are decoded."""

	def __init__(self, schema_root, protoc=None, compile_modules=None):
		self.project_root = schema_root
		self.protoc = protoc
		self._compile = compile_modules or self._compile_with_protoc
		self._compile()

	def _compile_with_protoc(self):
		proto_dir = os.path.abspath(os.path.join(self.project_root, "src", "shared", "proto_data"))
		protos = sorted(name for name in os.listdir(proto_dir) if name.endswith(".proto")) if os.path.isdir(proto_dir) else []
		if not protos:
			raise DecoderUnavailable("no .proto schemas under " + proto_dir)
		if not self.protoc or not os.path.isfile(self.protoc):
			raise DecoderUnavailable("protoc not found ({})".format(self.protoc))
		out_dir = tempfile.mkdtemp(prefix="mmo_bugloop_proto_")
		completed = subprocess.run([self.protoc, "-I" + proto_dir, "--python_out=" + out_dir] + protos, cwd=proto_dir,
			capture_output=True, text=True, encoding="utf-8", errors="replace")
		if completed.returncode != 0:
			raise DecoderUnavailable("protoc failed: " + (completed.stderr or completed.stdout or "")[-1000:])
		sys.path.insert(0, out_dir)

	def decode(self, stem, data):
		proto = os.path.join(self.project_root, "src", "shared", "proto_data", stem + ".proto")
		if not os.path.exists(proto):
			return None
		name = message_name(proto)
		if name is None:
			return None
		message = getattr(importlib.import_module(stem + "_pb2"), name)()
		# Fields the compiled schema does not know are not refused: leaf_items() reports them as
		# "<unknown N>" leaves, so a stale schema cannot hide a change (and unchanged ones stay invisible).
		message.ParseFromString(data)
		return message
