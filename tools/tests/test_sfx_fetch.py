# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Tests for tools/sfx_gen/fetch.py."""

import base64
import os
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx_gen"))

import fetch
import postprocess


class FetchAndProcessTests(unittest.TestCase):
    def test_creates_missing_parent_directory(self):
        """fetch_and_process should create output directories that do not exist."""
        with tempfile.TemporaryDirectory() as tmpdir:
            # Generate a short WAV clip using postprocess
            samples = (np.random.default_rng(42).normal(0, 0.1, 22050)).astype(np.float32)

            # Write to a temp file to get the bytes
            temp_wav = os.path.join(tmpdir, "temp.wav")
            postprocess.write_wav(temp_wav, samples)

            # Read back as bytes and encode as data URL
            with open(temp_wav, "rb") as f:
                wav_bytes = f.read()
            data_url = "data:audio/wav;base64," + base64.b64encode(wav_bytes).decode("ascii")

            # Point to a non-existent subdirectory
            out_path = os.path.join(tmpdir, "nonexistent", "sub", "output.wav")

            # This should NOT raise FileNotFoundError, and the file should be created
            stats = fetch.fetch_and_process(data_url, out_path, -3.0)

            # Verify the output file exists
            self.assertTrue(os.path.exists(out_path),
                          f"Output file {out_path} was not created")

            # Verify the returned stats are valid
            self.assertIn("peak", stats)
            self.assertIn("duration", stats)
            self.assertIsInstance(stats["peak"], float)
            self.assertIsInstance(stats["duration"], float)
            self.assertGreaterEqual(stats["duration"], 0.0)

    def test_cleans_up_download_file_on_download_failure(self):
        """fetch_and_process should remove .download file even if download fails."""
        with tempfile.TemporaryDirectory() as tmpdir:
            out_path = os.path.join(tmpdir, "output.wav")
            tmp_path = out_path + ".download"

            # Mock urlopen to raise an exception mid-download
            original_urlopen = None

            def failing_urlopen(url):
                # Create a mock response that raises when read() is called
                mock_response = mock.MagicMock()
                mock_response.__enter__ = mock.MagicMock(return_value=mock_response)
                mock_response.__exit__ = mock.MagicMock(return_value=None)
                mock_response.read = mock.MagicMock(side_effect=IOError("Connection reset"))
                return mock_response

            with mock.patch("urllib.request.urlopen", side_effect=failing_urlopen):
                # This should raise an exception
                with self.assertRaises(IOError):
                    fetch.fetch_and_process("http://example.com/fake.wav", out_path, -3.0)

            # Verify no .download file is left behind
            self.assertFalse(os.path.exists(tmp_path),
                           f"Orphaned .download file {tmp_path} was not cleaned up after download failure")


if __name__ == "__main__":
    unittest.main()
