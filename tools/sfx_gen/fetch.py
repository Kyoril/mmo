# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Download generated audio and run it through the postprocessor.

Usage::

    python tools/sfx_gen/fetch.py <out.wav> <target_dbfs> <signed-url>
    python tools/sfx_gen/fetch.py --audition <out.wav> <wav> [<wav> ...]

The signed URLs come from the MCP connector's run status; they expire, so fetch promptly.
"""

from __future__ import annotations

import os
import sys
import urllib.request

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import postprocess


def fetch_and_process(url: str, out_path: str, target_dbfs: float) -> dict:
    """Download one generated clip, postprocess it, and write it as a game-ready WAV."""
    tmp_path = out_path + ".download"
    with urllib.request.urlopen(url) as response:
        with open(tmp_path, "wb") as handle:
            handle.write(response.read())

    try:
        raw = postprocess.decode_to_mono(tmp_path)
        processed = postprocess.process(raw, postprocess.RATE, target_dbfs=target_dbfs)
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        postprocess.write_wav(out_path, processed)
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

    return {
        "peak": float(np.max(np.abs(processed))) if len(processed) else 0.0,
        "duration": len(processed) / float(postprocess.RATE),
    }


def build_audition(wav_paths, out_path: str, gap_seconds: float = 0.5) -> None:
    """Concatenate finished WAVs with gaps, so the whole set can be judged in one listen."""
    gap = np.zeros(int(postprocess.RATE * gap_seconds), dtype=np.float32)
    clips = []
    for path in wav_paths:
        clips.append(postprocess.decode_to_mono(path))
        clips.append(gap)
    postprocess.write_wav(out_path, np.concatenate(clips) if clips else np.zeros(0, np.float32))


if __name__ == "__main__":
    if sys.argv[1] == "--audition":
        build_audition(sys.argv[3:], sys.argv[2])
        print("wrote %s" % sys.argv[2])
    else:
        stats = fetch_and_process(sys.argv[3], sys.argv[1], float(sys.argv[2]))
        print("wrote %s  peak=%.3f  duration=%.2fs"
              % (sys.argv[1], stats["peak"], stats["duration"]))
