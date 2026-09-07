# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Recipe for ``data/client/Particles/LevelUp.hpar`` -- the golden column that plays on the
player when they gain a level.

Run from the repo root::

    python tools/particle_gen/recipes/level_up.py

Scale reference: the player capsule is ~1.8 world units tall, so the column reads as
"about three times the character" at its peak. The system is anchored at the unit's feet
(scene-node origin) and every emitter is one-shot, so the whole thing self-terminates.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import hpar
from hpar import Burst, Emitter, ParticleSystem, color_curve, float_curve

# Material instances of Particles/Particle_Alpha_Tex.hmat with soft DXT5 sprites and
# depth-write off. NOT Particles/Additive.hmat -- that one is typed Unlit, so the engine
# renders it with opaque blending and every particle shows up as a hard occluding quad.
# The engine has no additive blend state at all, so all of this is straight alpha blending:
# overlapping particles do not accumulate toward white, and each one has to carry its own
# brightness.
BEAM = "Particles/Particle_Beam.hmi"     # vertical soft streak, for RENDER_STRETCHED
GLOW = "Particles/Particle_Glow.hmi"     # soft radial blob
STAR = "Particles/Particle_Star.hmi"     # four-point star

# Yellow, not fire. The green channel is what separates the two: dropping G below ~0.8
# swings the hue into orange, and with alpha blending there is no white accumulation to pull
# it back, so the whole column reads as flame.
WHITE_HOT = (1.00, 1.00, 0.92)
YELLOW = (1.00, 0.94, 0.50)
AMBER = (1.00, 0.86, 0.34)


def rgba(rgb, a):
    return (rgb[0], rgb[1], rgb[2], a)


