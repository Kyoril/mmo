# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Frost-school mage effects. Run from the repo root::

    py -3.14 tools/particle_gen/recipes/mage_frost.py

Writes eleven systems into ``data/client/Particles/Mage/``: five one-shots (impacts, Frost
Nova, Frost Armor's cast burst, the Chilled puff) and six loops (the hand channel, two
projectile trails, and three aura idles).

Art direction: **crystalline, cold, magical frost** in a stylized high-fantasy register --
readable at WoW distances, not realistic. Every effect is built from the same four
ingredients so the whole school reads as one family:

* **White-hot core** (``FROST_HOT``) -- onset flashes and the brightest single sprites.
* **Pale cyan body** (``FROST_ICE``) and **deep azure accent** (``FROST_DEEP``) -- the hue
  shift over life that sells energy dissipating (see ``warrior_common``'s technique 4).
* **Crystal geometry** -- star sprites spinning like snowflakes, and sharp ``RENDER_STRETCHED``
  beam shards with a high ``length_scale`` for ice splinters.
* **Low cold mist** (``FROST_MIST``) -- soft glows that sink rather than rise, so frost
  never reads as smoke or steam.

There is no additive blending and no bloom (see ``warrior_common``), so mist and swirl
layers stay at peak alpha 0.2-0.45 and density carries the brightness; only single flashes,
shards and stars run brighter.

Notes that are not obvious from the parameters:

* **Flat radial throws use a zero-height cone.** ``SHAPE_CONE`` with ``height = 0`` spawns
  on a disc and its outward direction is horizontal, so ``start_speed`` throws particles
  flat along the ground -- the Frost Nova shards and rolling mist depend on it. A sphere
  would throw half of them into the floor and half into the sky.
* **Projectile trails lean on ``inherit_velocity``.** The engine derives the system's
  velocity from the node's frame-to-frame movement and adds ``inherit_velocity`` times that
  to every world-space spawn. Trail shards inherit a fraction of the flight speed, which
  aligns their stretch with the flight path (a stretched sprite points along its own
  velocity) without needing to know the projectile node's orientation -- ``face_movement``
  is optional per projectile visual, so the local axes are not reliable.
* **World-space orbital spins around the emitter's current position.** On a moving node
  that would sweep old trail particles around the projectile, so every world-space trail
  emitter here has ``orbital_speed = 0``; the orbiting stars are local-space.
* **Stationary loops warm up** (``_warm``) so an aura appears in steady state instead of
  fading in over its first lifetime; the trails deliberately do not.
* **Loops have no pulse by construction.** Each looping emitter spawns continuously, and
  its colour curve fades in and out, so the live population is constant. Steady-state live
  counts are documented on each loop's function.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mage_common as mc
from mage_common import (
    BEAM, GLOW, RING, STAR, FROST_HOT, FROST_ICE, FROST_DEEP, FROST_MIST,
    Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar, rgba, looping, write)


# --- Building blocks -----------------------------------------------------------------------

def _burst_emitter(name, count, **kw):
    """A one-shot burst emitter with frost-friendly defaults (no gravity, no pop-in)."""
    params = dict(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.10,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_POINT,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        gravity=(0.0, 0.0, 0.0),
        material_name=GLOW,
    )
    params.update(kw)
    return Emitter(**params)


def _fade(colour, alpha, peak=0.15, end_colour=None, hold=None):
    """Fade in to ``alpha`` at ``peak``, optionally hold, then fade to zero while shifting
    toward ``end_colour``."""
    end_colour = end_colour or colour
    keys = [(0.0, rgba(colour, 0.0)), (peak, rgba(colour, alpha))]
    if hold is not None:
        keys.append((hold, rgba(end_colour, alpha * 0.8)))
    keys.append((1.0, rgba(end_colour, 0.0)))
    return color_curve(*keys)


def _flash(name, size, colour, alpha, lifetime, lift=0.0, delay=0.0):
    """One soft onset blob. ``lift`` raises it off a feet-level origin: the particle is
    shot upward and stopped by heavy drag, travelling roughly ``lift`` units."""
    drag = 14.0
    return _burst_emitter(
        name, 1, max_particles=2, start_delay=delay, duration=0.05,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, lift * drag, 0.0), max_velocity=(0.0, lift * drag, 0.0),
        drag=drag if lift > 0 else 0.0,
        min_start_size=size, max_start_size=size,
        render_mode=hpar.RENDER_BILLBOARD,
        size_over_life=float_curve((0.0, 0.55), (0.3, 1.0), (1.0, 1.25)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, alpha)),
            (0.35, rgba(colour, alpha * 0.7)),
            (1.00, rgba(colour, 0.0))))


