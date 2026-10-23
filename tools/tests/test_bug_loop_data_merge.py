#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the three-way merge of protobuf game data files. The files are built by hand on the
wire format, like the editor's: a container with `repeated Entry entry = 1`, each entry's id in
field 1."""

import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

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


if __name__ == "__main__":
	unittest.main()
