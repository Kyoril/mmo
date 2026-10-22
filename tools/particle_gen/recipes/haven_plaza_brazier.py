# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Recipe for ``data/client/Particles/Haven_BrazierFlame.hpar`` -- the calm, endlessly looping
fire in the brazier at the centre of Haven's plaza (the character-select gathering spot).

Run from the repo root::

    python tools/particle_gen/recipes/haven_plaza_brazier.py

Placed by ``Models/Haven/Props/Haven_PlazaBrazier.hwmo`` as a particle-system mesh reference
(scale 1, so the numbers below are world units) on the coal bed of ``SM_hc_Brazier_B`` at 2x:
that bowl is ~2.1 m wide with its rim ~0.96 m above the ground, and the reference sits at
y = 0.78, a little below the rim, so the flames grow out of the bowl rather than float on it.

The first version used the teardrop FLAME sprite and one orange ramp: in game it read as a
flat cluster of hard, uniform spikes. This one is built from ``Particle_FireLick`` -- soft,
noise-eroded flame licks, four variants in a 4x1 sheet picked at random per particle
(``make_sprites.py T_Particle_FireLick``) -- with lifetime, size and speed spread, and a
yellow-white -> orange -> deep red ramp so the base burns hot and the tips cool and vanish.

Layers (all looping, ``SIM_WORLD`` so the fire does not drag along if the placement moves):

* **Flame licks** -- the body and silhouette. STRETCHED at length_scale 2 (the frames are
  1:2), so a lick leans with its velocity and the noise force makes each one sway.
* **Inner licks** -- smaller, paler, shorter-lived licks in the middle: the hot heart.
* **Hot bed** -- soft yellow-white blobs low over the coals that flicker in and out. The
  engine has no additive blending or bloom; this is the stand-in for a bloom core.
* **Heat glow** -- a few large, very faint blobs around the whole fire.
* **Smoke wisp** -- thin dark puffs, invisible for the first third of their life (while
  they rise through the flames), then a barely-there grey wisp curling up above.
* **Embers** -- many tiny bright sparks with spread speeds and lifetimes, drifting on noise
  and flickering (their alpha ramp pulses), the part still visible above a character.

Roughly 150 live particles in total.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import hpar  # noqa: E402
from hpar import Emitter, ParticleSystem, color_curve, float_curve  # noqa: E402

# The colours below look pale on paper: in game the particle colour comes out noticeably
# redder than these values (an orange of G 0.64 rendered as red), so every key keeps more
# green than its intended hue. Judge them on a --capture-view shot, not on the preview sheet.

FIRE_LICK = "Particles/Particle_FireLick.hmi"   # 4x1 sheet of soft ragged flame licks (1:2 frames)
GLOW = "Particles/Particle_Glow.hmi"             # soft radial blob
SMOKE = "Particles/Particle_Smoke.hmi"           # lumpy smoke puff

WHITE_HOT = (1.00, 0.98, 0.90)
YELLOW = (1.00, 0.92, 0.58)
ORANGE = (1.00, 0.78, 0.36)
DEEP_RED = (0.92, 0.44, 0.14)
EMBER_HOT = (1.00, 0.95, 0.75)
EMBER = (1.00, 0.84, 0.48)
SMOKE_GREY = (0.16, 0.14, 0.13)


def rgba(rgb, a):
    return (rgb[0], rgb[1], rgb[2], a)


def budget(rate, max_lifetime, margin=1.3):
    """max_particles that can never truncate a continuous emitter."""
    return int(rate * max_lifetime * margin) + 4


def fire_ramp(alpha):
    """Hot at the base, cooling through orange to a deep red that is already fading out."""
    return color_curve(
        (0.00, rgba(WHITE_HOT, 0.0)),
        (0.10, rgba(WHITE_HOT, alpha)),
        (0.30, rgba(YELLOW, alpha)),
        (0.62, rgba(ORANGE, alpha * 0.65)),
        (0.85, rgba(DEEP_RED, alpha * 0.15)),
        (1.00, rgba(DEEP_RED, 0.0)),
    )