def build():
    emitters = []

    # -------------------------------------------------------------------------------------
    # 1. Core beam -- the thin white-hot shaft at the centre. Narrow, fast and very
    #    stretched, so it reads as a continuous column of light rather than as particles.
    # -------------------------------------------------------------------------------------
    emitters.append(Emitter(
        name="Column Core",
        simulation_space=hpar.SIM_LOCAL,
        loop=False,
        duration=1.25,
        spawn_rate=110.0,
        max_particles=200,
        shape=hpar.SHAPE_CONE,
        shape_extents=(0.03, 0.12, 0.22),   # (angle, height, base radius)
        min_lifetime=0.55, max_lifetime=0.85,
        min_velocity=(0.0, 2.4, 0.0), max_velocity=(0.0, 5.0, 0.0),
        min_start_size=0.20, max_start_size=0.28,
        gravity=(0.0, 0.0, 0.0),
        orbital_speed=0.6,
        render_mode=hpar.RENDER_STRETCHED,
        length_scale=13.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.55), (0.25, 1.0), (1.0, 0.75)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.12, rgba(WHITE_HOT, 0.60)),
            (0.75, rgba(YELLOW, 0.42)),
            (1.00, rgba(AMBER, 0.0)),
        ),
    ))

    # -------------------------------------------------------------------------------------
    # 2. Outer column -- wider, slower, swirling gold sheath around the core. This is what
    #    gives the effect its body and its silhouette from a distance.
    # -------------------------------------------------------------------------------------
    emitters.append(Emitter(
        name="Column Glow",
        simulation_space=hpar.SIM_LOCAL,
        loop=False,
        duration=1.35,
        spawn_rate=150.0,
        max_particles=340,
        shape=hpar.SHAPE_CONE,
        shape_extents=(0.05, 0.15, 0.80),
        min_lifetime=0.85, max_lifetime=1.30,
        min_velocity=(0.0, 1.1, 0.0), max_velocity=(0.0, 3.4, 0.0),
        min_start_size=0.26, max_start_size=0.40,
        gravity=(0.0, 0.0, 0.0),
        orbital_speed=1.4,
        render_mode=hpar.RENDER_STRETCHED,
        length_scale=8.5,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.85)),
        color_over_lifetime=color_curve(
            (0.00, rgba(YELLOW, 0.0)),
            (0.20, rgba(YELLOW, 0.34)),
            (0.80, rgba(YELLOW, 0.20)),
            (1.00, rgba(AMBER, 0.0)),
        ),
    ))

    # -------------------------------------------------------------------------------------
    # 3. Ground flare -- flat discs at the feet that expand and fade. Sells the column as
    #    something coming *out of the ground* instead of floating.
    # -------------------------------------------------------------------------------------
    emitters.append(Emitter(
        name="Ground Flare",
        simulation_space=hpar.SIM_LOCAL,
        loop=False,
        duration=0.95,
        spawn_rate=0.0,
        max_particles=12,
        bursts=[Burst(0.0, 2), Burst(0.30, 2), Burst(0.65, 2)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=0.55, max_lifetime=0.75,
        min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.12, 0.0),
        min_start_size=1.5, max_start_size=2.0,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-0.9, max_angular_velocity=0.9,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.35), (0.35, 1.0), (1.0, 1.9)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.55)),
            (0.30, rgba(YELLOW, 0.35)),
            (1.00, rgba(AMBER, 0.0)),
        ),
    ))

    # -------------------------------------------------------------------------------------
    # 4. Impact sparks -- a single burst at t=0 that throws stretched sparks outward and up.
    #    Short-lived; its whole job is to give the effect an onset instead of a fade-in.
    # -------------------------------------------------------------------------------------
    emitters.append(Emitter(
        name="Burst Sparks",
        simulation_space=hpar.SIM_WORLD,
        loop=False,
        duration=0.15,
        spawn_rate=0.0,
        max_particles=70,
        bursts=[Burst(0.0, 55)],
        shape=hpar.SHAPE_SPHERE,
        shape_extents=(0.22, 0.0, 0.0),
        min_lifetime=0.40, max_lifetime=0.85,
        min_velocity=(-1.2, 1.5, -1.2), max_velocity=(1.2, 4.0, 1.2),
        min_start_speed=2.5, max_start_speed=6.5,
        min_start_size=0.09, max_start_size=0.15,
        gravity=(0.0, -7.0, 0.0),
        drag=1.6,
        render_mode=hpar.RENDER_STRETCHED,
        length_scale=5.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.25)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 1.0)),
            (0.55, rgba(YELLOW, 0.9)),
            (1.00, rgba(AMBER, 0.0)),
        ),
    ))

    # -------------------------------------------------------------------------------------
    # 5. Rising motes -- textured star sprites that drift up and outlive the column, so the
    #    effect trails off instead of cutting out. Alpha-blended, not additive, so the stars
    #    stay readable as individual shapes against the bloom.
    # -------------------------------------------------------------------------------------
    emitters.append(Emitter(
        name="Rising Motes",
        simulation_space=hpar.SIM_LOCAL,
        loop=False,
        duration=1.60,
        spawn_rate=30.0,
        max_particles=70,
        shape=hpar.SHAPE_SPHERE,
        shape_extents=(0.75, 0.0, 0.0),
        min_lifetime=1.10, max_lifetime=1.90,
        min_velocity=(-0.15, 0.7, -0.15), max_velocity=(0.15, 1.5, 0.15),
        min_start_size=0.16, max_start_size=0.30,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.6, max_angular_velocity=1.6,
        gravity=(0.0, 0.35, 0.0),
        drag=0.4,
        orbital_speed=0.7,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.2), (0.25, 1.0), (1.0, 0.0)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.18, rgba(WHITE_HOT, 0.95)),
            (0.75, rgba(YELLOW, 0.8)),
            (1.00, rgba(AMBER, 0.0)),
        ),
    ))

    return ParticleSystem(emitters=emitters)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("data", "client", "Particles", "LevelUp.hpar")
    system = build()
    hpar.save(system, out)
    print("wrote %s (%d emitters, %d bytes)" % (out, len(system.emitters), os.path.getsize(out)))
