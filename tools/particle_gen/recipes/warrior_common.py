# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shared palette and emitter building blocks for the warrior ability effects.

Art direction is **stylized high fantasy**, matching the ability audio: physical weapon
impacts fused with arcane energy, in the register of a AAA fantasy MMO.

Two engine constraints shape every number in here, and the second one is why this file
looks the way it does:

* ``Particles/Additive.hmat`` is typed ``Unlit``, which makes the engine render it with
  opaque blending -- hard occluding rectangles. Only the ``.hmi`` instances below are safe.
* There is **no additive blending and no bloom**. This is the hard part of a stylized
  brief: overlapping particles do not accumulate toward white, so the usual way of drawing
  magic -- a few bright, high-alpha sprites -- composites into flat coloured cardboard.

Everything here is built around four techniques that *do* survive alpha-only blending:

1. **Density over alpha.** Many particles at peak alpha 0.2-0.45, never few at 0.8+. The
   apparent brightness comes from overlap count, not from any single particle.
2. **Saturated hue at low alpha.** A 0.25-alpha saturated violet reads as energy; the same
   colour at 0.9 reads as plastic.
3. **Motion carries the magic.** Orbital swirl, expanding rings, stretched beams and
   spinning star sprites all read as arcane and cost nothing the renderer cannot do. Shape
   is free; glow is not.
4. **Hue shift over life.** Hot near-white core -> saturated body, with the exit handled by
   alpha falling to zero rather than by a third darker colour. That two-colour shift plus
   the alpha fade sells energy dissipating, which a static bright blob cannot give.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import hpar
from hpar import Burst, Emitter, ParticleSystem, color_curve, float_curve

# Translucent, depth-write off, soft DXT5 sprites.
BEAM = "Particles/Particle_Beam.hmi"     # streak, for RENDER_STRETCHED
GLOW = "Particles/Particle_Glow.hmi"     # soft radial blob
RING = "Particles/Particle_Ring.hmi"     # hollow ring, for ground shockwaves
STAR = "Particles/Particle_Star.hmi"     # four-point star

# Palette. Every hue is pushed well past its realistic value: with no additive blending,
# a saturated colour at low alpha is the only way to read as energy rather than as matter.
# Each effect pairs a HOT core with a SATURATED body and a DARK fade.
HOT = (1.00, 0.98, 0.90)          # near-white core, every energy effect starts here
ARC_STEEL = (0.78, 0.92, 1.00)    # cold enchanted-blade energy
ARC_GOLD = (1.00, 0.84, 0.38)     # heroic / rally
ARC_EMBER = (1.00, 0.55, 0.15)    # embers and sparks
ARC_CRIMSON = (1.00, 0.28, 0.20)  # rage energy
ARC_BLOOD = (0.82, 0.10, 0.14)    # stylized wound energy, brighter than real blood
ARC_VIOLET = (0.60, 0.34, 0.95)   # dread / debuff magic
ARC_AZURE = (0.42, 0.70, 1.00)    # protective barrier
DUST = (0.62, 0.56, 0.46)         # ground contact only; lifted so it reads under energy
DUST_DARK = (0.40, 0.35, 0.30)

OUT_DIR = os.path.join("data", "client", "Particles", "Warrior")


def rgba(rgb, a):
    return (rgb[0], rgb[1], rgb[2], a)


def spark_burst(name, count, speed, colour, size=0.10, lifetime=0.45,
                gravity=-9.0, drag=1.6, spread=1.0):
    """A single burst of stretched sparks thrown radially and killed by gravity and drag.

    ``spread`` biases the velocity box: 1.0 is omnidirectional, smaller values tighten the
    cone so the spray reads as directional.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,     # sparks stay where they were thrown
        loop=False, duration=0.10,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.14, 0.0, 0.0),
        min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
        min_velocity=(-spread, 0.1, -spread), max_velocity=(spread, spread * 1.6, spread),
        min_start_speed=speed * 0.5, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, gravity, 0.0), drag=drag,
        render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.2)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 1.0)),
            (0.45, rgba(colour, 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def ground_ring(name, start_size, end_size, colour, alpha=0.35, lifetime=0.5, delay=0.0):
    """A flat expanding ring on the ground. This is what anchors an effect to the world.

    Lifted a hair off the floor so it does not z-fight the terrain, and given a random start
    rotation so repeated pulses do not visibly stamp the same image.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.10, start_delay=delay,
        spawn_rate=0.0, max_particles=4,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime * 0.85, max_lifetime=lifetime,
        min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.10, 0.0),
        min_start_size=start_size, max_start_size=start_size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=RING,
        size_over_life=float_curve((0.0, 1.0), (1.0, end_size / max(start_size, 1e-6))),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.15, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def dust_cloud(name, count, spread, colour, alpha=0.30, size=0.5, lifetime=0.8, rise=0.4):
    """Slow, soft, low-alpha billboards -- the body of any dust or pressure effect.

    Alpha stays low by design: with no additive blending, stacking opaque-ish dust reads as
    a solid wall instead of a cloud.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.18,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(spread * 0.35, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-spread, 0.0, -spread), max_velocity=(spread, rise, spread),
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-0.8, max_angular_velocity=0.8,
        gravity=(0.0, -0.4, 0.0), drag=1.2,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.20, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def soft_flash(name, size, colour, alpha=0.5, lifetime=0.12):
    """One brief soft blob at the origin. Gives an impact an onset instead of a fade-in.

    The default alpha sits above the 0.2-0.45 volumetric budget for the same reason a spark
    does: this is a single short-lived particle, so nothing accumulates against it. The
    budget exists for layers that overlap each other, not for one blob lasting 0.12s.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.05,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=size, max_start_size=size,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 1.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def energy_swirl(name, count, radius, colour, hot=HOT, alpha=0.38, size=0.16,
                 lifetime=0.65, orbital=3.2, rise=1.4):
    """Orbiting stretched motes -- the main "this is magic" primitive.

    Orbital motion is what sells arcane energy in a renderer that cannot glow: the eye reads
    the spiral, not the brightness. Colour runs hot core -> saturated body -> dark fade so
    the swirl looks like energy dissipating rather than confetti falling.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,     # swirl follows the actor
        loop=False, duration=0.30,
        spawn_rate=0.0, max_particles=count + 10,
        bursts=[Burst(0.0, count // 2), Burst(0.12, count - count // 2)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-0.25, rise * 0.4, -0.25), max_velocity=(0.25, rise, 0.25),
        min_start_size=size * 0.55, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-2.2, max_angular_velocity=2.2,
        gravity=(0.0, 0.2, 0.0), drag=0.6, orbital_speed=orbital,
        render_mode=hpar.RENDER_STRETCHED, length_scale=4.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.15)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.15, rgba(hot, alpha)),
            (0.60, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))