def build():
    emitters = []

    # 1. Flame licks -- the body of the fire.
    rate, life = 30.0, 1.25
    emitters.append(Emitter(
        name="Flame Licks",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=1.5,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.48),
        min_lifetime=0.65, max_lifetime=life,
        min_velocity=(-0.05, 0.25, -0.05), max_velocity=(0.05, 0.60, 0.05),
        min_start_size=0.26, max_start_size=0.50,
        gravity=(0.0, 0.55, 0.0),
        noise_amplitude=0.45, noise_frequency=1.5,
        sprite_sheet_columns=4, sprite_sheet_rows=1, sprite_animation=hpar.SPRITE_RANDOM_STATIC,
        render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
        material_name=FIRE_LICK,
        # Swells out of the coals, then thins away as it rises.
        size_over_life=float_curve((0.0, 0.5), (0.25, 1.0), (0.7, 0.85), (1.0, 0.45)),
        color_over_lifetime=fire_ramp(0.55),
    ))

    # 2. Inner licks -- the paler, hotter heart.
    rate, life = 18.0, 0.85
    emitters.append(Emitter(
        name="Inner Licks",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=1.5,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.26),
        min_lifetime=0.45, max_lifetime=life,
        min_velocity=(-0.03, 0.25, -0.03), max_velocity=(0.03, 0.45, 0.03),
        min_start_size=0.20, max_start_size=0.34,
        gravity=(0.0, 0.45, 0.0),
        noise_amplitude=0.30, noise_frequency=1.8,
        sprite_sheet_columns=4, sprite_sheet_rows=1, sprite_animation=hpar.SPRITE_RANDOM_STATIC,
        render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
        material_name=FIRE_LICK,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.12, rgba(WHITE_HOT, 0.75)),
            (0.55, rgba(YELLOW, 0.50)),
            (1.00, rgba(ORANGE, 0.0)),
        ),
    ))

    # 3. Hot bed -- flickering bright blobs low over the coals (the "bloom" core).
    rate, life = 7.0, 0.9
    emitters.append(Emitter(
        name="Hot Bed",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=1.0,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.35),
        min_lifetime=0.5, max_lifetime=life,
        min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.20, 0.0),
        min_start_size=0.7, max_start_size=1.1,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (1.0, 1.15)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.30, rgba(WHITE_HOT, 0.32)),
            (1.00, rgba(YELLOW, 0.0)),
        ),
    ))

    # 4. Heat glow -- large, very faint halo around the whole fire.
    rate, life = 2.5, 1.8
    emitters.append(Emitter(
        name="Heat Glow",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=2.0,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.25),
        min_lifetime=1.3, max_lifetime=life,
        min_velocity=(0.0, 0.20, 0.0), max_velocity=(0.0, 0.35, 0.0),
        min_start_size=1.7, max_start_size=2.2,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (1.0, 1.1)),
        color_over_lifetime=color_curve(
            (0.00, rgba(ORANGE, 0.0)),
            (0.35, rgba(YELLOW, 0.08)),
            (1.00, rgba(ORANGE, 0.0)),
        ),
    ))

    # 5. Smoke wisp -- hidden while it rises through the flames, then a faint dark curl above.
    rate, life = 2.2, 6.0
    emitters.append(Emitter(
        name="Smoke Wisp",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=6.0,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.20),
        min_lifetime=4.0, max_lifetime=life,
        min_velocity=(-0.04, 0.45, -0.04), max_velocity=(0.04, 0.65, 0.04),
        min_start_size=0.35, max_start_size=0.55,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-0.4, max_angular_velocity=0.4,
        gravity=(0.0, 0.0, 0.0), drag=0.12,
        noise_amplitude=0.30, noise_frequency=0.6,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=SMOKE,
        size_over_life=float_curve((0.0, 0.6), (1.0, 2.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(SMOKE_GREY, 0.0)),
            (0.28, rgba(SMOKE_GREY, 0.0)),
            (0.45, rgba(SMOKE_GREY, 0.16)),
            (0.75, rgba(SMOKE_GREY, 0.08)),
            (1.00, rgba(SMOKE_GREY, 0.0)),
        ),
    ))

    # 6. Embers -- many tiny sparks with spread speeds, drifting and flickering.
    rate, life = 13.0, 4.5
    emitters.append(Emitter(
        name="Embers",
        simulation_space=hpar.SIM_WORLD,
        loop=True, duration=2.0, warmup_time=4.5,
        spawn_rate=rate, max_particles=budget(rate, life),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.50),
        min_lifetime=1.4, max_lifetime=life,
        min_velocity=(-0.12, 0.30, -0.12), max_velocity=(0.12, 1.25, 0.12),
        min_start_size=0.022, max_start_size=0.050,
        gravity=(0.0, 0.22, 0.0), drag=0.40,
        noise_amplitude=0.55, noise_frequency=1.6,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
        # The pulsing alpha is the flicker; spread lifetimes keep the pulses out of step.
        color_over_lifetime=color_curve(
            (0.00, rgba(EMBER_HOT, 0.0)),
            (0.05, rgba(EMBER_HOT, 1.0)),
            (0.18, rgba(EMBER_HOT, 0.55)),
            (0.30, rgba(YELLOW, 1.0)),
            (0.45, rgba(EMBER, 0.50)),
            (0.58, rgba(EMBER, 0.95)),
            (0.72, rgba(EMBER, 0.40)),
            (0.85, rgba(ORANGE, 0.75)),
            (1.00, rgba(ORANGE, 0.0)),
        ),
    ))

    return ParticleSystem(emitters=emitters)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("data", "client", "Particles", "Haven_BrazierFlame.hpar")
    hpar.save(build(), out)
    print("wrote", out)


if __name__ == "__main__":
    main()