def _ring(name, start, end, colour, alpha, lifetime, delay=0.0, horizontal=True,
          growth=0.35, hold=0.8):
    """An expanding ring. Size and alpha are shaped together so most of the growth
    happens while the ring is still bright (the ``_fast_ring`` lesson from the warrior
    pass). ``horizontal=False`` makes a camera-facing shockwave for torso impacts."""
    ratio = end / start
    return _burst_emitter(
        name, 1, max_particles=3, start_delay=delay,
        simulation_space=hpar.SIM_LOCAL,
        min_lifetime=lifetime * 0.95, max_lifetime=lifetime,
        min_velocity=(0.0, 0.04 if horizontal else 0.0, 0.0),
        max_velocity=(0.0, 0.04 if horizontal else 0.0, 0.0),
        min_start_size=start, max_start_size=start,
        min_start_rotation=0.0, max_start_rotation=3.14,
        render_mode=hpar.RENDER_HORIZONTAL if horizontal else hpar.RENDER_BILLBOARD,
        material_name=RING,
        size_over_life=float_curve((0.0, 1.0), (growth, 1.0 + (ratio - 1.0) * hold), (1.0, ratio)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.0)),
            (0.08, rgba(FROST_HOT, alpha)),
            (growth, rgba(colour, alpha * 0.75)),
            (1.00, rgba(colour, 0.0))))


def _shards(name, count, speed, size, lifetime, hot=FROST_HOT, body=FROST_ICE, alpha=0.95,
            length=6.0, gravity=-6.0, drag=2.2, **kw):
    """Sharp ice splinters: stretched beams, white-hot at birth, cyan as they die."""
    params = dict(
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
        min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
        min_velocity=(-0.6, -0.3, -0.6), max_velocity=(0.6, 0.9, 0.6),
        min_start_speed=speed * 0.45, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, gravity, 0.0), drag=drag,
        render_mode=hpar.RENDER_STRETCHED, length_scale=length,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (0.6, 0.8), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, alpha)),
            (0.35, rgba(body, alpha * 0.85)),
            (1.00, rgba(body, 0.0))))
    params.update(kw)
    return _burst_emitter(name, count, **params)


def _snowflakes(name, count, lifetime, size=0.26, alpha=0.8, radius=0.35, fall=-0.7, **kw):
    """Spinning star sprites drifting down -- the "snowflake" accent of the school."""
    params = dict(
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.65, max_lifetime=lifetime,
        min_velocity=(-0.5, -0.1, -0.5), max_velocity=(0.5, 0.6, 0.5),
        min_start_speed=0.8, max_start_speed=2.0,
        min_start_size=size * 0.55, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-3.0, max_angular_velocity=3.0,
        gravity=(0.0, fall, 0.0), drag=2.0,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.5), (0.2, 1.0), (1.0, 0.6)),
        color_over_lifetime=_fade(FROST_HOT, alpha, peak=0.12, end_colour=FROST_ICE, hold=0.55))
    params.update(kw)
    return _burst_emitter(name, count, **params)


def _mist(name, count, lifetime, size, alpha=0.24, spread=0.6, sink=-0.35, **kw):
    """Low cold mist: soft glows that grow and sink. Pale at birth, azure as it fades."""
    params = dict(
        shape=hpar.SHAPE_SPHERE, shape_extents=(spread * 0.4, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-spread, -0.1, -spread), max_velocity=(spread, 0.25, spread),
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-0.6, max_angular_velocity=0.6,
        gravity=(0.0, sink, 0.0), drag=1.4,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.55), (0.35, 1.0), (1.0, 1.6)),
        color_over_lifetime=_fade(FROST_MIST, alpha, peak=0.2, end_colour=FROST_DEEP))
    params.update(kw)
    return _burst_emitter(name, count, **params)


