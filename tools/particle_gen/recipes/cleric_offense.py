# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Cleric offensive and cast-hand effects: the hand glows that play while casting, the release
pop, Smite and Holy Fire. Run from the repo root::

    py -3 tools/particle_gen/recipes/cleric_offense.py

Writes into ``data/client/Particles/Cleric/``.

Art direction
-------------
Radiant stylized holy light, sharing the gold/ivory core of every cleric effect
(``cleric_common``). Damage reads **sharper, brighter and faster** than healing: Smite is a
near-white column that slams down in a third of a second, Holy Fire is amber-white flame
whose darkest key never drops below ``AMBER_FIRE`` (anything redder reads as ordinary fire).

Attach points (where the visualization kit spawns each effect):

* hand_r      -- HolyHandGlow, SmiteGather, HolyFireGather (looping cast phase), HolyRelease
* root (feet) -- SmiteImpact, HolyFireImpact, HolyFireBurn (looping aura idle)
* spine_03    -- SmiteFlash, HolyFireTick

Tuning lessons that are not obvious from the shared helpers
-----------------------------------------------------------
* **A bone-attached emitter inherits the bone's orientation.** ``AttachObjectToBone`` hangs the
  emitter off a TagPoint, so in ``SIM_LOCAL`` the emitter's +Y is the *hand bone's* axis, not
  world up -- and in ``SIM_WORLD`` the initial velocity is still rotated by that bone
  (``systemWorld.TransformDirectionAffine``). Only **gravity** is applied in unrotated world
  space. So anything on hand_r that must rise (flame tongues, embers) is spawned with zero
  velocity in ``SIM_WORLD`` and lifted by a *positive* gravity; anything that converges or
  orbits is built from radial terms (negative start speed, orbital swirl) that look the same
  whatever way the bone points.
* **Converging sparks = negative start speed.** ``start_speed`` is applied along the sphere's
  outward spawn direction, so a negative range throws every particle at the centre. Lifetime
  ~ radius / speed makes them die as they arrive instead of overshooting out the far side.
* **Root-attached flashes sit half underground.** Emitters have no offset, and a billboard at
  y=0 is half clipped by the terrain in game. ``_lift`` gives such a single sprite an upward
  velocity that drag bleeds off (travel = v / drag), parking it at chest/waist height.
* **Flame tongues are RENDER_STRETCHED at length_scale 2, not billboards.** The FLAME texture
  is 1:2 and a billboard quad is square, so a billboarded tongue is squashed into a fat cone.
  A stretched quad is ``size`` wide by ``size * length_scale`` long and maps the texture's top
  (the tip) to the +velocity end (``particle_emitter.cpp``: the ``+upOffset`` vertices get
  ``vMin``), so tongues keep their shape and point the way they travel.
  ``preview.py`` draws stretched sprites rotated 180 degrees relative to the engine -- the
  soft symmetric beam sprite never showed it, a flame does.
* Holy-fire colour ramps (``flame_colour``) hold FIRE_CORE for most of the life and reach
  AMBER_FIRE only in the last third, while alpha is already falling: amber at low alpha
  composites toward brown on dark ground and reads as campfire, not holy fire.
* A contracting RING billboard is what sells "gathering" in SmiteGather; inward streaks alone
  are ambiguous in any still frame.
* Thin falling streaks alone read as rain; Smite's column needs a few wide, slow, low-alpha
  beams (the sheath) to read as one solid shaft of light.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cleric_common as cc  # noqa: E402
from cleric_common import (  # noqa: E402
    BEAM, GLOW, RING, STAR, RAYS, FLAME,
    HOLY_WHITE, HOLY_GOLD, SOFT_GOLD, AMBER_FIRE, FIRE_CORE,
    Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar, rgba)


# --------------------------------------------------------------------------------------------
# Local helpers
# --------------------------------------------------------------------------------------------

def _budget(rate, max_lifetime, bursts=0, margin=1.25):
    """max_particles that can never truncate a continuous emitter plus its bursts."""
    return int(rate * max_lifetime * margin) + bursts + 4


