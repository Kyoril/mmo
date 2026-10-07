# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Field-level diff of the protobuf game data files (data/editor/data/*.data), so the guard
can tell a typo fix from a numeric change."""

import importlib
import importlib.util
import os
import pathlib
import re

from google.protobuf.unknown_fields import UnknownFieldSet

# Repeated message elements are keyed by the first of these fields they have, so reordering a
# list is not reported as a change. LocalizedString entries are keyed by locale.
KEY_FIELDS = ("id", "key", "locale")
_ENTRY = re.compile(r"^[^.\[]+\[[^\]]*\]")


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
			for index, element in enumerate(value):
				if field.type == field.TYPE_MESSAGE:
					items.update(leaf_items(element, "{}[{}].".format(path, _element_key(element, index))))
				else:
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
	return isinstance(value, str) or "_loc[" in path


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


class DataDecoder:
	"""Decodes data files with the schemas of the checkout being judged (project_root). The
	proto_runtime helper that compiles them comes from the loop's runtime copy (runtime_root),
	never from the checkout a fixer edited."""

	def __init__(self, project_root, runtime_root, compile_modules=None):
		self.project_root = project_root
		self.runtime_root = runtime_root
		self._compile = compile_modules or self._compile_with_proto_runtime
		self._compiled = False

	def _compile_with_proto_runtime(self):
		path = os.path.join(self.runtime_root, ".agents", "skills", "mmo-quest-creator", "scripts", "proto_runtime.py")
		spec = importlib.util.spec_from_file_location("bugloop_proto_runtime", path)
		module = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(module)
		module.compile_proto_modules(pathlib.Path(self.project_root))

	def decode(self, stem, data):
		proto = os.path.join(self.project_root, "src", "shared", "proto_data", stem + ".proto")
		if not os.path.exists(proto):
			return None
		name = message_name(proto)
		if name is None:
			return None
		if not self._compiled:
			self._compile()
			self._compiled = True
		message = getattr(importlib.import_module(stem + "_pb2"), name)()
		# Fields the compiled schema does not know are not refused: leaf_items() reports them as
		# "<unknown N>" leaves, so a stale schema cannot hide a change (and unchanged ones stay invisible).
		message.ParseFromString(data)
		return message
