# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Mix separately generated sound layers into one ability sound.

Why this exists: a single text-to-sound prompt that lists several layers ("a horn blast, a
steel ring, a drum hit, a tail") comes back as *one* dominant sound -- the first Call of the
Watch set was four takes of the same dull drum hit with no horn in it at all. Ability audio
needs a recognisable signature built from several elements, so each element is generated on
its own (one prompt, one sound) and the layers are combined here with explicit timing and
balance. The result then goes through :func:`postprocess.process` like any other take.

    py tools/sfx_gen/layer.py out.wav --target -3 \
        --layer horn.wav@0.04:0 --layer steel.wav@0:-5 --layer drum.wav@0:-8:1.2

A layer argument is ``path[@offset[:gain_db[:length]]]``: start offset in seconds, gain in
dB, and an optional length in seconds after which the layer is cut with a short fade.

Checked-in mixes live in the recipe modules (``MIXES``); render one from the downloaded
layer takes, named ``<SoundKey>_take<n>.wav``:

    py tools/sfx_gen/layer.py out.wav --recipe human --mix CallOfTheWatch --layer-dir <dir>
"""

from __future__ import annotations

import argparse
import importlib
import os
from dataclasses import dataclass
from typing import Optional, Sequence, Tuple

import numpy as np

import postprocess


@dataclass(frozen=True)
class Layer:
    """One element of a layered sound."""

    path: str
    offset: float = 0.0          # seconds from the start of the mix
    gain_db: float = 0.0
    length: Optional[float] = None   # seconds; None keeps the whole layer
    fade_out: float = 0.08       # seconds of fade when ``length`` cuts the layer

    def __post_init__(self):
        if self.offset < 0.0:
            raise ValueError(f"{self.path}: offset must not be negative")
        if self.length is not None and self.length <= 0.0:
            raise ValueError(f"{self.path}: length must be positive")


def _shape(samples: np.ndarray, spec: Layer, sample_rate: int) -> np.ndarray:
    out = samples.astype(np.float32, copy=True) * float(10.0 ** (spec.gain_db / 20.0))
    if spec.length is not None:
        end = min(len(out), int(round(spec.length * sample_rate)))
        out = out[:end]
        fade = min(len(out), int(round(spec.fade_out * sample_rate)))
        if fade > 0:
            out[len(out) - fade:] *= np.linspace(1.0, 0.0, fade, dtype=np.float32)
    return out


def mix(layers: Sequence[Tuple[np.ndarray, Layer]], sample_rate: int) -> np.ndarray:
    """Sum the layers at their offsets. No normalisation: that is postprocess's job."""
    shaped = [(int(round(spec.offset * sample_rate)), _shape(samples, spec, sample_rate))
              for samples, spec in layers]
    total = max((start + len(body) for start, body in shaped), default=0)
    out = np.zeros(total, dtype=np.float32)
    for start, body in shaped:
        out[start:start + len(body)] += body
    return out


def render(layers: Sequence[Layer], out_path: str, target_dbfs: float = -3.0) -> dict:
    """Decode, mix, postprocess and write the layered sound; returns basic stats."""
    decoded = [(postprocess.decode_to_mono(spec.path), spec) for spec in layers]
    mixed = postprocess.process(mix(decoded, postprocess.RATE), postprocess.RATE, target_dbfs)
    postprocess.write_wav(out_path, mixed)
    return {"peak": float(np.max(np.abs(mixed))) if len(mixed) else 0.0,
            "duration": len(mixed) / postprocess.RATE}


def parse_layer_arg(text: str) -> Layer:
    path, _, rest = text.partition("@")
    if not rest:
        return Layer(path)
    parts = rest.split(":")
    offset = float(parts[0])
    gain_db = float(parts[1]) if len(parts) > 1 else 0.0
    length = float(parts[2]) if len(parts) > 2 else None
    return Layer(path, offset=offset, gain_db=gain_db, length=length)


def recipe_layers(mix: dict, layer_dir: str) -> list:
    """Turn a recipe ``MIXES`` entry into Layers pointing at the downloaded takes."""
    return [Layer(os.path.join(layer_dir, f"{entry.sound}_take{entry.take}.wav"),
                  offset=entry.offset, gain_db=entry.gain_db, length=entry.length)
            for entry in mix["layers"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("out")
    parser.add_argument("--target", type=float, default=-3.0, help="peak dBFS of the result")
    parser.add_argument("--layer", action="append", default=[],
                        help="path[@offset[:gain_db[:length]]], repeatable")
    parser.add_argument("--recipe", help="recipe module under recipes/, e.g. human")
    parser.add_argument("--mix", help="MIXES key in the recipe module")
    parser.add_argument("--layer-dir", default=".", help="where the <SoundKey>_take<n>.wav files are")
    args = parser.parse_args()
    if args.recipe:
        mix = importlib.import_module(f"recipes.{args.recipe}").MIXES[args.mix]
        layers, target = recipe_layers(mix, args.layer_dir), mix["target_dbfs"]
    else:
        if not args.layer:
            parser.error("give --layer arguments or --recipe/--mix")
        layers, target = [parse_layer_arg(text) for text in args.layer], args.target
    stats = render(layers, args.out, target)
    print("wrote %s  peak=%.3f  duration=%.2fs" % (args.out, stats["peak"], stats["duration"]))


if __name__ == "__main__":
    main()
