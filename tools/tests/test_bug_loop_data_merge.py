#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the three-way merge of protobuf game data files. The files are built by hand on the
wire format, like the editor's: a container with `repeated Entry entry = 1`, each entry's id in
field 1."""

import os
import struct
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from google.protobuf import descriptor_pb2, descriptor_pool  # noqa: E402

from bugloop import data_merge  # noqa: E402


def varint(value):
	out = bytearray()
	while True:
		byte = value & 0x7F
		value >>= 7
		if value:
			out.append(byte | 0x80)
		else:
			out.append(byte)
			return bytes(out)


def number(field, value):
	return varint(field << 3) + varint(value)


def nested(field, payload):
	return varint(field << 3 | 2) + varint(len(payload)) + payload


def text(field, value):
	return nested(field, value.encode("utf-8"))


def entry(entry_id, *fields):
	return nested(1, number(1, entry_id) + b"".join(fields))


def spawn(x, y):
	return nested(5, number(1, x) + number(2, y))


def data(*entries):
	return b"".join(entries)


BASE = data(entry(1, text(2, "Sword"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))


class DataMergeTests(unittest.TestCase):
	def test_one_side_changed_returns_the_other_side_unchanged(self):
		ours = data(entry(1, text(2, "Sword"), number(3, 11)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		self.assertEqual(data_merge.merge(BASE, BASE, ours), ours)
		self.assertEqual(data_merge.merge(BASE, ours, BASE), ours)
		self.assertEqual(data_merge.merge(BASE, ours, ours), ours)

	def test_different_entries_merge(self):
		ours = data(entry(1, text(2, "Sword"), number(3, 11)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		theirs = data(entry(1, text(2, "Sword"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Great Helm"), number(3, 30)))
		merged = data_merge.merge(BASE, ours, theirs)
		self.assertEqual(merged, data(entry(1, text(2, "Sword"), number(3, 11)), entry(2, text(2, "Shield"), number(3, 20)),
			entry(3, text(2, "Great Helm"), number(3, 30))))

	def test_different_fields_of_one_entry_merge(self):
		ours = data(entry(1, text(2, "Longsword"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		theirs = data(entry(1, text(2, "Sword"), number(3, 15)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		merged = data_merge.merge(BASE, ours, theirs)
		self.assertEqual(merged[:len(entry(1, text(2, "Longsword"), number(3, 15)))], entry(1, text(2, "Longsword"), number(3, 15)))

	def test_same_value_changed_differently_conflicts(self):
		ours = data(entry(1, text(2, "Sword"), number(3, 11)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		theirs = data(entry(1, text(2, "Sword"), number(3, 12)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		with self.assertRaises(data_merge.DataConflict) as raised:
			data_merge.merge(BASE, ours, theirs)
		self.assertIn("entry[id=1]", str(raised.exception))

	def test_text_changed_on_both_sides_is_never_merged_as_a_message(self):
		ours = data(entry(1, text(2, "Sword of Dawn"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		theirs = data(entry(1, text(2, "Sword of Dusk"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		with self.assertRaises(data_merge.DataConflict):
			data_merge.merge(BASE, ours, theirs)

	def test_entries_added_on_both_sides_are_kept(self):
		ours = BASE + entry(4, text(2, "Boots"))
		theirs = BASE + entry(5, text(2, "Gloves"))
		self.assertEqual(data_merge.merge(BASE, ours, theirs), BASE + entry(4, text(2, "Boots")) + entry(5, text(2, "Gloves")))

	def test_reordered_entries_still_match_by_id(self):
		theirs = data(entry(3, text(2, "Helm"), number(3, 30)), entry(1, text(2, "Sword"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 20)))
		ours = data(entry(1, text(2, "Sword"), number(3, 11)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		merged = data_merge.merge(BASE, ours, theirs)
		self.assertIn(entry(1, text(2, "Sword"), number(3, 11)), merged)
		self.assertEqual(merged.count(entry(3, text(2, "Helm"), number(3, 30))), 1)

	def test_removal_merges_with_an_untouched_entry_but_not_with_a_changed_one(self):
		removed = data(entry(1, text(2, "Sword"), number(3, 10)), entry(3, text(2, "Helm"), number(3, 30)))
		other = data(entry(1, text(2, "Sword"), number(3, 11)), entry(2, text(2, "Shield"), number(3, 20)), entry(3, text(2, "Helm"), number(3, 30)))
		self.assertEqual(data_merge.merge(BASE, removed, other), data(entry(1, text(2, "Sword"), number(3, 11)), entry(3, text(2, "Helm"), number(3, 30))))
		changed = data(entry(1, text(2, "Sword"), number(3, 10)), entry(2, text(2, "Shield"), number(3, 21)), entry(3, text(2, "Helm"), number(3, 30)))
		with self.assertRaises(data_merge.DataConflict) as raised:
			data_merge.merge(BASE, removed, changed)
		self.assertIn("removed", str(raised.exception))

	def test_unkeyed_lists_merge_as_sequences(self):
		# Spawn points have no id; edits to different spawns of one map still merge.
		base = data(entry(7, spawn(1, 1), spawn(2, 2), spawn(3, 3)))
		ours = data(entry(7, spawn(1, 9), spawn(2, 2), spawn(3, 3)))
		theirs = data(entry(7, spawn(1, 1), spawn(2, 2), spawn(3, 8), spawn(4, 4)))
		self.assertEqual(data_merge.merge(base, ours, theirs), data(entry(7, spawn(1, 9), spawn(2, 2), spawn(3, 8), spawn(4, 4))))

	def test_same_spawn_moved_differently_conflicts(self):
		base = data(entry(7, spawn(1, 1), spawn(2, 2)))
		ours = data(entry(7, spawn(1, 5), spawn(2, 2)))
		theirs = data(entry(7, spawn(1, 6), spawn(2, 2)))
		with self.assertRaises(data_merge.DataConflict) as raised:
			data_merge.merge(base, ours, theirs)
		self.assertTrue(str(raised.exception).startswith("entry[id=7].5"), str(raised.exception))

	def test_file_added_on_both_sides(self):
		self.assertEqual(data_merge.merge(None, entry(1), entry(1)), entry(1))
		self.assertEqual(data_merge.merge(None, entry(1), entry(2)), entry(1) + entry(2))

	def test_not_a_protobuf_message(self):
		with self.assertRaises(ValueError):
			data_merge.merge(BASE, BASE + b"\xff", BASE + b"\x0f")


def fixed32(field, value):
	return varint(field << 3 | 5) + struct.pack("<f", value)


def location(x, y, z):
	return fixed32(1, x) + fixed32(2, y) + fixed32(3, z)


def placed(x, y, z, name=""):
	return nested(5, nested(10, location(x, y, z)) + (text(2, name) if name else b""))


def map_schema():
	"""Container { repeated Entry entry = 1 }, Entry { id = 1; repeated Spawn spawns = 5 },
	Spawn { name = 2; Location location = 10 }, Location { positionx/y/z = 1/2/3 }."""
	proto = descriptor_pb2.FileDescriptorProto(name="test_maps.proto", package="mergetest", syntax="proto2")
	location_type = proto.message_type.add(name="Location")
	for number, name in ((1, "positionx"), (2, "positiony"), (3, "positionz")):
		location_type.field.add(name=name, number=number, type=descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT,
			label=descriptor_pb2.FieldDescriptorProto.LABEL_OPTIONAL)
	spawn = proto.message_type.add(name="Spawn")
	spawn.field.add(name="name", number=2, type=descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
		label=descriptor_pb2.FieldDescriptorProto.LABEL_OPTIONAL)
	spawn.field.add(name="location", number=10, type=descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE,
		type_name=".mergetest.Location", label=descriptor_pb2.FieldDescriptorProto.LABEL_OPTIONAL)
	entry_type = proto.message_type.add(name="Entry")
	entry_type.field.add(name="id", number=1, type=descriptor_pb2.FieldDescriptorProto.TYPE_UINT32,
		label=descriptor_pb2.FieldDescriptorProto.LABEL_OPTIONAL)
	entry_type.field.add(name="spawns", number=5, type=descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE,
		type_name=".mergetest.Spawn", label=descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED)
	container = proto.message_type.add(name="Maps")
	container.field.add(name="entry", number=1, type=descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE,
		type_name=".mergetest.Entry", label=descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED)
	pool = descriptor_pool.DescriptorPool()
	pool.Add(proto)
	return pool.FindMessageTypeByName("mergetest.Maps")


class SmarterMergeTests(unittest.TestCase):
	"""Replayed against the 62 historical merges in data/editor and data/client where both sides
	changed one .data file: 54 come out as the humans merged them, the 8 others are id collisions or
	two different new sentences."""

	def test_edits_to_different_words_of_a_string_merge(self):
		# Spell 3, merge d28f5b2e: both turned the damage into $s0, one side also the percentage.
		base = entry(3, text(65, "causing 20 to 22 Frost damage and slowing by 40% for 5 sec."))
		ours = entry(3, text(65, "causing $s0 Frost damage and slowing by $s1% for $d sec."))
		theirs = entry(3, text(65, "causing $s0 Frost damage and slowing by 40% for $d sec."))
		self.assertEqual(data_merge.merge(base, ours, theirs), ours)

	def test_edits_to_the_same_words_still_conflict(self):
		base = entry(5, text(65, "Engulfs the target."))
		with self.assertRaises(data_merge.DataConflict):
			data_merge.merge(base, entry(5, text(65, "Engulfs the foe.")), entry(5, text(65, "Engulfs the enemy.")))

	def test_spawns_appended_next_to_re_snapped_heights_merge(self):
		# Merge 9ca1af03: the fix added two spawns at the end of the list, develop re-snapped the
		# height of the last two existing ones. The edits only meet; both apply.
		base = entry(0, placed(1, 1, 1), placed(2, 2, 2), placed(3, 3, 3))
		ours = entry(0, placed(1, 1, 1), placed(2, 2, 2), placed(3, 3, 3), placed(4, 4, 4), placed(5, 5, 5))
		theirs = entry(0, placed(1, 1, 1), placed(2, 9, 2), placed(3, 8, 3))
		merged = entry(0, placed(1, 1, 1), placed(2, 9, 2), placed(3, 8, 3), placed(4, 4, 4), placed(5, 5, 5))
		self.assertEqual(data_merge.merge(base, ours, theirs), merged)

	def test_a_moved_spawn_keeps_its_own_height_over_a_re_snap(self):
		# Merge 9ca1af03: the fix moved and named a crate, develop re-snapped its old height.
		base = entry(0, placed(262, 15.6, 762))
		ours = entry(0, placed(244, 13.0, 741, "Lina's Crate 01"))
		theirs = entry(0, placed(262, 15.8, 762))
		self.assertEqual(data_merge.merge(base, ours, theirs, map_schema()), ours)
		self.assertEqual(data_merge.merge(base, theirs, ours, map_schema()), ours)
		with self.assertRaises(data_merge.DataConflict):
			data_merge.merge(base, ours, theirs)  # without the schema there are no field names

	def test_two_moves_of_one_spawn_still_conflict(self):
		base = entry(0, placed(1, 1, 1))
		with self.assertRaises(data_merge.DataConflict):
			data_merge.merge(base, entry(0, placed(5, 1, 1)), entry(0, placed(6, 1, 1)), map_schema())

	def test_an_id_both_sides_added_is_named_a_collision(self):
		with self.assertRaises(data_merge.DataConflict) as raised:
			data_merge.merge(BASE, BASE + entry(4, text(2, "Boots")), BASE + entry(4, text(2, "Gloves")))
		self.assertIn("id collision", str(raised.exception))


if __name__ == "__main__":
	unittest.main()
