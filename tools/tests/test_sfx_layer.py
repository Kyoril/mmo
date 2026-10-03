# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Tests for tools/sfx_gen/layer.py."""

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx_gen"))

import layer


class MixTests(unittest.TestCase):
    RATE = 1000

    def test_places_each_layer_at_its_offset(self):
        a = np.ones(100, dtype=np.float32) * 0.5
        b = np.ones(50, dtype=np.float32) * 0.25

        out = layer.mix([(a, layer.Layer("a")), (b, layer.Layer("b", offset=0.2))], self.RATE)

        self.assertEqual(len(out), 250)
        self.assertAlmostEqual(float(out[50]), 0.5, places=5)
        self.assertAlmostEqual(float(out[150]), 0.0, places=5)
        self.assertAlmostEqual(float(out[220]), 0.25, places=5)

    def test_overlapping_layers_sum(self):
        a = np.ones(100, dtype=np.float32) * 0.25
        b = np.ones(100, dtype=np.float32) * 0.25

        out = layer.mix([(a, layer.Layer("a")), (b, layer.Layer("b", offset=0.05))], self.RATE)

        self.assertAlmostEqual(float(out[75]), 0.5, places=5)

    def test_gain_is_applied_in_decibels(self):
        a = np.ones(10, dtype=np.float32)

        out = layer.mix([(a, layer.Layer("a", gain_db=-6.0))], self.RATE)

        self.assertAlmostEqual(float(out[5]), 10 ** (-6.0 / 20.0), places=4)

    def test_length_cuts_the_layer_with_a_fade(self):
        # A layer's own tail can run far past the moment it should give way to the others.
        a = np.ones(1000, dtype=np.float32)

        out = layer.mix([(a, layer.Layer("a", length=0.4, fade_out=0.1))], self.RATE)

        self.assertEqual(len(out), 400)
        self.assertAlmostEqual(float(out[250]), 1.0, places=5)
        self.assertLess(float(out[399]), 0.05)

    def test_negative_offsets_are_rejected(self):
        with self.assertRaises(ValueError):
            layer.Layer("a", offset=-0.1)

    def test_empty_mix_is_empty(self):
        self.assertEqual(len(layer.mix([], self.RATE)), 0)


class ParseLayerArgTests(unittest.TestCase):
    def test_parses_path_offset_gain_and_length(self):
        spec = layer.parse_layer_arg("horn.wav@0.05:-2.5:1.8")

        self.assertEqual(spec, layer.Layer("horn.wav", offset=0.05, gain_db=-2.5, length=1.8))

    def test_offset_and_gain_are_optional(self):
        self.assertEqual(layer.parse_layer_arg("drum.wav"), layer.Layer("drum.wav"))
        self.assertEqual(layer.parse_layer_arg("drum.wav@0.1"), layer.Layer("drum.wav", offset=0.1))


class RecipeLayersTests(unittest.TestCase):
    def test_recipe_mix_points_at_named_takes(self):
        from recipes import human

        layers = layer.recipe_layers(human.MIXES["CallOfTheWatch"], "dir")

        self.assertEqual(len(layers), len(human.MIXES["CallOfTheWatch"]["layers"]))
        for spec, entry in zip(layers, human.MIXES["CallOfTheWatch"]["layers"]):
            self.assertEqual(spec.path, os.path.join("dir", f"{entry.sound}_take{entry.take}.wav"))
            self.assertEqual((spec.offset, spec.gain_db, spec.length),
                             (entry.offset, entry.gain_db, entry.length))


if __name__ == "__main__":
    unittest.main()
