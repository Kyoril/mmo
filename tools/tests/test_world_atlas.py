#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for worldkit.atlas: validation, stable formatting, and geometry lookups.

The atlas is edited both by agents (as text) and by mmo_edit (nlohmann json), so the byte-stable
round trip is part of the contract: loading and saving an untouched file must not change a byte.

	python tools/tests/test_world_atlas.py
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures  # noqa: F401,E402

from worldkit.atlas import AtlasError, contains, dumps, empty_atlas, load_atlas, save_atlas, validate  # noqa: E402


def sample_doc():
	return {
		"map": 0,
		"version": 1,
		"zones": [{"areaId": 8, "name": "Briarwatch March", "levelMin": 5, "levelMax": 10, "status": "placeholder", "ask": "Confirm band"}],
		"pois": [
			{"id": "kingsroad_waypost", "name": "Kingsroad Waypost", "kind": "hub", "center": [-262.0, 405.0], "radius": 25.0,
			 "levelMin": 7, "levelMax": 8, "status": "canon", "source": "user", "customField": {"keep": True}},
			{"id": "ridge_note", "name": "Kobold ridge", "kind": "note", "center": [5.0, 5.0], "polygon": [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0], [0.0, 10.0]],
			 "status": "note", "source": "user"},
		],
		"roads": [{"id": "westroad", "name": "Westroad", "points": [[0.0, 0.0], [100.0, 5.0]], "status": "placeholder", "ask": "Place it"}],
	}


class AtlasFormatTests(unittest.TestCase):
	def test_roundtrip_is_byte_stable(self):
		text = dumps(sample_doc())
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "map_0.json"
			path.write_text(text, encoding="utf-8", newline="\n")
			atlas = load_atlas(path)
			save_atlas(atlas)
			self.assertEqual(path.read_bytes().decode("utf-8"), text)

	def test_unknown_fields_and_order_preserved(self):
		text = dumps(sample_doc())
		self.assertIn('"customField"', text)
		keys = list(json.loads(text)["pois"][0].keys())
		self.assertEqual(keys[:3], ["id", "name", "kind"])
		self.assertEqual(keys[-1], "customField")

	def test_float_rounding_and_unicode(self):
		doc = sample_doc()
		doc["pois"][0]["center"] = [-262.04, 405.3333]
		doc["pois"][0]["name"] = "Thalric’s Camp"
		text = dumps(doc)
		self.assertIn("-262.0", text)
		self.assertIn("405.3", text)
		self.assertNotIn("405.33", text)
		self.assertIn("Thalric’s Camp", text)
		self.assertTrue(text.endswith("}\n"))


class AtlasValidationTests(unittest.TestCase):
	def assertInvalid(self, doc, fragment):
		errors = validate(doc)
		self.assertTrue(any(fragment in e for e in errors), errors)

	def test_valid(self):
		self.assertEqual(validate(sample_doc()), [])

	def test_duplicate_poi_id(self):
		doc = sample_doc()
		doc["pois"][1]["id"] = "kingsroad_waypost"
		self.assertInvalid(doc, "duplicate id")

	def test_bad_kind(self):
		doc = sample_doc()
		doc["pois"][0]["kind"] = "camps"
		self.assertInvalid(doc, "pois[0].kind")

	def test_note_status_requires_note_kind(self):
		doc = sample_doc()
		doc["pois"][1]["kind"] = "camp"
		self.assertInvalid(doc, "note")

	def test_canon_must_not_keep_an_ask(self):
		doc = sample_doc()
		doc["pois"][0]["ask"] = "still open?"
		self.assertInvalid(doc, "pois[0].ask")

	def test_radius_xor_polygon(self):
		doc = sample_doc()
		doc["pois"][0]["polygon"] = [[0, 0], [1, 0], [1, 1]]
		self.assertInvalid(doc, "exactly one of radius or polygon")

	def test_level_order(self):
		doc = sample_doc()
		doc["zones"][0]["levelMin"] = 11
		self.assertInvalid(doc, "levelMin")

	def test_load_rejects_invalid(self):
		doc = sample_doc()
		doc["version"] = 2
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "map_0.json"
			path.write_text(json.dumps(doc), encoding="utf-8")
			with self.assertRaises(AtlasError):
				load_atlas(path)


class AtlasLookupTests(unittest.TestCase):
	def setUp(self):
		with tempfile.TemporaryDirectory() as tmp:
			path = Path(tmp) / "map_0.json"
			path.write_text(dumps(sample_doc()), encoding="utf-8")
			self.atlas = load_atlas(path)

	def test_contains(self):
		self.assertTrue(contains(self.atlas.poi("kingsroad_waypost"), -250.0, 400.0))
		self.assertFalse(contains(self.atlas.poi("kingsroad_waypost"), -200.0, 400.0))
		self.assertTrue(contains(self.atlas.poi("ridge_note"), 5.0, 5.0))
		self.assertFalse(contains(self.atlas.poi("ridge_note"), 15.0, 5.0))

	def test_band_prefers_poi_over_zone(self):
		self.assertEqual(self.atlas.band_for(-262.0, 405.0, 8), (7, 8, "Kingsroad Waypost"))
		self.assertEqual(self.atlas.band_for(-100.0, 100.0, 8), (5, 10, "Briarwatch March"))
		self.assertIsNone(self.atlas.band_for(-100.0, 100.0, 99))

	def test_lookup_by_name_is_case_insensitive(self):
		self.assertEqual(self.atlas.poi_by_name("kingsroad waypost")["id"], "kingsroad_waypost")

	def test_add_poi_orders_keys_and_dedupes_ids(self):
		atlas = empty_atlas(0)
		first = atlas.add_poi(name="Old Mill", kind="ruin", center=[1.0, 2.0], radius=20.0, status="placeholder", source="agent")
		second = atlas.add_poi(name="Old Mill", kind="ruin", center=[50.0, 2.0], radius=20.0, status="placeholder", source="agent")
		self.assertEqual((first["id"], second["id"]), ("old_mill", "old_mill_2"))
		self.assertEqual(list(first.keys()), ["id", "name", "kind", "center", "radius", "status", "source"])

	def test_add_rejects_invalid(self):
		atlas = empty_atlas(0)
		with self.assertRaises(AtlasError):
			atlas.add_poi(name="X", kind="castle", center=[0.0, 0.0], radius=5.0, status="placeholder")
		self.assertEqual(atlas.pois, [])


if __name__ == "__main__":
	unittest.main()