def _warm(system, seconds):
    """Pre-simulate a stationary loop so it appears already in steady state instead of
    fading in. Only for loops on a node that is not moving at spawn: a projectile trail
    must not warm up, or its world-space particles pile up at the launch point."""
    for emitter in system.emitters:
        emitter.warmup_time = seconds
    return system


# --- One-shots -------------------------------------------------------------------------------

def frostbolt_impact():
    """At the target's torso (the system origin is spine_03). A white flash, a cyan
    shockwave ring facing the camera, a spray of ice splinters, a cold mist puff, and
    snowflake stars spinning down after everything else -- ~1.1 s."""
    return ParticleSystem(emitters=[
        _flash("Frostbolt Flash", 1.7, FROST_HOT, 0.8, 0.18),
        _flash("Frostbolt Halo", 1.7, FROST_ICE, 0.45, 0.42),
        _ring("Frostbolt Shock", 0.6, 3.0, FROST_ICE, 0.6, 0.36, horizontal=False),
        _shards("Frostbolt Shards", 32, speed=9.0, size=0.17, lifetime=0.55),
        _mist("Frostbolt Mist", 18, lifetime=1.05, size=1.2, alpha=0.32, spread=1.2,
              color_over_lifetime=_fade(FROST_MIST, 0.32, peak=0.18, end_colour=FROST_ICE)),
        _snowflakes("Frostbolt Flakes", 12, lifetime=1.15, size=0.42, alpha=0.9,
                    min_start_speed=1.2, max_start_speed=2.8),
    ])


def ice_lance_impact():
    """At the target's torso. A hard white shatter: a brief white flash, fast splinters
    biased upward, a tight vertical spike of beams, and a pinch of glitter -- ~0.7 s,
    sharper and whiter than the Frostbolt impact."""
    shatter = _shards("Lance Shatter", 36, speed=11.0, size=0.15, lifetime=0.42,
                      body=FROST_HOT, length=8.0, gravity=-9.0, drag=2.6,
                      min_velocity=(-1.0, 0.4, -1.0), max_velocity=(1.0, 2.8, 1.0))
    spike = _shards("Lance Spike", 10, speed=0.0, size=0.19, lifetime=0.3,
                    length=8.0, gravity=0.0, drag=4.0,
                    min_velocity=(-0.7, 4.5, -0.7), max_velocity=(0.7, 8.0, 0.7),
                    min_start_speed=0.0, max_start_speed=0.0)
    return ParticleSystem(emitters=[
        _flash("Lance Flash", 1.5, FROST_HOT, 0.9, 0.13),
        _ring("Lance Shock", 0.4, 2.2, FROST_HOT, 0.65, 0.24, horizontal=False),
        shatter,
        spike,
        _snowflakes("Lance Glitter", 9, lifetime=0.68, size=0.34, alpha=0.95, radius=0.25,
                    min_start_speed=1.5, max_start_speed=3.2),
        _mist("Lance Mist", 8, lifetime=0.62, size=0.85, alpha=0.26, spread=0.8,
              color_over_lifetime=_fade(FROST_HOT, 0.26, peak=0.15, end_colour=FROST_ICE)),
    ])


