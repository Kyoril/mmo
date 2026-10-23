#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the review view of a merge resolution: only the conflicts of a file, as long as the
resolution left git's clean merge around them unchanged."""

import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import merge_resolve  # noqa: E402

HEAD = "".join("line {}\n".format(n) for n in range(1, 21))
TAIL = "".join("tail {}\n".format(n) for n in range(1, 21))
MARKED = HEAD + "<<<<<<< ours\nint a = 9;\n||||||| base\nint a = 1;\n=======\nint a = 2;\n>>>>>>> theirs\n" + TAIL


class ConflictRegionTests(unittest.TestCase):
	def test_shows_each_conflict_with_its_resolution_and_some_context(self):
		view = merge_resolve.conflict_regions(MARKED, HEAD + "int a = 11;\n" + TAIL, "src/a.cpp")
		self.assertIn("1 conflict(s)", view)
		self.assertIn("--- BASE\nint a = 1;\n--- OURS\nint a = 9;\n--- THEIRS\nint a = 2;\n--- RESOLUTION\nint a = 11;\n", view)
		self.assertIn("line 20\n", view)
		self.assertNotIn("line 1\n", view)
		self.assertIn("tail 1\n", view)
		self.assertNotIn("tail 20\n", view)

	def test_two_conflicts(self):
		marked = "a\n<<<<<<< ours\nx\n||||||| base\no\n=======\ny\n>>>>>>> theirs\nb\n<<<<<<< ours\n1\n||||||| base\n0\n=======\n2\n>>>>>>> theirs\nc\n"
		view = merge_resolve.conflict_regions(marked, "a\nxy\nb\n12\nc\n", "f")
		self.assertIn("--- conflict 1 of 2", view)
		self.assertIn("--- RESOLUTION\nxy\n", view)
		self.assertIn("--- RESOLUTION\n12\n", view)

	def test_a_change_outside_the_conflicts_falls_back_to_full_diffs(self):
		self.assertIsNone(merge_resolve.conflict_regions(MARKED, HEAD.replace("line 3", "line three") + "int a = 2;\n" + TAIL, "f"))
		self.assertIsNone(merge_resolve.conflict_regions(MARKED, HEAD + "int a = 2;\n" + TAIL + "extra\n", "f"))

	def test_no_markers_is_not_a_conflict_view(self):
		self.assertIsNone(merge_resolve.conflict_regions("just text\n", "just text\n", "f"))


if __name__ == "__main__":
	unittest.main()
