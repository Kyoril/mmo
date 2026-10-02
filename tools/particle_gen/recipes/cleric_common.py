# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shared palette and emitter building blocks for the cleric spell effects.

Art direction is **radiant stylized holy light**: warm gold and ivory light that descends
from above (heals, smites, resurrection), consecration sigils drawn on the ground (auras,
blessings), and a hotter amber-white for holy fire. Damage reads sharper and brighter,
healing reads softer and slower, protection leans cool silver-azure. Every cleric effect
shares the gold core so the class is recognisable at a glance.

The renderer constraints from ``warrior_common`` apply unchanged -- read its docstring:
no additive blending, no bloom, only the translucent ``.hmi`` sprite materials are safe.
Density at low alpha, saturated hue, and motion carry the look.

What the cleric adds on top of the warrior toolkit are **shaped sprites** (built by
``make_sprites.py``): a rune circle for ground sigils, a sunburst for divine flashes, a
feather, and a flame tongue. Shape is the one thing this renderer gives for free, so the
holy identity leans on it.

Looping emitters are allowed **only** in effects the visualization service tears down
itself: cast-phase kits (START_CAST/CASTING, destroyed at CAST_SUCCEEDED/CANCEL_CAST) and
AURA_IDLE kits (destroyed when the aura is removed). Everything spawned by CAST_SUCCEEDED,
IMPACT, AURA_APPLIED or AURA_TICK must be one-shot, or it leaks for the life of the unit.
``LOOPING_EFFECTS`` below is the allow-list the data test checks against.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc  # noqa: E402
from warrior_common import (  # noqa: E402,F401  (re-exported for the cleric recipes)
    BEAM, GLOW, RING, STAR, HOT, Burst, Emitter, ParticleSystem, color_curve, float_curve,
    hpar, rgba, spark_burst, ground_ring, soft_flash, energy_swirl, dust_cloud)

# Shaped sprites (make_sprites.py), all on Particle_Alpha_Tex.hmat: translucent, no depth write.
SIGIL = "Particles/Particle_Sigil.hmi"      # rune circle, for RENDER_HORIZONTAL ground glyphs
RAYS = "Particles/Particle_Rays.hmi"        # sunburst, for billboarded divine flashes
FEATHER = "Particles/Particle_Feather.hmi"  # soft feather, tip up
FLAME = "Particles/Particle_Flame.hmi"      # teardrop flame tongue, tip up

# Palette. Pushed past realistic values for the same reason as the warrior palette.
HOLY_WHITE = (1.00, 0.98, 0.88)   # hot core of every cleric effect
HOLY_GOLD = (1.00, 0.82, 0.36)    # the class colour
SOFT_GOLD = (1.00, 0.90, 0.58)    # gentle healing body
HEAL_GREEN = (0.72, 1.00, 0.55)   # life accent, used sparingly inside heals
AMBER_FIRE = (1.00, 0.62, 0.18)   # holy fire body
FIRE_CORE = (1.00, 0.92, 0.62)    # holy fire core, never darker than this (else it reads as plain fire)
WARD_SILVER = (0.80, 0.90, 1.00)  # protection
WARD_AZURE = (0.50, 0.74, 1.00)   # protection accent

ROOT = wc.ROOT
OUT_DIR = os.path.join(ROOT, "data", "client", "Particles", "Cleric")

# Effects allowed to contain looping emitters (see module docstring). Anything not listed
# here must pass ``inspect_hpar.py --check --one-shot``.
LOOPING_EFFECTS = {
    "HolyHandGlow.hpar",
    "SmiteGather.hpar",
    "HolyFireGather.hpar",
    "ResurrectionChannel.hpar",
    "HolyFireBurn.hpar",
    "RenewingLightIdle.hpar",
    "FaithwardIdle.hpar",
    "HealingAuraIdle.hpar",
    "ProtectiveAuraIdle.hpar",
}


def light_shaft(name, colour, hot=HOLY_WHITE, height=6.0, radius=0.35, count=40,
                speed=14.0, size=0.22, lifetime=0.42, alpha=0.40, delay=0.0):
    """Stretched streaks pouring down a column onto the origin -- light called from above.

    Emitters have no per-emitter offset, so "spawn high up" uses the **cone** shape: the
    engine spawns cone particles at ``y = height * t`` with radius ``baseRadius * t``
    (``particle_emitter.cpp``), i.e. a column standing on the origin and widening upward.
    Driving them straight down with RENDER_STRETCHED turns the speed into streak length; a
    lifetime around ``height / speed`` lets the top ones reach the ground. Start speed stays
    zero -- the cone's spawn direction points outward/up and would fight the fall.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.20, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count // 2), Burst(0.08, count - count // 2)],
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, height, radius),
        min_lifetime=lifetime * 0.8, max_lifetime=lifetime,
        min_velocity=(0.0, -speed, 0.0), max_velocity=(0.0, -speed * 0.75, 0.0),
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.10, rgba(hot, alpha)),
            (0.70, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def ground_sigil(name, size, colour, alpha=0.55, lifetime=1.2, spin=0.6, grow=1.15, delay=0.0):
    """A rune circle drawn on the ground that fades in, turns slowly and fades out.

    Thin strokes survive alpha blending far better than filled discs, so this sprite may run
    brighter than the 0.2-0.45 volumetric budget -- it is one particle, nothing stacks.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.04, 0.0),
        min_start_size=size, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=spin, max_angular_velocity=spin,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=SIGIL,
        size_over_life=float_curve((0.0, 0.85), (0.25, 1.0), (1.0, grow)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.18, rgba(colour, alpha)),
            (0.70, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def ray_flash(name, size, colour, alpha=0.65, lifetime=0.30, spin=1.5, delay=0.0):
    """One sunburst billboard that pops and spins -- radiance at the moment of impact."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=size, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=spin, max_angular_velocity=spin,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=RAYS,
        size_over_life=float_curve((0.0, 0.4), (0.25, 1.0), (1.0, 1.25)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, alpha)),
            (0.40, rgba(colour, alpha * 0.7)),
            (1.00, rgba(colour, 0.0))),
    )


def rising_motes(name, colour, hot=HOLY_WHITE, count=24, radius=0.45, rise=1.6, size=0.12,
                 lifetime=1.2, alpha=0.45, orbital=1.2, loop=False, rate=0.0, duration=0.4,
                 material=STAR, delay=0.0):
    """Slow sparkles lifting around the body. One-shot burst by default; ``loop=True`` with a
    ``rate`` turns it into an idle/channel emitter (only for LOOPING_EFFECTS)."""
    bursts = [] if loop else [Burst(0.0, count // 2), Burst(duration * 0.5, count - count // 2)]
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=loop, duration=duration, start_delay=delay,
        spawn_rate=rate, max_particles=max(count, int(rate * lifetime * 1.3)) + 8,
        bursts=bursts,
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-0.1, rise * 0.5, -0.1), max_velocity=(0.1, rise, 0.1),
        min_start_size=size * 0.5, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.5, max_angular_velocity=1.5,
        gravity=(0.0, 0.0, 0.0), drag=0.4, orbital_speed=orbital,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=material,
        size_over_life=float_curve((0.0, 0.3), (0.25, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.20, rgba(hot, alpha)),
            (0.60, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))
