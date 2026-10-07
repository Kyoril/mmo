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

	def test_colliding_keys_fall_back_to_index(self):
		def build(first_name):
			proto = descriptor_pb2.FileDescriptorProto(name="items.proto", package="mmo")
			message = proto.message_type.add(name="Item")
			message.field.add(name=first_name, number=1)
			message.field.add(name="b", number=1)
			return proto
		with mock.patch.object(data_diff, "KEY_FIELDS", ("number",)):
			before = data_diff.leaf_items(build("a"))
			after = data_diff.leaf_items(build("changed"))
		self.assertEqual(before["message_type[0].field[0].name"], "a")
		self.assertEqual(before["message_type[0].field[1].name"], "b")
		self.assertEqual([c["path"] for c in data_diff.diff_leaves(before, after)], ["message_type[0].field[0].name"])

	def test_loc_text_detection_is_anchored(self):
		changes = data_diff.diff_leaves({"name_loc[locale=1].value": "a"}, {"name_loc[locale=1].value": "b"})
		self.assertTrue(changes[0]["text"])
		changes = data_diff.diff_leaves({"entry[key=x_loc[].price": 1}, {"entry[key=x_loc[].price": 2})
		self.assertFalse(changes[0]["text"])
		path = "entry[id=1].name_loc[locale=1].locale"
		self.assertTrue(data_diff.diff_leaves({path: 1}, {path: 2})[0]["text"])

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

	def test_schemas_compile_at_construction_not_on_first_decode(self):
		calls = []
		with tempfile.TemporaryDirectory() as folder:
			decoder = data_diff.DataDecoder(folder, compile_modules=lambda: calls.append(1))
			self.assertEqual(calls, [1])
			self.assertIsNone(decoder.decode("combat_ratings", b""))
			self.assertEqual(calls, [1])

	def test_missing_schemas_or_protoc_make_the_decoder_unavailable(self):
		with tempfile.TemporaryDirectory() as folder:
			with self.assertRaises(data_diff.DecoderUnavailable):
				data_diff.DataDecoder(folder, protoc=os.path.join(folder, "protoc.exe"))
			proto_dir = os.path.join(folder, "src", "shared", "proto_data")
			os.makedirs(proto_dir)
			with open(os.path.join(proto_dir, "x.proto"), "w", encoding="utf-8") as handle:
				handle.write("message X {}\n")
			with self.assertRaises(data_diff.DecoderUnavailable):
				data_diff.DataDecoder(folder, protoc=os.path.join(folder, "protoc.exe"))
			self.assertIsNone(data_diff.find_protoc(folder))

	def make_decoder(self, folder):
		proto_dir = os.path.join(folder, "src", "shared", "proto_data")
		os.makedirs(proto_dir)
		with open(os.path.join(proto_dir, "descriptor.proto"), "w", encoding="utf-8") as handle:
			handle.write("message FileDescriptorProto {}\n")
		return data_diff.DataDecoder(folder, compile_modules=lambda: None)

	def test_decodes_with_compiled_module(self):
		with tempfile.TemporaryDirectory() as folder, mock.patch.dict(sys.modules, {"descriptor_pb2": descriptor_pb2}):
			message = self.make_decoder(folder).decode("descriptor", file_proto().SerializeToString())
			self.assertEqual(message.name, "items.proto")

	def decode_with_unknown(self, payload):
		# Field 99 varint: valid wire data the schema does not know.
		data = file_proto().SerializeToString() + payload
		with tempfile.TemporaryDirectory() as folder, mock.patch.dict(sys.modules, {"descriptor_pb2": descriptor_pb2}):
			return self.make_decoder(folder).decode("descriptor", data)

	def test_unknown_fields_surface_as_leaves(self):
		message = self.decode_with_unknown(b"\x98\x06\x01")
		leaves = data_diff.leaf_items(message)
		self.assertTrue([key for key in leaves if "<unknown 99>" in key])

	def test_changed_unknown_field_is_one_non_text_change(self):
		before = data_diff.leaf_items(self.decode_with_unknown(b"\x98\x06\x01"))
		after = data_diff.leaf_items(self.decode_with_unknown(b"\x98\x06\x02"))
		changes = data_diff.diff_leaves(before, after)
		self.assertEqual(len(changes), 1)
		self.assertFalse(changes[0]["text"])

	def test_identical_unknown_fields_do_not_change(self):
		before = data_diff.leaf_items(self.decode_with_unknown(b"\x98\x06\x01"))
		after = data_diff.leaf_items(self.decode_with_unknown(b"\x98\x06\x01"))
		self.assertEqual(data_diff.diff_leaves(before, after), [])


PROTOC = data_diff.find_protoc(REPO_ROOT)
ZONES = os.path.join(REPO_ROOT, "data", "editor", "data", "zones.data")


@unittest.skipUnless(PROTOC and os.path.exists(ZONES), "needs a build with protoc and the data/editor submodule")
class RealDataTests(unittest.TestCase):
	def test_decodes_a_real_data_file(self):
		decoder = data_diff.DataDecoder(REPO_ROOT, protoc=PROTOC)
		with open(ZONES, "rb") as handle:
			message = decoder.decode("zones", handle.read())
		self.assertIsNotNone(message)
		self.assertTrue(data_diff.leaf_items(message))


if __name__ == "__main__":
	unittest.main()
