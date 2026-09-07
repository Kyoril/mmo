# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Tests for tools/sfx_gen/postprocess.py."""

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx_gen"))

import postprocess


class TrimSilenceTests(unittest.TestCase):
    def test_removes_leading_and_trailing_silence(self):
        rate = 1000
        body = np.ones(200, dtype=np.float32) * 0.5
        clip = np.concatenate([np.zeros(500, dtype=np.float32), body,
                               np.zeros(800, dtype=np.float32)])

        out = postprocess.trim_silence(clip, rate, pre_roll_ms=0.0)

        self.assertEqual(len(out), 200)

    def test_keeps_pre_roll_before_the_transient(self):
        rate = 1000
        clip = np.concatenate([np.zeros(500, dtype=np.float32),
                               np.ones(100, dtype=np.float32) * 0.5])

        # 10 ms of pre-roll at 1000 Hz is 10 samples.
        out = postprocess.trim_silence(clip, rate, pre_roll_ms=10.0)

        self.assertEqual(len(out), 110)

    def test_all_silence_returns_empty(self):
        out = postprocess.trim_silence(np.zeros(1000, dtype=np.float32), 1000)
        self.assertEqual(len(out), 0)


class NormalizeTests(unittest.TestCase):
    def test_quiet_clip_is_raised_to_target(self):
        clip = np.ones(100, dtype=np.float32) * 0.01

        out = postprocess.normalize_peak(clip, target_dbfs=-3.0)

        expected = 10.0 ** (-3.0 / 20.0)
        self.assertAlmostEqual(float(np.max(np.abs(out))), expected, places=5)

    def test_loud_clip_is_lowered_to_target(self):
        clip = np.ones(100, dtype=np.float32) * 0.99

        out = postprocess.normalize_peak(clip, target_dbfs=-6.0)

        expected = 10.0 ** (-6.0 / 20.0)
        self.assertAlmostEqual(float(np.max(np.abs(out))), expected, places=5)

    def test_silence_is_left_alone(self):
        out = postprocess.normalize_peak(np.zeros(50, dtype=np.float32))
        self.assertEqual(float(np.max(np.abs(out))), 0.0)


class FadeTests(unittest.TestCase):
    def test_fades_start_and_end_at_zero(self):
        rate = 1000
        clip = np.ones(1000, dtype=np.float32)

        out = postprocess.apply_fades(clip, rate, fade_in_ms=2.0, fade_out_ms=15.0)

        self.assertAlmostEqual(float(out[0]), 0.0, places=6)
        self.assertAlmostEqual(float(out[-1]), 0.0, places=6)
        # The middle is untouched.
        self.assertAlmostEqual(float(out[500]), 1.0, places=6)


class ProcessTests(unittest.TestCase):
    def test_quiet_clip_with_long_tail_becomes_short_and_loud(self):
        rate = 44100
        body = (np.random.default_rng(7).normal(0, 1, 4410) * 0.02).astype(np.float32)
        clip = np.concatenate([np.zeros(2205, dtype=np.float32), body,
                               np.zeros(44100, dtype=np.float32)])

        out = postprocess.process(clip, rate, target_dbfs=-3.0)

        # The one-second dead tail is gone.
        self.assertLess(len(out), 5000)
        # And it is no longer inaudible.
        self.assertGreater(float(np.max(np.abs(out))), 0.6)


if __name__ == "__main__":
    unittest.main()