def _fade(colour_a, colour_b, alpha, peak_at=0.2, hold_at=0.6, hold=0.8):
    """The standard two-colour, alpha-in / alpha-out ramp used by every layer here."""
    return color_curve(
        (0.00, rgba(colour_a, 0.0)),
        (peak_at, rgba(colour_a, alpha)),
        (hold_at, rgba(colour_b, alpha * hold)),
        (1.00, rgba(colour_b, 0.0)))


def _lift(emitter, height, drag=6.0):
    """Float a single root-attached sprite up to ~``height`` (see module docstring)."""
    v = height * drag
    emitter.min_velocity = (0.0, v, 0.0)
    emitter.max_velocity = (0.0, v, 0.0)
    emitter.drag = drag
    return emitter


def _fast_ring(ring, growth_frac=0.35, growth_hold=0.8, hold_alpha_frac=0.65):
    """Make a ground_ring expand mostly while it is still bright (warrior_abilities lesson:
    size and alpha curves otherwise fade independently and a large ring dies before it reads
    as having travelled)."""
    peak = ring.color_over_lifetime[1].color
    colour, alpha = peak[:3], peak[3]
    end_ratio = ring.size_over_life[-1].value
    ring.size_over_life = float_curve(
        (0.0, 1.0),
        (growth_frac, 1.0 + (end_ratio - 1.0) * growth_hold),
        (1.0, end_ratio))
    ring.color_over_lifetime = color_curve(
        (0.00, rgba(colour, 0.0)),
        (0.10, rgba(colour, alpha)),
        (growth_frac, rgba(colour, alpha * hold_alpha_frac)),
        (1.00, rgba(colour, 0.0)))
    return ring


def glow_core(name, size, colour, hot=HOLY_WHITE, alpha=0.4, rate=14.0, lifetime=0.6,
              loop=True, duration=1.0, bursts=None):
    """Overlapping soft blobs at the origin -- a steady (or pulsing, with a low rate) glow."""
    bursts = bursts or []
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=loop, duration=duration,
        warmup_time=0.5 if loop else 0.0,
        spawn_rate=rate, max_particles=_budget(rate, lifetime, sum(b.count for b in bursts)),
        bursts=bursts,
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime * 0.7, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=size * 0.75, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.7), (0.4, 1.0), (1.0, 0.85)),
        color_over_lifetime=_fade(hot, colour, alpha, peak_at=0.3, hold_at=0.65),
    )


def flame_colour(alpha, hot=FIRE_CORE, colour=AMBER_FIRE):
    """Holy-fire ramp: hot -> FIRE_CORE for most of the life, AMBER_FIRE only as it dies.

    Amber at low alpha composites toward brown on dark ground, so it is kept to the last
    third of the life where alpha is already falling."""
    return color_curve(
        (0.00, rgba(hot, 0.0)),
        (0.15, rgba(hot, alpha)),
        (0.50, rgba(FIRE_CORE, alpha * 0.9)),
        (0.80, rgba(colour, alpha * 0.5)),
        (1.00, rgba(colour, 0.0)))