def frost_nova():
    """At the caster's feet; spell radius 10. A big fast frost ring sweeping out to ~10
    units, a second bright ring and a deep inner ring behind it, ice shards and cold mist
    thrown flat along the ground (zero-height cone, see module docstring), glitter left in
    the wake, and a flash at the caster -- ~1.4 s."""
    shards = _shards("Nova Shards", 72, speed=26.0, size=0.22, lifetime=0.8,
                     length=6.5, gravity=-3.0, drag=2.4,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.6),
                     min_velocity=(0.0, 0.2, 0.0), max_velocity=(0.0, 1.4, 0.0))
    shards.min_start_speed = 11.0
    mist = _mist("Nova Mist", 52, lifetime=1.35, size=1.8, alpha=0.27, spread=0.0,
                 sink=0.0, drag=1.8,
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.8),
                 min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.5, 0.0),
                 min_start_speed=7.0, max_start_speed=19.0,
                 color_over_lifetime=_fade(FROST_MIST, 0.27, peak=0.15, end_colour=FROST_ICE))
    glitter = _snowflakes("Nova Glitter", 28, lifetime=1.2, size=0.32, alpha=0.85,
                          shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.6),
                          min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 1.2, 0.0),
                          min_start_speed=5.0, max_start_speed=16.0, drag=2.6, fall=-0.4)
    frost = _burst_emitter(
        "Nova Frost Disc", 1, max_particles=3, simulation_space=hpar.SIM_LOCAL,
        min_lifetime=1.3, max_lifetime=1.3,
        min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
        min_start_size=6.0, max_start_size=6.0,
        min_start_rotation=0.0, max_start_rotation=3.14,
        render_mode=hpar.RENDER_HORIZONTAL,
        size_over_life=float_curve((0.0, 0.5), (0.3, 2.4), (1.0, 3.0)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_ICE, 0.0)),
            (0.12, rgba(FROST_ICE, 0.32)),
            (0.45, rgba(FROST_DEEP, 0.2)),
            (1.00, rgba(FROST_DEEP, 0.0))))
    return ParticleSystem(emitters=[
        frost,
        _ring("Nova Ring", 3.0, 30.0, (0.45, 0.78, 1.0), 0.65, 0.9, growth=0.4, hold=0.85),
        _ring("Nova Ring Hot", 2.0, 26.0, FROST_ICE, 0.5, 0.8, delay=0.06, growth=0.42),
        _ring("Nova Ring Inner", 1.5, 13.0, FROST_ICE, 0.45, 0.8, delay=0.14),
        shards,
        mist,
        glitter,
        _flash("Nova Flash", 2.6, FROST_HOT, 0.6, 0.22, lift=0.9),
    ])


def frost_armor_burst():
    """On the caster's feet when Frost Armor is cast. A helix of snowflake stars and cold
    streaks spiralling up around the body, a ground ring, and a low mist -- ~1.2 s."""
    # Spawned near the axis and kicked outward; heavy drag turns the kick into a fixed
    # ~0.6-unit radius, and upward "gravity" against that drag gives a steady 2 u/s climb.
    helix = Emitter(
        name="Armor Helix",
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.45,
        spawn_rate=60.0, max_particles=64,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.12),
        min_lifetime=0.75, max_lifetime=0.95,
        min_velocity=(0.0, 1.5, 0.0), max_velocity=(0.0, 2.5, 0.0),
        min_start_speed=1.4, max_start_speed=1.8,
        min_start_size=0.22, max_start_size=0.38,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-3.0, max_angular_velocity=3.0,
        gravity=(0.0, 5.0, 0.0), drag=2.5, orbital_speed=5.0,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.5), (0.25, 1.0), (1.0, 0.5)),
        color_over_lifetime=_fade(FROST_HOT, 0.9, peak=0.12, end_colour=FROST_ICE, hold=0.6))
    streaks = Emitter(
        name="Armor Streaks",
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.40,
        spawn_rate=70.0, max_particles=56,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.12),
        min_lifetime=0.55, max_lifetime=0.75,
        min_velocity=(0.0, 2.0, 0.0), max_velocity=(0.0, 3.0, 0.0),
        min_start_speed=1.2, max_start_speed=1.7,
        min_start_size=0.09, max_start_size=0.15,
        gravity=(0.0, 6.0, 0.0), drag=2.5, orbital_speed=5.0,
        render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.3)),
        color_over_lifetime=_fade(FROST_ICE, 0.55, peak=0.15, end_colour=FROST_DEEP, hold=0.6))
    mist = _mist("Armor Mist", 14, lifetime=1.1, size=1.0, alpha=0.26, spread=0.7,
                 color_over_lifetime=_fade(FROST_MIST, 0.26, peak=0.2, end_colour=FROST_ICE),
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.5),
                 min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.5, 0.0),
                 min_start_speed=0.6, max_start_speed=1.4, sink=0.0)
    return ParticleSystem(emitters=[
        _ring("Armor Ring", 0.6, 2.8, FROST_ICE, 0.5, 0.6),
        helix,
        streaks,
        mist,
        _flash("Armor Flash", 1.4, FROST_HOT, 0.4, 0.2, lift=0.9),
    ])


