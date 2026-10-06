# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import json
import os
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.state import HISTORY_LIMIT, State, load_state, record, save_state


class StateFile(unittest.TestCase):
	def setUp(self):
		self.dir = tempfile.TemporaryDirectory()
		self.path = Path(self.dir.name) / "state.json"

	def tearDown(self):
		self.dir.cleanup()

	def test_missing_file_is_empty_state(self):
		state = load_state(self.path)
		self.assertIsNone(state.live)
		self.assertEqual(state.bad, [])
		self.assertFalse(state.paused)

	def test_round_trip(self):
		state = State(live="a", staged="b", staged_changes=["x"], bad=["c"], paused=True)
		save_state(self.path, state)
		self.assertEqual(load_state(self.path), state)

	def test_unknown_keys_are_ignored(self):
		self.path.write_text(json.dumps({"live": "a", "future_field": 1}), encoding="utf-8")
		self.assertEqual(load_state(self.path).live, "a")

	def test_save_leaves_no_temp_file(self):
		save_state(self.path, State(live="a"))
		self.assertEqual(sorted(os.listdir(self.dir.name)), ["state.json"])

	def test_history_is_capped(self):
		state = State()
		now = datetime.datetime(2026, 10, 7, tzinfo=datetime.timezone.utc)
		for index in range(HISTORY_LIMIT + 5):
			record(state, now, "event", index=index)
		self.assertEqual(len(state.history), HISTORY_LIMIT)
		self.assertEqual(state.history[0]["index"], 5)
		self.assertEqual(state.history[-1]["event"], "event")


if __name__ == "__main__":
	unittest.main()
