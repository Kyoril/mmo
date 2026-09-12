# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Generates the tiling water textures used by the ocean material and the underwater pass.

Outputs, next to this script:

  WaterNoise_01.png  - broadband tileable value-noise fbm. Thresholded in the water material
                       to scatter whitecaps across open water.
  Caustics_01.png    - Worley cell edges: the bright filaments light forms when it refracts
                       through a rippled surface. Projected in world space by PS_Underwater.hlsl.

Both tile seamlessly. The noise lattice wraps at its period, and the caustic feature points are
replicated into the eight neighbouring copies so distances wrap at the border. That matters
because both are sampled with a wrapping address mode over world-space coordinates that run for
hundreds of units - a visible seam would repeat across the whole ocean.

Import the PNGs as .htex through mmo_edit; there is no CLI for that conversion.

Usage:
    python tools/water_gen/gen_water_textures.py
"""

import os

import numpy as np
from PIL import Image

SIZE = 512
OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def tileable_value_noise(size, period, rng):
    """Value noise whose lattice wraps at `period`, so the result tiles seamlessly."""
    grid = rng.random((period, period))

    ys, xs = np.meshgrid(np.arange(size), np.arange(size), indexing="ij")
    fx = xs * period / size
    fy = ys * period / size

    x0 = np.floor(fx).astype(int) % period
    y0 = np.floor(fy).astype(int) % period
    x1 = (x0 + 1) % period
    y1 = (y0 + 1) % period

    tx = fx - np.floor(fx)
    ty = fy - np.floor(fy)

    # Smoothstep the interpolant, or the lattice shows up as diamond artefacts.
    sx = tx * tx * (3 - 2 * tx)
    sy = ty * ty * (3 - 2 * ty)

    a = grid[y0, x0] * (1 - sx) + grid[y0, x1] * sx
    b = grid[y1, x0] * (1 - sx) + grid[y1, x1] * sx
    return a * (1 - sy) + b * sy


def fbm(size, base_period, octaves, rng):
    """Fractal sum of tileable value noise. Every octave wraps, so the sum does too."""
    total = np.zeros((size, size))
    amplitude = 1.0
    normalisation = 0.0
    period = base_period

    for _ in range(octaves):
        total += tileable_value_noise(size, period, rng) * amplitude
        normalisation += amplitude
        amplitude *= 0.5
        period *= 2

    return total / normalisation


def caustic_layer(size, count, seed, sharpness):
    """F2-F1 Worley edges, tiled so distances wrap at the border.

    Scaled by the natural cell spacing rather than by the observed extremes: a global
    min/max normalisation lets one unusually large cell wash a whole corner brighter than
    the rest, which reads as a stain rather than as caustics.
    """
    rng = np.random.default_rng(seed)
    points = rng.random((count, 2))

    ys, xs = np.meshgrid(
        np.linspace(0, 1, size, endpoint=False),
        np.linspace(0, 1, size, endpoint=False),
        indexing="ij",
    )

    nearest = np.full((size, size), 10.0)
    second = np.full((size, size), 10.0)

    for offset_x in (-1, 0, 1):
        for offset_y in (-1, 0, 1):
            for px, py in points:
                distance = np.sqrt((xs - (px + offset_x)) ** 2 + (ys - (py + offset_y)) ** 2)
                closer = distance < nearest
                second = np.where(closer, nearest, np.minimum(second, distance))
                nearest = np.where(closer, distance, nearest)

    spacing = 1.0 / np.sqrt(count)
    edge = np.clip((second - nearest) / spacing, 0.0, 1.0)
    return (1.0 - edge) ** sharpness


def save_grey(array, filename):
    """Writes a [0,1] array as an RGB PNG with the value replicated across all channels."""
    image = (np.stack([array] * 3, axis=-1) * 255).astype(np.uint8)
    path = os.path.join(OUT_DIR, filename)
    Image.fromarray(image, "RGB").save(path)
    print("%-20s mean=%.3f" % (filename, array.mean()))


def main():
    rng = np.random.default_rng(20260912)

    noise = fbm(SIZE, 4, 5, rng)
    noise = (noise - noise.min()) / (noise.max() - noise.min())
    save_grey(noise, "WaterNoise_01.png")

    # Two densities combined. The coarse layer modulates the fine one into unequal
    # brightness, which is what stops the result reading as a regular net.
    fine = caustic_layer(SIZE, 90, 20260912, 6.0)
    coarse = caustic_layer(SIZE, 28, 777, 4.0)
    caustic = np.clip(fine * 0.75 + fine * coarse * 0.9, 0.0, 1.0)
    caustic /= caustic.max()
    save_grey(caustic, "Caustics_01.png")


if __name__ == "__main__":
    main()