def chilled_burst():
    """A modest puff on an attacker slowed by Frost Armor; origin at its feet. A little
    cold mist around the legs, a handful of shards, a small ring -- ~0.6 s."""
    shards = _shards("Chill Shards", 12, speed=0.0, size=0.14, lifetime=0.42,
                     length=5.0, gravity=-4.0, drag=2.0,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.6, 0.35),
                     min_velocity=(-1.4, 1.0, -1.4), max_velocity=(1.4, 2.6, 1.4),
                     min_start_speed=0.0, max_start_speed=0.0)
    mist = _mist("Chill Mist", 12, lifetime=0.62, size=0.85, alpha=0.3, spread=0.5,
                 color_over_lifetime=_fade(FROST_MIST, 0.3, peak=0.18, end_colour=FROST_ICE),
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.9, 0.35),
                 min_velocity=(-0.4, -0.2, -0.4), max_velocity=(0.4, 0.3, 0.4))
    return ParticleSystem(emitters=[
        _ring("Chill Ring", 0.5, 1.8, FROST_ICE, 0.5, 0.42),
        _snowflakes("Chill Flakes", 4, lifetime=0.55, size=0.28, alpha=0.85, radius=0.2,
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.0, 0.3)),
        mist,
        shards,
    ])


# --- Loops -----------------------------------------------------------------------------------

def frost_hand_channel():
    """On ``hand_r`` during a 1.5-1.8 s cast. Local-space: an orbiting cyan swirl of motes
    and stars at radius ~0.3 around a small layered glow, so it follows the hand. World-
    space: a few frost motes falling off the hand. Steady state ~36 live."""
    swirl = looping(Emitter(
        name="Channel Swirl",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.32, 0.0, 0.0),
        min_velocity=(-0.05, -0.1, -0.05), max_velocity=(0.05, 0.25, 0.05),
        min_start_size=0.07, max_start_size=0.14,
        gravity=(0.0, 0.0, 0.0), orbital_speed=7.0, radial_acceleration=-0.4,
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.4)),
        color_over_lifetime=_fade(FROST_HOT, 0.6, peak=0.2, end_colour=FROST_ICE, hold=0.6)),
        rate=34.0, lifetime=0.5)
    stars = looping(Emitter(
        name="Channel Stars",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.28, 0.0, 0.0),
        min_velocity=(0.0, -0.05, 0.0), max_velocity=(0.0, 0.15, 0.0),
        min_start_size=0.12, max_start_size=0.2,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-3.0, max_angular_velocity=3.0,
        gravity=(0.0, 0.0, 0.0), orbital_speed=-5.0,
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.5)),
        color_over_lifetime=_fade(FROST_HOT, 0.85, peak=0.25, end_colour=FROST_ICE, hold=0.6)),
        rate=9.0, lifetime=0.55)
    core = looping(Emitter(
        name="Channel Core",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.03, 0.0, 0.0),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.3, max_start_size=0.4,
        min_start_rotation=0.0, max_start_rotation=3.14,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 1.1)),
        color_over_lifetime=_fade(FROST_HOT, 0.4, peak=0.3, end_colour=FROST_ICE)),
        rate=10.0, lifetime=0.45)
    halo = looping(Emitter(
        name="Channel Halo",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.04, 0.0, 0.0),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.65, max_start_size=0.8,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.85), (1.0, 1.1)),
        color_over_lifetime=_fade(FROST_ICE, 0.2, peak=0.4, end_colour=FROST_DEEP)),
        rate=6.0, lifetime=0.6)
    motes = looping(Emitter(
        name="Channel Motes",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.25, 0.0, 0.0),
        min_velocity=(-0.15, -0.3, -0.15), max_velocity=(0.15, 0.0, 0.15),
        min_start_size=0.06, max_start_size=0.11,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-2.0, max_angular_velocity=2.0,
        gravity=(0.0, -1.2, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
        color_over_lifetime=_fade(FROST_ICE, 0.75, peak=0.15, end_colour=FROST_DEEP)),
        rate=10.0, lifetime=0.8)
    return _warm(ParticleSystem(emitters=[halo, core, swirl, stars, motes]), 0.5)


