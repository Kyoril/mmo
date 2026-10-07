#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the protobuf data diff. descriptor_pb2 messages stand in for game data, so
these tests need no protoc; one integration test decodes a real data file when a build exists."""

import glob
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from google.protobuf import descriptor_pb2  # noqa: E402

from bugloop import data_diff  # noqa: E402


def file_proto():
	proto = descriptor_pb2.FileDescriptorProto(name="items.proto", package="mmo")
	message = proto.message_type.add(name="Item")
	message.field.add(name="id", number=1)
	message.field.add(name="price", number=2)
	return proto


class LeafTests(unittest.TestCase):
	def test_flattens_set_fields(self):
		leaves = data_diff.leaf_items(file_proto())
		self.assertEqual(leaves["name"], "items.proto")
		self.assertEqual(leaves["message_type[0].name"], "Item")
		self.assertEqual(leaves["message_type[0].field[1].number"], 2)

	def test_text_change(self):
		after = file_proto()
		after.message_type[0].name = "Weapon"
		changes = data_diff.diff_leaves(data_diff.leaf_items(file_proto()), data_diff.leaf_items(after))
		self.assertEqual(len(changes), 1)
		self.assertTrue(changes[0]["text"])

	def test_numeric_change(self):
		after = file_proto()
		after.message_type[0].field[1].number = 7
		changes = data_diff.diff_leaves(data_diff.leaf_items(file_proto()), data_diff.leaf_items(after))
		self.assertEqual([change["path"] for change in changes], ["message_type[0].field[1].number"])
		self.assertFalse(changes[0]["text"])

	def test_keyed_elements_ignore_reordering(self):
		after = descriptor_pb2.FileDescriptorProto(name="items.proto", package="mmo")
		message = after.message_type.add(name="Item")
		message.field.add(name="price", number=2)
		message.field.add(name="id", number=1)
		with mock.patch.object(data_diff, "KEY_FIELDS", ("number",)):
			before_leaves = data_diff.leaf_items(file_proto())
			after_leaves = data_diff.leaf_items(after)
		self.assertIn("message_type[0].field[number=2].name", before_leaves)
		self.assertEqual(data_diff.diff_leaves(before_leaves, after_leaves), [])

	def test_changed_entries_groups_by_top_level_element(self):
		changes = [{"path": "entry[id=5].name"}, {"path": "entry[id=5].price"}, {"path": "entry[id=9].name"}, {"path": "name"}]
		self.assertEqual(data_diff.changed_entries(changes), {"entry[id=5]", "entry[id=9]", "name"})


class DecoderTests(unittest.TestCase):
	def test_message_name_is_last_top_level_message(self):
		with tempfile.TemporaryDirectory() as folder:
			path = os.path.join(folder, "x.proto")
			with open(path, "w", encoding="utf-8") as handle:
				handle.write("message Entry {\n\tmessage Nested {}\n}\nmessage Entries {\n\trepeated Entry entry = 1;\n}\n")
			self.assertEqual(data_diff.message_name(path), "Entries")

	def test_missing_schema_decodes_to_none_without_compiling(self):
		def fail():
			raise AssertionError("must not compile")
		with tempfile.TemporaryDirectory() as folder:
			decoder = data_diff.DataDecoder(folder, folder, compile_modules=fail)
			self.assertIsNone(decoder.decode("combat_ratings", b""))

	def make_decoder(self, folder):
		proto_dir = os.path.join(folder, "src", "shared", "proto_data")
		os.makedirs(proto_dir)
		with open(os.path.join(proto_dir, "descriptor.proto"), "w", encoding="utf-8") as handle:
			handle.write("message FileDescriptorProto {}\n")
		return data_diff.DataDecoder(folder, folder, compile_modules=lambda: None)

	def test_decodes_with_compiled_module(self):
		with tempfile.TemporaryDirectory() as folder, mock.patch.dict(sys.modules, {"descriptor_pb2": descriptor_pb2}):
			message = self.make_decoder(folder).decode("descriptor", file_proto().SerializeToString())
			self.assertEqual(message.name, "items.proto")

	def test_unknown_fields_are_refused(self):
		# Field 99, varint 1: valid wire data the schema does not know. Ignoring it would hide a change.
		data = file_proto().SerializeToString() + b"\x98\x06\x01"
		with tempfile.TemporaryDirectory() as folder, mock.patch.dict(sys.modules, {"descriptor_pb2": descriptor_pb2}):
			with self.assertRaises(ValueError):
				self.make_decoder(folder).decode("descriptor", data)


PROTOC = glob.glob(os.path.join(REPO_ROOT, "build", "_deps", "protobuf-build", "*", "protoc.exe"))
ZONES = os.path.join(REPO_ROOT, "data", "editor", "data", "zones.data")


@unittest.skipUnless(PROTOC and os.path.exists(ZONES), "needs a build with protoc and the data/editor submodule")
class RealDataTests(unittest.TestCase):
	def test_decodes_a_real_data_file(self):
		decoder = data_diff.DataDecoder(REPO_ROOT, REPO_ROOT)
		with open(ZONES, "rb") as handle:
			message = decoder.decode("zones", handle.read())
		self.assertIsNotNone(message)
		self.assertTrue(data_diff.leaf_items(message))


if __name__ == "__main__":
	unittest.main()
