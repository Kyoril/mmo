# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Recipe for ``data/client/Particles/Haven_BrazierFlame.hpar`` -- the calm, endlessly looping
fire in the brazier at the centre of Haven's plaza (the character-select gathering spot).

Run from the repo root::

    python tools/particle_gen/recipes/haven_plaza_brazier.py

Placed by ``Models/Haven/Props/Haven_PlazaBrazier.hwmo`` as a particle-system mesh reference
(scale 1, so the numbers below are world units) on the coal bed of ``SM_hc_Brazier_B`` at 2x:
that bowl is ~2.1 m wide with its rim ~0.96 m above the ground, and the reference sits at
y = 0.78, a little below the rim, so the tongues grow out of the bowl rather than float on it.

Layers (all looping, ``SIM_WORLD`` so the fire does not drag along if the placement moves):

* **Flame tongues** -- FLAME sprites rendered STRETCHED at length_scale 2 (the sprite is
  1:2), rising slowly out of a disc a little narrower than the bowl. This is the silhouette.
* **Hot core** -- fewer, smaller, paler tongues in the middle; the bright heart of the fire.
* **Heat glow** -- a few large, very low-alpha soft blobs over the bowl. The engine has no
  additive blending or bloom, so this is what reads as glow; peak alpha stays ~0.15.
* **Embers** -- tiny bright sparks drifting up on noise for 3-5 s, 2-4 m high. Calm, so the
  rate is low; they are the part that is still visible above a character standing in front.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import hpar  # noqa: E402
from hpar import Emitter, ParticleSystem, color_curve, float_curve  # noqa: E402

FLAME = "Particles/Particle_Flame.hmi"   # teardrop flame tongue, tip up (1:2)
GLOW = "Particles/Particle_Glow.hmi"     # soft radial blob

PALE_HOT = (1.00, 0.96, 0.78)
FIRE_YELLOW = (1.00, 0.88, 0.48)
FIRE_ORANGE = (1.00, 0.66, 0.26)
EMBER = (1.00, 0.62, 0.22)


def rgba(rgb, a):
    return (rgb[0], rgb[1], rgb[2], a)


def budget(rate, max_lifetime, margin=1.3):
    """max_particles that can never truncate a continuous emitter."""
    return int(rate * max_lifetime * margin) + 4


def build():
    emitters = []

    # 1. Flame tongues -- the body and silhouette of the fire.
    rate, life = 34.0, 1.3
    emitters.append(Emitter(
        name="Flame Tongues",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=1.5,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.42),
        min_lifetime=0.75, max_lifetime=life,
        min_velocity=(0.0, 0.35, 0.0), max_velocity=(0.0, 0.60, 0.0),
        min_start_size=0.34, max_start_size=0.52,
        gravity=(0.0, 0.55, 0.0),
        noise_amplitude=0.25, noise_frequency=1.2,
        render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
        material_name=FLAME,
        # Grows out of the coals, then the tongue narrows away as it rises.
        size_over_life=float_curve((0.0, 0.55), (0.3, 1.0), (1.0, 0.30)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_YELLOW, 0.0)),
            (0.15, rgba(FIRE_YELLOW, 0.62)),
            (0.55, rgba(FIRE_ORANGE, 0.50)),
            (1.00, rgba(FIRE_ORANGE, 0.0)),
        ),
    ))

    # 2. Hot core -- the pale, bright heart of the fire.
    rate, life = 20.0, 0.8
    emitters.append(Emitter(
        name="Hot Core",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=1.5,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.22),
        min_lifetime=0.5, max_lifetime=life,
        min_velocity=(0.0, 0.30, 0.0), max_velocity=(0.0, 0.50, 0.0),
        min_start_size=0.22, max_start_size=0.32,
        gravity=(0.0, 0.45, 0.0),
        noise_amplitude=0.15, noise_frequency=1.2,
        render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
        material_name=FLAME,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.35)),
        color_over_lifetime=color_curve(
            (0.00, rgba(PALE_HOT, 0.0)),
            (0.15, rgba(PALE_HOT, 0.70)),
            (0.60, rgba(FIRE_YELLOW, 0.50)),
            (1.00, rgba(FIRE_ORANGE, 0.0)),
        ),
    ))

    # 3. Heat glow -- large, very faint blobs; the only "glow" alpha blending allows.
    rate, life = 3.0, 1.8
    emitters.append(Emitter(
        name="Heat Glow",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=2.0,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.25),
        min_lifetime=1.3, max_lifetime=life,
        min_velocity=(0.0, 0.15, 0.0), max_velocity=(0.0, 0.30, 0.0),
        min_start_size=1.6, max_start_size=2.1,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (1.0, 1.1)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_ORANGE, 0.0)),
            (0.35, rgba(FIRE_YELLOW, 0.15)),
            (1.00, rgba(FIRE_ORANGE, 0.0)),
        ),
    ))

    # 4. Embers -- a few sparks drifting up for seconds; still visible above a character in front.
    rate, life = 5.0, 5.0
    emitters.append(Emitter(
        name="Embers",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=5.0,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.45),
        min_lifetime=3.0, max_lifetime=life,
        min_velocity=(-0.05, 0.45, -0.05), max_velocity=(0.05, 0.75, 0.05),
        min_start_size=0.045, max_start_size=0.075,
        gravity=(0.0, 0.18, 0.0), drag=0.25,
        noise_amplitude=0.30, noise_frequency=0.9,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.45)),
        color_over_lifetime=color_curve(
            (0.00, rgba(PALE_HOT, 0.0)),
            (0.06, rgba(PALE_HOT, 1.0)),
            (0.45, rgba(EMBER, 0.9)),
            (1.00, rgba(FIRE_ORANGE, 0.0)),
        ),
    ))

    return ParticleSystem(emitters=emitters)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("data", "client", "Particles", "Haven_BrazierFlame.hpar")
    hpar.save(build(), out)
    print("wrote", out)


if __name__ == "__main__":
    main()