def frostbolt_trail():
    """On the Frostbolt projectile (28 u/s). Local core: three stacked glows (white, cyan,
    azure) ~0.5 across plus a few orbiting stars. World trail: mist and splinters left
    behind, fading within ~0.32 s, so the visible trail is ~6-8 units. Steady state ~70
    live."""
    def core_layer(name, size, colour, end, alpha, rate, lifetime):
        return looping(Emitter(
            name=name,
            simulation_space=hpar.SIM_LOCAL,
            shape=hpar.SHAPE_SPHERE, shape_extents=(0.03, 0.0, 0.0),
            min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
            min_start_size=size * 0.85, max_start_size=size,
            min_start_rotation=0.0, max_start_rotation=3.14,
            gravity=(0.0, 0.0, 0.0),
            render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
            size_over_life=float_curve((0.0, 0.85), (0.5, 1.0), (1.0, 0.9)),
            color_over_lifetime=_fade(colour, alpha, peak=0.3, end_colour=end)),
            rate=rate, lifetime=lifetime)

    stars = looping(Emitter(
        name="Bolt Stars",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.26, 0.0, 0.0),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.16, max_start_size=0.24,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-6.0, max_angular_velocity=6.0,
        gravity=(0.0, 0.0, 0.0), orbital_speed=9.0,
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 0.6), (0.4, 1.0), (1.0, 0.6)),
        color_over_lifetime=_fade(FROST_HOT, 0.9, peak=0.25, end_colour=FROST_ICE)),
        rate=16.0, lifetime=0.32)
    mist = looping(Emitter(
        name="Bolt Mist",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
        min_velocity=(-0.3, -0.4, -0.3), max_velocity=(0.3, 0.1, 0.3),
        min_start_size=0.6, max_start_size=0.9,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.0, max_angular_velocity=1.0,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (1.0, 1.8)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_ICE, 0.42)),
            (0.40, rgba(FROST_MIST, 0.3)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=90.0, lifetime=0.32)
    shards = looping(Emitter(
        name="Bolt Shards",
        simulation_space=hpar.SIM_WORLD,
        inherit_velocity=0.18,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
        min_velocity=(-0.8, -0.8, -0.8), max_velocity=(0.8, 0.6, 0.8),
        min_start_size=0.08, max_start_size=0.13,
        gravity=(0.0, -2.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=6.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.9)),
            (0.45, rgba(FROST_ICE, 0.7)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=40.0, lifetime=0.3)
    return ParticleSystem(emitters=[
        mist,
        shards,
        core_layer("Bolt Halo", 1.4, FROST_DEEP, FROST_ICE, 0.32, 20.0, 0.25),
        core_layer("Bolt Body", 0.85, FROST_ICE, FROST_ICE, 0.5, 24.0, 0.2),
        core_layer("Bolt Core", 0.5, FROST_HOT, FROST_HOT, 0.85, 24.0, 0.18),
        stars,
    ])


def ice_lance_trail():
    """On the Ice Lance projectile (35 u/s). A thin elongated white shard core -- world-
    space beams with ``inherit_velocity = 1`` ride along with the projectile and stretch
    along the flight path -- over a small local white glow, plus a short glittering world
    trail. Steady state ~45 live."""
    shard = looping(Emitter(
        name="Lance Shard",
        simulation_space=hpar.SIM_WORLD,
        inherit_velocity=1.0,
        shape=hpar.SHAPE_POINT,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.13, max_start_size=0.16,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.9)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.0)),
            (0.25, rgba(FROST_HOT, 0.85)),
            (0.70, rgba(FROST_ICE, 0.7)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=50.0, lifetime=0.1)
    glow = looping(Emitter(
        name="Lance Glow",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.02, 0.0, 0.0),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.4, max_start_size=0.48,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.9), (1.0, 1.0)),
        color_over_lifetime=_fade(FROST_ICE, 0.35, peak=0.3)),
        rate=20.0, lifetime=0.2)
    glitter = looping(Emitter(
        name="Lance Glitter",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.1, 0.0, 0.0),
        min_velocity=(-0.5, -0.6, -0.5), max_velocity=(0.5, 0.3, 0.5),
        min_start_size=0.2, max_start_size=0.3,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-5.0, max_angular_velocity=5.0,
        gravity=(0.0, -0.8, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.95)),
            (0.50, rgba(FROST_ICE, 0.7)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=45.0, lifetime=0.26)
    streak = looping(Emitter(
        name="Lance Streak",
        simulation_space=hpar.SIM_WORLD,
        inherit_velocity=0.25,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.05, 0.0, 0.0),
        min_velocity=(-0.2, -0.2, -0.2), max_velocity=(0.2, 0.2, 0.2),
        min_start_size=0.09, max_start_size=0.13,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=9.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.6)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=35.0, lifetime=0.2)
    wake = looping(Emitter(
        name="Lance Wake",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.05, 0.0, 0.0),
        min_velocity=(-0.2, -0.2, -0.2), max_velocity=(0.2, 0.2, 0.2),
        min_start_size=0.3, max_start_size=0.42,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (1.0, 1.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.4)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=60.0, lifetime=0.18)
    return ParticleSystem(emitters=[wake, glow, streak, glitter, shard])


