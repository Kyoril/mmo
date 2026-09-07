# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Turn raw generated sound-effect output into game-ready WAV files.

Text-to-sound models return a fixed-length clip padded with near-silence and mixed well
below full scale -- a verification take came back at -24 dBFS peak with 1.06 s of dead tail
on a 1.2 s clip. Dropping that straight into the game gives inaudible effects that also
waste memory. Every generated file goes through :func:`process` first.

The output contract matches what the rest of data/client/Sound uses: 44.1 kHz, mono,
16-bit PCM.
"""

from __future__ import annotations

import shutil
import subprocess
import wave

import numpy as np

RATE = 44100


def _db_to_linear(db: float) -> float:
    return float(10.0 ** (db / 20.0))


def trim_silence(samples: np.ndarray, sample_rate: int, threshold_db: float = -45.0,
                 pre_roll_ms: float = 10.0) -> np.ndarray:
    """Drop leading and trailing near-silence.

    A small amount of pre-roll is kept ahead of the first loud sample so the attack
    transient -- the part that makes an impact read as an impact -- is never clipped.
    Returns an empty array when the whole clip is below the threshold.
    """
    if len(samples) == 0:
        return samples

    threshold = _db_to_linear(threshold_db)
    loud = np.flatnonzero(np.abs(samples) > threshold)
    if len(loud) == 0:
        return samples[:0]

    pre_roll = int(sample_rate * pre_roll_ms / 1000.0)
    start = max(0, int(loud[0]) - pre_roll)
    end = int(loud[-1]) + 1
    return samples[start:end]


def apply_fades(samples: np.ndarray, sample_rate: int, fade_in_ms: float = 2.0,
                fade_out_ms: float = 15.0) -> np.ndarray:
    """Ramp the first and last few milliseconds so trimming cannot introduce a click."""
    if len(samples) == 0:
        return samples

    out = samples.astype(np.float32, copy=True)

    fade_in = int(sample_rate * fade_in_ms / 1000.0)
    fade_out = int(sample_rate * fade_out_ms / 1000.0)

    # If the two ramps would overlap, clamp them proportionally so together they span
    # at most the whole clip. This prevents multiplicative parabolic dips on short clips.
    total_fade = fade_in + fade_out
    if total_fade > len(out):
        scale = len(out) / total_fade
        fade_in = int(fade_in * scale)
        fade_out = int(fade_out * scale)

    fade_in = min(fade_in, len(out))
    if fade_in > 1:
        out[:fade_in] *= np.linspace(0.0, 1.0, fade_in, dtype=np.float32)

    fade_out = min(fade_out, len(out))
    if fade_out > 1:
        out[-fade_out:] *= np.linspace(1.0, 0.0, fade_out, dtype=np.float32)

    return out


def normalize_peak(samples: np.ndarray, target_dbfs: float = -3.0) -> np.ndarray:
    """Scale so the loudest sample sits at ``target_dbfs``. Silence is left alone."""
    if len(samples) == 0:
        return samples

    peak = float(np.max(np.abs(samples)))
    if peak <= 0.0:
        return samples.astype(np.float32, copy=True)

    return (samples * (_db_to_linear(target_dbfs) / peak)).astype(np.float32)


def process(samples: np.ndarray, sample_rate: int, target_dbfs: float = -3.0) -> np.ndarray:
    """Trim, fade and normalise in the order that keeps the transient intact.

    Normalising last matters: trimming removes the quiet padding that would otherwise drag
    the average down, so the level is set from the real body of the sound.
    """
    trimmed = trim_silence(samples, sample_rate)
    faded = apply_fades(trimmed, sample_rate)
    return normalize_peak(faded, target_dbfs)


def _ffmpeg() -> str:
    exe = shutil.which("ffmpeg")
    if not exe:
        raise RuntimeError("ffmpeg is not on PATH; it is required to decode generated audio")
    return exe


def decode_to_mono(path: str, sample_rate: int = RATE) -> np.ndarray:
    """Decode any ffmpeg-readable file to a mono float32 array at ``sample_rate``."""
    result = subprocess.run(
        [_ffmpeg(), "-v", "error", "-i", str(path), "-f", "f32le",
         "-ac", "1", "-ar", str(sample_rate), "-"],
        capture_output=True, check=True)
    return np.frombuffer(result.stdout, dtype="<f4").copy()


def write_wav(path: str, samples: np.ndarray, sample_rate: int = RATE) -> None:
    """Write a mono 16-bit PCM WAV."""
    clipped = np.clip(samples, -1.0, 1.0)
    with wave.open(str(path), "wb") as handle:
        handle.setparams((1, 2, sample_rate, 0, "NONE", "not compressed"))
        handle.writeframes((clipped * 32767.0).astype("<i2").tobytes())