def flame_tongues(name, width, rate, lifetime, rise, radius, alpha=0.5, hot=FIRE_CORE,
                  colour=AMBER_FIRE, loop=True, duration=1.0, bursts=None, space=hpar.SIM_WORLD,
                  shape=hpar.SHAPE_SPHERE, extents=None, velocity=None, drag=0.0, noise=0.0):
    """FLAME tongues rising from the origin, ``width`` wide and twice as tall.

    Rendered STRETCHED at length_scale 2: the FLAME texture is 1:2, so a square billboard
    squashes it into a fat cone, while a stretched quad keeps its aspect and puts the
    texture's top (the tip) along the velocity -- tongues point the way they travel.

    Default is the hand-safe form: ``SIM_WORLD``, zero initial velocity and a positive
    *gravity* of ``rise`` so the tongues go up in world space whatever the bone's
    orientation. Root-attached callers pass ``space=SIM_LOCAL`` and an explicit ``velocity``.
    """
    bursts = bursts or []
    vmin, vmax = velocity if velocity else ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    return Emitter(
        name=name,
        simulation_space=space,
        loop=loop, duration=duration,
        warmup_time=0.6 if loop else 0.0,
        spawn_rate=rate, max_particles=_budget(rate, lifetime, sum(b.count for b in bursts)),
        bursts=bursts,
        shape=shape, shape_extents=extents if extents else (radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.65, max_lifetime=lifetime,
        min_velocity=vmin, max_velocity=vmax,
        min_start_size=width * 0.6, max_start_size=width,
        gravity=(0.0, rise, 0.0),
        drag=drag, noise_amplitude=noise, noise_frequency=1.5,
        render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
        material_name=FLAME,
        # Grows out of the source, then the tongue narrows away as it rises.
        size_over_life=float_curve((0.0, 0.45), (0.3, 1.0), (1.0, 0.35)),
        color_over_lifetime=flame_colour(alpha, hot, colour),
    )


def embers(name, rate, lifetime, size=0.05, radius=0.1, rise=1.2, alpha=0.9, loop=True,
           duration=1.0, bursts=None, kick=0.4, noise=3.0, space=hpar.SIM_WORLD, velocity=None,
           drag=0.6):
    """Tiny bright sparks that drift up on noise. Gravity (world up) carries them, the radial
    kick is orientation-independent, so the hand-safe default works on any bone."""
    bursts = bursts or []
    vmin, vmax = velocity if velocity else ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
    return Emitter(
        name=name,
        simulation_space=space,
        loop=loop, duration=duration,
        warmup_time=0.6 if loop else 0.0,
        spawn_rate=rate, max_particles=_budget(rate, lifetime, sum(b.count for b in bursts)),
        bursts=bursts,
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=vmin, max_velocity=vmax,
        min_start_speed=kick * 0.4, max_start_speed=kick,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, rise, 0.0), drag=drag,
        noise_amplitude=noise, noise_frequency=1.8,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_WHITE, 0.0)),
            (0.10, rgba(HOLY_WHITE, alpha)),
            (0.50, rgba(FIRE_CORE, alpha * 0.85)),
            (1.00, rgba(AMBER_FIRE, 0.0))),
    )