def frozen_root():
    """Aura idle on a rooted enemy for up to 8 s; origin at its feet. Ice crystals jut
    slowly up around the feet and glint, a faint frost disc breathes on the ground, and
    low mist creeps along the floor -- "frozen in place". Steady state ~30 live."""
    crystals = looping(Emitter(
        name="Root Crystals",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.55),
        min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 0.7, 0.0),
        min_start_size=0.13, max_start_size=0.2,
        gravity=(0.0, 0.0, 0.0), drag=0.8,
        render_mode=hpar.RENDER_STRETCHED, length_scale=4.5, material_name=BEAM,
        size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.8)),
        color_over_lifetime=_fade(FROST_HOT, 0.75, peak=0.25, end_colour=FROST_ICE, hold=0.65)),
        rate=10.0, lifetime=1.0)
    glints = looping(Emitter(
        name="Root Glints",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.45, 0.6),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.1, 0.0),
        min_start_size=0.14, max_start_size=0.24,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.5, max_angular_velocity=1.5,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.4, 1.0), (1.0, 0.3)),
        color_over_lifetime=_fade(FROST_HOT, 0.85, peak=0.4, end_colour=FROST_ICE)),
        rate=6.0, lifetime=0.6)
    disc = looping(Emitter(
        name="Root Disc",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_POINT,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=1.7, max_start_size=2.0,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-0.2, max_angular_velocity=0.2,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.85), (1.0, 1.1)),
        color_over_lifetime=_fade(FROST_ICE, 0.2, peak=0.5, end_colour=FROST_DEEP)),
        rate=2.5, lifetime=1.6)
    ring = looping(Emitter(
        name="Root Ring",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_POINT,
        min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.02, 0.0),
        min_start_size=1.3, max_start_size=1.4,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-0.3, max_angular_velocity=0.3,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
        size_over_life=float_curve((0.0, 0.95), (1.0, 1.1)),
        color_over_lifetime=_fade(FROST_ICE, 0.28, peak=0.5, end_colour=FROST_DEEP)),
        rate=1.5, lifetime=2.0)
    mist = looping(Emitter(
        name="Root Mist",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.6),
        min_velocity=(-0.15, 0.0, -0.15), max_velocity=(0.15, 0.12, 0.15),
        min_start_speed=0.1, max_start_speed=0.3,
        min_start_size=0.7, max_start_size=1.0,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-0.4, max_angular_velocity=0.4,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (1.0, 1.4)),
        color_over_lifetime=_fade(FROST_MIST, 0.24, peak=0.35, end_colour=FROST_ICE)),
        rate=6.0, lifetime=1.4)
    return _warm(ParticleSystem(emitters=[disc, ring, mist, crystals, glints]), 2.0)


