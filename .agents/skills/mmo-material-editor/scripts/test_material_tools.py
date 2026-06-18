#!/usr/bin/env python3
"""Regression tests for the project-local material tools."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


HTEX = load_module("mmo_htex_tool", Path(__file__).with_name("htex_tool.py"))
MATERIAL = load_module("mmo_material_tool", Path(__file__).with_name("material_tool.py"))


class MaterialToolTests(unittest.TestCase):
    def test_htex_metadata_and_preview(self) -> None:
        source = ROOT / "data/client/Textures/Tilesets/T_Dirt_01_C.htex"
        info = HTEX.read_htex(source)
        self.assertEqual((info["width"], info["height"]), (512, 512))
        self.assertEqual(info["format"], "DXT5 / BC3")
        image = HTEX.decode_mip(info, 0)
        self.assertEqual(image.size, (512, 512))

    def test_node_catalog_has_no_unknown_properties(self) -> None:
        header = ROOT / "src/mmo_edit/editors/material_editor/material_node.h"
        catalog = MATERIAL.build_node_catalog(header)
        self.assertGreaterEqual(len(catalog), 50)
        unknown = [
            (node["type_name"], prop["name"])
            for node in catalog.values()
            for prop in node["properties"]
            if prop["type"] == "unknown"
        ]
        self.assertEqual(unknown, [])

    def test_hmi_round_trip_is_byte_identical(self) -> None:
        source = ROOT / "data/client/Editor/TextureEditorPreview_Instance.hmi"
        document, chunks, original = MATERIAL.parse_material(source, ROOT)
        rewritten = MATERIAL.apply_hmi(document, chunks)
        self.assertEqual(rewritten, original)

    def test_hmat_graph_round_trip_is_byte_identical(self) -> None:
        source = ROOT / "data/client/Models/Terrain/Landscape_Wetness.hmat"
        document, chunks, original = MATERIAL.parse_material(source, ROOT)
        self.assertNotIn("error", document["graph"])
        catalog = MATERIAL.build_node_catalog(
            ROOT / "src/mmo_edit/editors/material_editor/material_node.h"
        )
        rewritten = MATERIAL.apply_hmat_graph(document, chunks, catalog)
        self.assertEqual(rewritten, original)

    def test_all_hmi_files_parse(self) -> None:
        paths = list((ROOT / "data/client").glob("**/*.hmi"))
        self.assertGreater(len(paths), 100)
        for path in paths:
            with self.subTest(path=path):
                document, _, _ = MATERIAL.parse_material(path, ROOT)
                self.assertEqual(document["kind"], "hmi")


if __name__ == "__main__":
    unittest.main()