def star_burst(name, count, speed, size, lifetime, colour=HOLY_GOLD, drag=4.0, radius=0.1,
               delay=0.0, alpha=1.0):
    """Star sprites flung outward and braked hard by drag -- divine sparks that hang a beat."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 4,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_speed=speed * 0.45, max_start_speed=speed,
        min_start_size=size * 0.55, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-5.0, max_angular_velocity=5.0,
        gravity=(0.0, 0.0, 0.0), drag=drag,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.5), (0.2, 1.0), (1.0, 0.2)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_WHITE, alpha)),
            (0.45, rgba(colour, alpha * 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def streak_burst(name, count, speed, size, lifetime, colour=HOLY_GOLD, drag=3.0, delay=0.0,
                 length=5.0, alpha=1.0, gravity=0.0, radius=0.1, upward=0.0):
    """Omnidirectional stretched sparks (``spark_burst`` is biased upward; this is not)."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 4,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, upward, 0.0),
        min_start_speed=speed * 0.5, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, gravity, 0.0), drag=drag,
        render_mode=hpar.RENDER_STRETCHED, length_scale=length,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.25)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_WHITE, alpha)),
            (0.40, rgba(colour, alpha * 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def ground_glow(name, size, colour, alpha=0.4, lifetime=0.5, delay=0.0, grow=1.4):
    """A flat soft glow disc on the ground -- the hot spot where the effect touches down."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=3,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
        min_start_size=size, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.25, 1.0), (1.0, grow)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.12, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


# --------------------------------------------------------------------------------------------
# Cast-phase hand effects (hand_r, looping)
# --------------------------------------------------------------------------------------------

def holy_hand_glow():
    """Healing-style cast: a calm radiant glow cupped in the hand."""
    rays = Emitter(
        name="Hand Rays",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=1.5,
        spawn_rate=1.6, max_particles=6,
        shape=hpar.SHAPE_POINT,
        min_lifetime=1.2, max_lifetime=1.5,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.48, max_start_size=0.58,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=0.5, max_angular_velocity=0.8,     # slow turn
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=RAYS,
        size_over_life=float_curve((0.0, 0.85), (0.5, 1.0), (1.0, 0.9)),
        color_over_lifetime=_fade(HOLY_WHITE, HOLY_GOLD, 0.32, peak_at=0.35, hold_at=0.65,
                                  hold=0.85),
    )
    motes = Emitter(
        name="Orbit Motes",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=1.0,
        spawn_rate=9.0, max_particles=_budget(9.0, 1.1),
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.2, 0.0, 0.0),
        min_lifetime=0.7, max_lifetime=1.1,
        min_velocity=(-0.04, -0.04, -0.04), max_velocity=(0.04, 0.04, 0.04),
        min_start_size=0.06, max_start_size=0.11,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-2.5, max_angular_velocity=2.5,    # twinkle
        gravity=(0.0, 0.0, 0.0), orbital_speed=3.2,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.3)),
        color_over_lifetime=_fade(HOLY_WHITE, SOFT_GOLD, 0.9),
    )
    return ParticleSystem(emitters=[
        glow_core("Glow Halo", size=0.55, colour=SOFT_GOLD, alpha=0.2, rate=5.0, lifetime=1.0),
        rays,
        glow_core("Glow Core", size=0.28, colour=SOFT_GOLD, alpha=0.42, rate=14.0, lifetime=0.6),
        motes,
    ])


def smite_gather():
    """Judgement gathering: white-gold streaks rushing INTO the hand around a hot core."""
    # Negative start speed throws every particle at the centre; lifetime ~ radius / speed so
    # they die on arrival rather than overshooting out the other side.
    inrush = Emitter(
        name="Inrush Streaks",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=0.5,
        spawn_rate=65.0, max_particles=_budget(65.0, 0.22),
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.55, 0.0, 0.0),
        min_lifetime=0.15, max_lifetime=0.22,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_speed=-2.6, max_start_speed=-1.8,
        min_start_size=0.03, max_start_size=0.055,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=4.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_GOLD, 0.0)),
            (0.30, rgba(HOLY_GOLD, 0.85)),
            (0.80, rgba(HOLY_WHITE, 0.9)),
            (1.00, rgba(HOLY_WHITE, 0.0))),
    )
    stars = Emitter(
        name="Core Stars",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=0.5,
        spawn_rate=7.0, max_particles=_budget(7.0, 0.4),
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.04, 0.0, 0.0),
        min_lifetime=0.25, max_lifetime=0.4,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.16, max_start_size=0.26,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-4.0, max_angular_velocity=4.0,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.5)),
        color_over_lifetime=_fade(HOLY_WHITE, HOLY_GOLD, 0.9, peak_at=0.25),
    )
    flicker = Emitter(
        name="Ray Flicker",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=0.5,
        spawn_rate=3.5, max_particles=6,
        shape=hpar.SHAPE_POINT,
        min_lifetime=0.3, max_lifetime=0.4,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.36, max_start_size=0.46,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=2.5, max_angular_velocity=3.5,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=RAYS,
        size_over_life=float_curve((0.0, 0.7), (0.4, 1.0), (1.0, 0.9)),
        color_over_lifetime=_fade(HOLY_WHITE, HOLY_GOLD, 0.4, peak_at=0.3),
    )
    # A ring that contracts onto the hand: the clearest "gathering" read, because the eye
    # follows the shrinking outline even in a still frame where streak direction is ambiguous.
    implode = Emitter(
        name="Implode Ring",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=0.5,
        spawn_rate=2.8, max_particles=6,
        shape=hpar.SHAPE_POINT,
        min_lifetime=0.4, max_lifetime=0.4,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.7, max_start_size=0.8,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=RING,
        size_over_life=float_curve((0.0, 1.0), (0.6, 0.45), (1.0, 0.12)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_GOLD, 0.0)),
            (0.30, rgba(HOLY_GOLD, 0.4)),
            (0.85, rgba(HOLY_WHITE, 0.6)),
            (1.00, rgba(HOLY_WHITE, 0.0))),
    )
    return ParticleSystem(emitters=[
        implode,
        glow_core("Gather Halo", size=0.45, colour=HOLY_GOLD, alpha=0.22, rate=8.0, lifetime=0.5),
        flicker,
        inrush,
        glow_core("Hot Core", size=0.2, colour=HOLY_WHITE, hot=HOLY_WHITE, alpha=0.65,
                  rate=22.0, lifetime=0.3),
        stars,
    ])


def holy_fire_gather():
    """Holy fire in the palm: small amber-white tongues licking upward plus rising embers."""
    return ParticleSystem(emitters=[
        glow_core("Fire Glow", size=0.34, colour=AMBER_FIRE, hot=FIRE_CORE, alpha=0.32,
                  rate=10.0, lifetime=0.6),
        flame_tongues("Palm Flames", width=0.13, rate=26.0, lifetime=0.5, rise=2.2, radius=0.08,
                      alpha=0.6),
        flame_tongues("Palm Flame Core", width=0.08, rate=16.0, lifetime=0.35, rise=1.8,
                      radius=0.04, alpha=0.7, hot=HOLY_WHITE, colour=FIRE_CORE),
        embers("Palm Embers", rate=12.0, lifetime=0.9, size=0.06, radius=0.1, rise=1.0),
    ])


# --------------------------------------------------------------------------------------------
# One-shots
# --------------------------------------------------------------------------------------------

def holy_release():
    """Cast-complete pop at the hand: sunburst, soft flash, a puff of sparks, a few motes."""
    motes = cc.rising_motes("Release Motes", SOFT_GOLD, count=8, radius=0.12, rise=0.6,
                            size=0.09, lifetime=0.45, alpha=0.8, orbital=2.0, duration=0.1)
    return ParticleSystem(emitters=[
        cc.soft_flash("Release Flash", size=0.6, colour=HOLY_WHITE, alpha=0.6, lifetime=0.16),
        cc.ray_flash("Release Rays", size=0.9, colour=HOLY_WHITE, alpha=0.75, lifetime=0.28,
                     spin=2.0),
        streak_burst("Release Sparks", count=18, speed=3.2, size=0.05, lifetime=0.32,
                     drag=3.5, length=4.0),
        motes,
    ])


def smite_impact():
    """A hard, fast column of judgement slamming onto the target's feet."""
    # Height 5.5 at speed 18: the top streaks reach the ground at ~0.3 s; everything after
    # that is the impact (sigil, ring, ground glow, flash, sparks) delayed to land on that beat.
    body = cc.light_shaft("Smite Shaft", HOLY_GOLD, height=5.5, radius=0.32, count=60,
                          speed=18.0, size=0.2, lifetime=0.32, alpha=0.45)
    core = cc.light_shaft("Smite Core", HOLY_WHITE, height=5.5, radius=0.1, count=30,
                          speed=22.0, size=0.13, lifetime=0.27, alpha=0.7)
    core.length_scale = 9.0
    # Thin streaks alone read as rain. A few wide, slow, low-alpha beams give the column a
    # body that hangs for a beat after the slam and then fades from the top down.
    sheath = cc.light_shaft("Smite Sheath", HOLY_GOLD, height=5.0, radius=0.22, count=22,
                            speed=4.0, size=0.55, lifetime=0.5, alpha=0.22, delay=0.04)
    sheath.length_scale = 5.0
    ring = _fast_ring(cc.ground_ring("Smite Ring", start_size=0.5, end_size=2.8,
                                     colour=HOLY_GOLD, alpha=0.6, lifetime=0.45, delay=0.2))
    flash = _lift(cc.ray_flash("Smite Ground Flash", size=1.3, colour=HOLY_WHITE, alpha=0.7,
                               lifetime=0.28, spin=2.0, delay=0.2), height=0.45)
    sparks = streak_burst("Smite Splash", count=24, speed=5.0, size=0.07, lifetime=0.4,
                          drag=2.5, gravity=-6.0, upward=2.0, delay=0.2, length=4.5)
    return ParticleSystem(emitters=[
        ground_glow("Smite Ground Glow", 1.4, HOLY_WHITE, alpha=0.5, lifetime=0.45, delay=0.18),
        cc.ground_sigil("Smite Sigil", 1.7, HOLY_GOLD, alpha=0.7, lifetime=0.6, spin=1.2,
                        grow=1.2, delay=0.15),
        ring,
        sheath,
        body,
        core,
        flash,
        sparks,
    ])


def smite_flash():
    """The judgement hits the chest: a big spinning sunburst and white-gold stars bursting."""
    counter = cc.ray_flash("Flash Rays Inner", size=1.0, colour=HOLY_GOLD, alpha=0.6,
                           lifetime=0.28, spin=-3.0)
    return ParticleSystem(emitters=[
        cc.soft_flash("Flash Core", size=1.0, colour=HOLY_WHITE, alpha=0.55, lifetime=0.18),
        cc.ray_flash("Flash Rays", size=1.8, colour=HOLY_WHITE, alpha=0.75, lifetime=0.34,
                     spin=2.0),
        counter,
        streak_burst("Flash Streaks", count=22, speed=6.5, size=0.07, lifetime=0.32, drag=3.0),
        star_burst("Flash Stars", count=14, speed=4.0, size=0.2, lifetime=0.38, drag=5.0),
    ])


def holy_fire_impact():
    """A pillar of holy flame erupting from the ground around the target."""
    # Cone with a tiny height and a wide base = a disc footprint around the feet; tongues
    # rise from there. With drag d, a tongue launched at v travels v*(1-e^-dt)/d -- about
    # 2.5 units for the fastest ones, i.e. well over the head.
    pillar = flame_tongues(
        "Flame Pillar", width=0.36, rate=100.0, lifetime=0.55, rise=0.0, radius=0.0, alpha=0.55,
        loop=False, duration=0.42, bursts=[Burst(0.0, 26)], space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, extents=(0.0, 0.1, 0.5),
        velocity=((-0.2, 3.0, -0.2), (0.2, 6.0, 0.2)), drag=1.0)
    inner = flame_tongues(
        "Flame Core", width=0.22, rate=45.0, lifetime=0.45, rise=0.0, radius=0.2, alpha=0.65,
        hot=HOLY_WHITE, colour=FIRE_CORE, loop=False, duration=0.38, bursts=[Burst(0.0, 10)],
        space=hpar.SIM_LOCAL, velocity=((-0.1, 4.0, -0.1), (0.1, 7.0, 0.1)), drag=1.0)
    # Fast amber streaks sketch the pillar's outline above the flame tongues.
    streaks = streak_burst("Pillar Streaks", count=16, speed=0.0, size=0.08, lifetime=0.38,
                           colour=AMBER_FIRE, drag=0.8, length=7.0, alpha=0.8)
    streaks.simulation_space = hpar.SIM_LOCAL
    streaks.shape, streaks.shape_extents = hpar.SHAPE_CONE, (0.0, 0.1, 0.45)
    streaks.min_velocity, streaks.max_velocity = (0.0, 5.0, 0.0), (0.0, 8.5, 0.0)
    streaks.spawn_rate, streaks.duration = 32.0, 0.35
    streaks.max_particles = _budget(32.0, 0.38, 16)
    sparks = embers(
        "Fire Embers", rate=30.0, lifetime=0.85, size=0.07, radius=0.45, rise=-0.8, alpha=0.95,
        loop=False, duration=0.4, bursts=[Burst(0.0, 22)], kick=1.2, noise=3.0,
        space=hpar.SIM_LOCAL, velocity=((-0.8, 2.5, -0.8), (0.8, 5.5, 0.8)), drag=0.8)
    flash = _lift(cc.ray_flash("Fire Flash", size=1.4, colour=FIRE_CORE, alpha=0.65,
                               lifetime=0.3, spin=1.8), height=0.9)
    ring = _fast_ring(cc.ground_ring("Fire Ring", start_size=0.6, end_size=2.8,
                                     colour=AMBER_FIRE, alpha=0.55, lifetime=0.6))
    return ParticleSystem(emitters=[
        ground_glow("Fire Ground Glow", 1.8, FIRE_CORE, alpha=0.45, lifetime=0.7),
        ring,
        pillar,
        inner,
        streaks,
        sparks,
        flash,
    ])


def holy_fire_burn():
    """6 s DoT idle: a few low-alpha flame tongues licking up around the body."""
    # Cone of height 0.6 / radius 0.36: tongues start anywhere from the shins to the waist
    # around the body and rise to ~1.6. Rate and alpha stay low; this lives for 6 seconds.
    licks = flame_tongues(
        "Burn Licks", width=0.18, rate=11.0, lifetime=0.8, rise=0.0, radius=0.0, alpha=0.62,
        space=hpar.SIM_LOCAL, shape=hpar.SHAPE_CONE, extents=(0.0, 0.6, 0.36),
        velocity=((-0.05, 0.9, -0.05), (0.05, 1.5, 0.05)))
    sparks = embers("Burn Embers", rate=4.0, lifetime=1.0, size=0.05, radius=0.35, rise=0.6,
                    alpha=0.85, kick=0.15, space=hpar.SIM_LOCAL,
                    velocity=((-0.1, 0.6, -0.1), (0.1, 1.2, 0.1)))
    sparks.shape = hpar.SHAPE_CONE
    sparks.shape_extents = (0.0, 1.0, 0.35)
    return ParticleSystem(emitters=[licks, sparks])


def holy_fire_tick():
    """A DoT tick at the chest: a quick flare of tongues and embers outward."""
    # Stretched flames point along their velocity, so the outward kick fans the tongues
    # into a little starburst before world-up gravity bends them upward.
    flare = flame_tongues(
        "Tick Flames", width=0.16, rate=0.0, lifetime=0.4, rise=2.5, radius=0.15, alpha=0.65,
        loop=False, duration=0.05, bursts=[Burst(0.0, 12)], space=hpar.SIM_WORLD, drag=3.0)
    flare.min_start_speed, flare.max_start_speed = 0.8, 1.6
    return ParticleSystem(emitters=[
        cc.soft_flash("Tick Flash", size=0.6, colour=FIRE_CORE, alpha=0.45, lifetime=0.18),
        flare,
        embers("Tick Embers", rate=0.0, lifetime=0.45, size=0.06, radius=0.1, rise=1.0,
               loop=False, duration=0.05, bursts=[Burst(0.0, 12)], kick=2.5, noise=2.0,
               drag=3.0),
    ])


EFFECTS = [
    (holy_hand_glow, "HolyHandGlow.hpar"),
    (smite_gather, "SmiteGather.hpar"),
    (holy_fire_gather, "HolyFireGather.hpar"),
    (holy_release, "HolyRelease.hpar"),
    (smite_impact, "SmiteImpact.hpar"),
    (smite_flash, "SmiteFlash.hpar"),
    (holy_fire_impact, "HolyFireImpact.hpar"),
    (holy_fire_burn, "HolyFireBurn.hpar"),
    (holy_fire_tick, "HolyFireTick.hpar"),
]


if __name__ == "__main__":
    for build, filename in EFFECTS:
        system = build()
        if filename not in cc.LOOPING_EFFECTS:
            looping = [e.name for e in system.emitters if e.loop]
            assert not looping, "%s is one-shot but loops: %s" % (filename, looping)
        cc.write(system, filename)