def frostburn_veil():
    """Aura idle on an enemy taking frost damage over time (8 s); origin at its feet. A
    thin veil of cold motes drifting up through torso height (0.6-1.6) and an occasional
    star. Subtle -- it may be on four enemies at once. Steady state ~20 live."""
    motes = looping(Emitter(
        name="Veil Motes",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.2),
        min_velocity=(0.0, 2.6, 0.0), max_velocity=(0.0, 3.4, 0.0),
        min_start_speed=1.2, max_start_speed=2.0,
        min_start_size=0.18, max_start_size=0.28,
        gravity=(0.0, 2.2, 0.0), drag=4.0, orbital_speed=0.8,
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_ICE, 0.0)),
            (0.15, rgba(FROST_HOT, 0.0)),
            (0.40, rgba(FROST_HOT, 0.5)),
            (1.00, rgba(FROST_DEEP, 0.0)))),
        rate=11.0, lifetime=1.6)
    stars = looping(Emitter(
        name="Veil Stars",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.2),
        min_velocity=(0.0, 2.8, 0.0), max_velocity=(0.0, 3.6, 0.0),
        min_start_speed=1.2, max_start_speed=2.0,
        min_start_size=0.26, max_start_size=0.36,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-2.0, max_angular_velocity=2.0,
        gravity=(0.0, 2.2, 0.0), drag=4.0, orbital_speed=0.8,
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 0.5), (0.6, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.0)),
            (0.20, rgba(FROST_HOT, 0.0)),
            (0.50, rgba(FROST_HOT, 0.8)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=2.0, lifetime=1.8)
    haze = looping(Emitter(
        name="Veil Haze",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.2),
        min_velocity=(0.0, 4.0, 0.0), max_velocity=(0.0, 5.2, 0.0),
        min_start_speed=0.8, max_start_speed=1.4,
        min_start_size=0.7, max_start_size=0.95,
        min_start_rotation=0.0, max_start_rotation=3.14,
        gravity=(0.0, 1.0, 0.0), drag=4.0, orbital_speed=0.5,
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.7), (1.0, 1.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_MIST, 0.0)),
            (0.20, rgba(FROST_MIST, 0.0)),
            (0.50, rgba(FROST_MIST, 0.13)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=2.2, lifetime=1.8)
    return _warm(ParticleSystem(emitters=[haze, motes, stars]), 2.0)


def frost_armor_aura():
    """Aura idle on the mage for 30 minutes; origin at the feet. Calm on purpose: a few
    snowflake stars orbiting slowly at waist-to-chest height and the odd faint mist mote.
    Nothing flashes and nothing pulses. Steady state ~12 live."""
    flakes = looping(Emitter(
        name="Armor Flakes",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.15),
        min_velocity=(0.0, 3.6, 0.0), max_velocity=(0.0, 5.6, 0.0),
        min_start_speed=2.0, max_start_speed=2.8, drag=4.0,
        min_start_size=0.2, max_start_size=0.3,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-0.8, max_angular_velocity=0.8,
        gravity=(0.0, 0.0, 0.0), orbital_speed=0.9, radial_acceleration=0.0,
        render_mode=hpar.RENDER_BILLBOARD, material_name=STAR,
        size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 0.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_HOT, 0.0)),
            (0.18, rgba(FROST_HOT, 0.0)),
            (0.45, rgba(FROST_HOT, 0.6)),
            (0.70, rgba(FROST_ICE, 0.5)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=5.0, lifetime=2.2)
    motes = looping(Emitter(
        name="Armor Motes",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.2),
        min_velocity=(0.0, 2.4, 0.0), max_velocity=(0.0, 5.2, 0.0),
        min_start_speed=1.8, max_start_speed=3.0, drag=4.0,
        min_start_size=0.5, max_start_size=0.7,
        gravity=(0.0, 0.0, 0.0), orbital_speed=0.6,
        render_mode=hpar.RENDER_BILLBOARD, material_name=GLOW,
        size_over_life=float_curve((0.0, 0.7), (1.0, 1.2)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FROST_MIST, 0.0)),
            (0.20, rgba(FROST_MIST, 0.0)),
            (0.55, rgba(FROST_MIST, 0.14)),
            (1.00, rgba(FROST_ICE, 0.0)))),
        rate=1.2, lifetime=2.4)
    return _warm(ParticleSystem(emitters=[motes, flakes]), 2.5)


EFFECTS = [
    (frostbolt_impact, "FrostboltImpact.hpar"),
    (ice_lance_impact, "IceLanceImpact.hpar"),
    (frost_nova, "FrostNova.hpar"),
    (frost_armor_burst, "FrostArmorBurst.hpar"),
    (chilled_burst, "ChilledBurst.hpar"),
    (frost_hand_channel, "FrostHandChannel.hpar"),
    (frostbolt_trail, "FrostboltTrail.hpar"),
    (ice_lance_trail, "IceLanceTrail.hpar"),
    (frozen_root, "FrozenRoot.hpar"),
    (frostburn_veil, "FrostburnVeil.hpar"),
    (frost_armor_aura, "FrostArmorAura.hpar"),
]


def main():
    for build, filename in EFFECTS:
        write(build(), filename)


if __name__ == "__main__":
    main()
