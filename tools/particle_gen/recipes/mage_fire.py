# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Fire school mage effects. Run from the repo root::

    py -3.14 tools/particle_gen/recipes/mage_fire.py

Writes into ``data/client/Particles/Mage/``.

Art direction: stylized high-fantasy arcane fire with WoW-like readability -- roiling flame,
hot yellow-white cores, bright embers, and a little dark smoke for contrast.

How that survives alpha-only blending (see ``warrior_common``'s docstring for the why):

* **Flame = many soft glow billboards at peak alpha ~0.3-0.45** whose colour runs
  ``FIRE_HOT -> FIRE_ORANGE -> FIRE_RED`` while alpha falls. The darkest key stays a bright,
  saturated red and the exit is the alpha curve, never a brown/black colour key -- high-alpha
  dark-orange blobs read as cardboard.
* **Roiling** comes from motion, not texture: random roll + angular velocity, curl noise,
  upward buoyancy (positive gravity) and size growth over life.
* **Embers** are the one layer allowed to run bright: few, small, stretched streaks at
  alpha ~1.0 that fade to orange.
* **Smoke** is sparse and low alpha (<= 0.22), late, rising -- just enough contrast to make
  the flame read hot.

Placement contract (decided by the spell visualization kits, not by this file):

==========================  ============  ==========  =======================================
File                        Attach        Lifetime    Notes
==========================  ============  ==========  =======================================
FireballImpact.hpar         spine_03      one-shot    origin = target torso
FireBlastImpact.hpar        target feet   one-shot    eruption column through the target
FireBlastHand.hpar          hand_r        one-shot    cast flare
FireBarrageImpact.hpar      spine_03      one-shot    ~55% of FireballImpact, x9 in 1.5 s
FireHandChannel.hpar        hand_r        looping     torn down at cast end
FireballTrail.hpar          projectile    looping     node flies at 24 u/s
FireBarrageTrail.hpar       projectile    looping     3 at once, 25 u/s
FireballBurn.hpar           target feet   looping     DoT aura idle
==========================  ============  ==========  =======================================

Looping trails: the projectile node moves, so every ``SIM_WORLD`` emitter is left behind as
the wake; trail length ~= speed * max_lifetime. ``SIM_LOCAL`` emitters ride with the node
and build the orb itself.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mage_common as mc
from mage_common import (BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem,
                         color_curve, float_curve, hpar, rgba,
                         FIRE_HOT, FIRE_ORANGE, FIRE_RED, FIRE_SMOKE, looping, write)

# A white-hot key a touch hotter than FIRE_HOT, for single flashes and the very core.
WHITE_HOT = (1.00, 0.98, 0.85)
# Ember colour: hotter and more yellow than the flame body so sparks pop against it.
EMBER = (1.00, 0.78, 0.30)


def fire_ramp(alpha, hot=FIRE_HOT, mid=FIRE_ORANGE, cool=FIRE_RED, fade_in=0.08):
    """Hot core -> orange -> red, alpha carrying the exit."""
    return color_curve(
        (0.00, rgba(hot, 0.0)),
        (fade_in, rgba(hot, alpha)),
        (0.45, rgba(mid, alpha * 0.92)),
        (0.78, rgba(cool, alpha * 0.60)),
        (1.00, rgba(cool, 0.0)))


def flame_burst(name, count, radius, speed, size, lifetime, alpha=0.40, drag=3.5,
                buoyancy=1.5, noise=2.5, space=hpar.SIM_WORLD, delay=0.0,
                hot=FIRE_HOT, mid=FIRE_ORANGE, cool=FIRE_RED, growth=1.6):
    """Roiling flame puffs thrown radially from a sphere and slowed by drag, rising as they
    cool. The body of every explosion here."""
    return Emitter(
        name=name,
        simulation_space=space,
        loop=False, duration=0.08, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 6,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-0.3, 0.0, -0.3), max_velocity=(0.3, 0.4, 0.3),
        min_start_speed=speed * 0.35, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-2.5, max_angular_velocity=2.5,
        gravity=(0.0, buoyancy, 0.0), drag=drag,
        noise_amplitude=noise, noise_frequency=1.2,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.55), (0.3, 1.0), (1.0, growth)),
        color_over_lifetime=fire_ramp(alpha, hot, mid, cool),
    )


def ember_spray(name, count, speed, size=0.07, lifetime=0.9, gravity=-6.0, drag=1.4,
                radius=0.15, delay=0.0, length=4.0):
    """Bright stretched sparks; the only layer that runs at full alpha."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.08, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 6,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.5, max_lifetime=lifetime,
        min_velocity=(-0.5, 0.3, -0.5), max_velocity=(0.5, 1.8, 0.5),
        min_start_speed=speed * 0.4, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, gravity, 0.0), drag=drag,
        render_mode=hpar.RENDER_STRETCHED, length_scale=length,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.35)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 1.0)),
            (0.35, rgba(EMBER, 1.0)),
            (0.75, rgba(FIRE_ORANGE, 0.8)),
            (1.00, rgba(FIRE_RED, 0.0))),
    )


def smoke(name, count, radius, size, lifetime, alpha=0.20, rise=1.0, delay=0.15,
          space=hpar.SIM_WORLD):
    """Sparse, late, low-alpha rising smoke for contrast. Never dense: it reads as dirt."""
    return Emitter(
        name=name,
        simulation_space=space,
        loop=False, duration=0.10, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 4,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
        min_lifetime=lifetime * 0.65, max_lifetime=lifetime,
        min_velocity=(-0.35, rise * 0.5, -0.35), max_velocity=(0.35, rise, 0.35),
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-0.8, max_angular_velocity=0.8,
        gravity=(0.0, 0.4, 0.0), drag=0.8,
        noise_amplitude=1.2, noise_frequency=0.8,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.7)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_SMOKE, 0.0)),
            (0.30, rgba(FIRE_SMOKE, alpha)),
            (1.00, rgba(FIRE_SMOKE, 0.0))),
    )


def flash(name, size, alpha=0.7, lifetime=0.14, colour=WHITE_HOT, rise=0.0):
    """Single onset blob. ``rise`` lifts it (velocity with heavy drag) when the system
    origin is at the feet but the flash belongs higher up."""
    e = mc.soft_flash(name, size=size, colour=colour, alpha=alpha, lifetime=lifetime)
    if rise > 0.0:
        e.min_velocity = (0.0, rise * 9.0, 0.0)
        e.max_velocity = (0.0, rise * 9.0, 0.0)
        e.drag = 9.0
    return e


def billboard_ring(name, start, end, colour, alpha, lifetime, delay=0.0):
    """Camera-facing expanding ring -- the shockwave of an airborne explosion."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=start, max_start_size=start,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=RING,
        size_over_life=float_curve((0.0, 1.0), (0.35, 1.0 + (end / start - 1.0) * 0.75),
                                   (1.0, end / start)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.10, rgba(colour, alpha)),
            (0.40, rgba(colour, alpha * 0.6)),
            (1.00, rgba(colour, 0.0))),
    )


def flat_ring(name, start, end, colour, alpha, lifetime, delay=0.0, material=RING):
    """Ground ring with the expansion front-loaded so it travels while still bright
    (the ``_fast_ring`` lesson from ``warrior_abilities``)."""
    ratio = end / start
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.08, 0.0),
        min_start_size=start, max_start_size=start,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-0.6, max_angular_velocity=0.6,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=material,
        size_over_life=float_curve((0.0, 1.0), (0.3, 1.0 + (ratio - 1.0) * 0.8), (1.0, ratio)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.10, rgba(colour, alpha)),
            (0.35, rgba(colour, alpha * 0.7)),
            (1.00, rgba(colour, 0.0))),
    )


# =========================================================================================
# One-shots
# =========================================================================================

def fireball_impact(scale=1.0, name="Fireball", smoke_count=7, time_scale=1.0):
    """Explosion at the target's torso: white-hot flash, a roiling ball of flame that
    blooms outward and lifts, a ring shockwave, an ember spray falling under gravity, and a
    few smoke puffs rising out of the top."""
    s, ts = scale, time_scale
    emitters = [
        flash("%s Flash" % name, size=1.9 * s, alpha=0.75, lifetime=0.16 * ts),
        flame_burst("%s Core" % name, count=int(16 * min(1.0, s * 1.4)), radius=0.15 * s,
                    speed=2.8 * s, size=0.85 * s, lifetime=0.55 * ts, alpha=0.50,
                    drag=4.5, buoyancy=0.8, hot=WHITE_HOT, mid=FIRE_HOT, cool=FIRE_ORANGE),
        flame_burst("%s Bloom" % name, count=int(44 * min(1.0, s * 1.3)), radius=0.25 * s,
                    speed=6.5 * s, size=1.05 * s, lifetime=0.95 * ts, alpha=0.40,
                    drag=3.6, buoyancy=1.6 * s, noise=3.0),
        billboard_ring("%s Shock" % name, start=0.5 * s, end=2.8 * s, colour=FIRE_ORANGE,
                       alpha=0.45, lifetime=0.32 * ts),
        ember_spray("%s Embers" % name, count=int(36 * min(1.0, s * 1.2)), speed=7.5 * s,
                    size=0.08 * max(s, 0.7), lifetime=0.95 * ts, gravity=-7.0, drag=1.3,
                    length=4.5),
    ]
    if smoke_count > 0:
        emitters.append(smoke("%s Smoke" % name, count=smoke_count, radius=0.35 * s,
                              size=1.1 * s, lifetime=1.0 * ts, alpha=0.24, rise=1.2 * s,
                              delay=0.25 * ts))
    return ParticleSystem(emitters=emitters)


def fire_barrage_impact():
    """~55% of the Fireball explosion and about half as long, so nine of them landing in
    1.5 s stay nine distinct pops rather than one smear."""
    return fireball_impact(scale=0.55, name="Barrage", smoke_count=0, time_scale=0.6)


def fire_blast_impact():
    """Instant eruption from under the target's feet: a scorched ground flash and racing
    fire ring, a white-hot column punching ~3 units up through the body, a flame sheath
    roiling up around it, embers flung high and raining back, smoke lifting off the top.
    The most violent fire effect, so it is the densest and the fastest."""
    column_core = Emitter(
        name="Blast Column Core",
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.30,
        spawn_rate=150.0, max_particles=66,  # --check sizes caps by rate*lifetime
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.2, 0.22),
        min_lifetime=0.26, max_lifetime=0.42,
        min_velocity=(0.0, 7.0, 0.0), max_velocity=(0.0, 11.5, 0.0),
        min_start_size=0.28, max_start_size=0.42,
        gravity=(0.0, 0.0, 0.0), drag=2.0, orbital_speed=3.0,
        render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.7), (0.3, 1.0), (1.0, 0.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.10, rgba(WHITE_HOT, 0.45)),
            (0.35, rgba(FIRE_HOT, 0.40)),
            (0.75, rgba(FIRE_ORANGE, 0.30)),
            (1.00, rgba(FIRE_ORANGE, 0.0))),
    )
    column_flame = Emitter(
        name="Blast Column Flame",
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.32,
        spawn_rate=110.0, max_particles=110,  # --check sizes caps by rate*lifetime
        bursts=[Burst(0.0, 8)],
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.3, 0.45),
        min_lifetime=0.55, max_lifetime=0.90,
        min_velocity=(-0.4, 2.5, -0.4), max_velocity=(0.4, 10.0, 0.4),
        min_start_size=0.55, max_start_size=0.95,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-2.5, max_angular_velocity=2.5,
        gravity=(0.0, 0.3, 0.0), drag=2.5, orbital_speed=1.6,
        noise_amplitude=3.0, noise_frequency=1.2,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.5)),
        color_over_lifetime=fire_ramp(0.40),
    )
    skirt = Emitter(
        name="Blast Ground Flames",
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.08,
        spawn_rate=0.0, max_particles=26,
        bursts=[Burst(0.0, 20)],
        shape=hpar.SHAPE_BOX, shape_extents=(0.5, 0.1, 0.5),
        min_lifetime=0.35, max_lifetime=0.60,
        min_velocity=(-3.2, 0.4, -3.2), max_velocity=(3.2, 1.6, 3.2),
        min_start_size=0.45, max_start_size=0.75,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-2.0, max_angular_velocity=2.0,
        gravity=(0.0, 1.5, 0.0), drag=3.5,
        noise_amplitude=2.0, noise_frequency=1.2,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.4)),
        color_over_lifetime=fire_ramp(0.40),
    )
    embers = ember_spray("Blast Embers", count=44, speed=1.5, size=0.09, lifetime=1.15,
                         gravity=-8.5, drag=0.9, radius=0.3, length=5.0)
    embers.min_velocity = (-2.4, 4.0, -2.4)
    embers.max_velocity = (2.4, 9.5, 2.4)
    embers.min_start_speed = 0.0
    embers.max_start_speed = 1.0
    # Smoke billows off the column, not the floor: spawn it up the column's height.
    blast_smoke = smoke("Blast Smoke", count=9, radius=0.4, size=1.1, lifetime=0.95,
                        alpha=0.20, rise=1.2, delay=0.22)
    blast_smoke.shape = hpar.SHAPE_CONE
    blast_smoke.shape_extents = (0.0, 2.6, 0.45)
    return ParticleSystem(emitters=[
        flat_ring("Blast Scorch", start=1.0, end=2.6, colour=FIRE_HOT, alpha=0.55,
                  lifetime=0.45, material=GLOW),
        flat_ring("Blast Ring", start=0.6, end=4.2, colour=FIRE_ORANGE, alpha=0.55,
                  lifetime=0.55),
        skirt,
        column_flame,
        column_core,
        flash("Blast Flash", size=2.0, alpha=0.65, lifetime=0.18, rise=1.0),
        embers,
        blast_smoke,
    ])


def fire_blast_hand():
    """Cast flare on hand_r: a short hot pop of flame and a handful of embers flicked out
    and upward. Small and fast -- it is the cast's punctuation, the impact is the sentence."""
    embers = ember_spray("Hand Embers", count=14, speed=3.8, size=0.055, lifetime=0.45,
                         gravity=-3.0, drag=2.0, radius=0.06, length=4.0)
    return ParticleSystem(emitters=[
        flash("Hand Flash", size=0.75, alpha=0.7, lifetime=0.12),
        flame_burst("Hand Flare", count=14, radius=0.06, speed=1.8, size=0.36,
                    lifetime=0.45, alpha=0.45, drag=4.0, buoyancy=2.0, noise=1.5,
                    hot=WHITE_HOT),
        embers,
    ])


# =========================================================================================
# Loops
# =========================================================================================

def fire_hand_channel():
    """Flickering flame swirl held in the right hand while Fireball / Fire Blast charge.
    LOCAL layers follow the hand; a few WORLD embers rise off it and are left behind."""
    swirl = looping(Emitter(
        name="Channel Flames",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
        min_velocity=(-0.15, 0.35, -0.15), max_velocity=(0.15, 0.9, 0.15),
        min_start_size=0.18, max_start_size=0.32,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-3.0, max_angular_velocity=3.0,
        gravity=(0.0, 0.6, 0.0), drag=1.0, orbital_speed=4.0,
        noise_amplitude=1.5, noise_frequency=2.0,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.5)),
        color_over_lifetime=fire_ramp(0.42, fade_in=0.12),
    ), rate=55.0, lifetime=0.42)
    core = looping(Emitter(
        name="Channel Core",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.03, 0.0, 0.0),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.1, 0.0),
        min_start_size=0.36, max_start_size=0.48,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.30, rgba(FIRE_HOT, 0.40)),
            (0.70, rgba(FIRE_ORANGE, 0.28)),
            (1.00, rgba(FIRE_ORANGE, 0.0))),
    ), rate=14.0, lifetime=0.32)
    embers = looping(Emitter(
        name="Channel Embers",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.14, 0.0, 0.0),
        min_velocity=(-0.3, 0.5, -0.3), max_velocity=(0.3, 1.4, 0.3),
        min_start_size=0.045, max_start_size=0.075,
        gravity=(0.0, 0.5, 0.0), drag=0.6,
        noise_amplitude=2.0, noise_frequency=2.0,
        render_mode=hpar.RENDER_STRETCHED, length_scale=3.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.10, rgba(EMBER, 1.0)),
            (0.60, rgba(FIRE_ORANGE, 0.85)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=12.0, lifetime=0.85)
    return ParticleSystem(emitters=[core, swirl, embers])


def _orb(prefix, scale, hot_rate, body_rate, roil_rate):
    """LOCAL layers of a projectile orb: white-hot centre, orange body, roiling outer
    flames orbiting fast enough to read as churning."""
    s = scale
    centre = looping(Emitter(
        name="%s Centre" % prefix,
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_POINT,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.36 * s, max_start_size=0.44 * s,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.85), (0.5, 1.0), (1.0, 0.9)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.25, rgba(WHITE_HOT, 0.75)),
            (0.75, rgba(FIRE_HOT, 0.6)),
            (1.00, rgba(FIRE_HOT, 0.0))),
    ), rate=hot_rate, lifetime=0.16)
    body = looping(Emitter(
        name="%s Body" % prefix,
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.06 * s, 0.0, 0.0),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=0.55 * s, max_start_size=0.70 * s,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-3.0, max_angular_velocity=3.0,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.9)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_HOT, 0.0)),
            (0.25, rgba(FIRE_ORANGE, 0.42)),
            (0.75, rgba(FIRE_ORANGE, 0.32)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=body_rate, lifetime=0.22)
    roil = looping(Emitter(
        name="%s Roil" % prefix,
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.16 * s, 0.0, 0.0),
        min_velocity=(-0.4, -0.4, -0.4), max_velocity=(0.4, 0.4, 0.4),
        min_start_size=0.24 * s, max_start_size=0.38 * s,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-4.0, max_angular_velocity=4.0,
        gravity=(0.0, 0.0, 0.0), orbital_speed=7.0,
        noise_amplitude=3.0, noise_frequency=3.0,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.6)),
        color_over_lifetime=fire_ramp(0.42, fade_in=0.15),
    ), rate=roil_rate, lifetime=0.22)
    return [body, roil, centre]


def _wake_flames(prefix, scale, rate, lifetime, alpha=0.42):
    """WORLD flames shed by the projectile: they stay where they spawned, so the wake is
    ``speed * lifetime`` long and tapers because older puffs shrink and fade."""
    s = scale
    return looping(Emitter(
        name="%s Wake" % prefix,
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.20 * s, 0.0, 0.0),
        min_velocity=(-0.6, -0.2, -0.6), max_velocity=(0.6, 0.9, 0.6),
        min_start_size=0.50 * s, max_start_size=0.80 * s,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-3.0, max_angular_velocity=3.0,
        gravity=(0.0, 1.2, 0.0), drag=1.5,
        noise_amplitude=2.0, noise_frequency=1.5,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 1.0), (0.4, 0.95), (1.0, 0.35)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_HOT, 0.0)),
            (0.06, rgba(FIRE_HOT, alpha)),
            (0.25, rgba(FIRE_ORANGE, alpha * 0.92)),
            (0.75, rgba(FIRE_RED, alpha * 0.45)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=rate, lifetime=lifetime)


def _wake_streaks(prefix, scale, rate, lifetime, alpha=0.34, inherit=0.35):
    """Stretched flame streaks that inherit a fraction of the projectile's velocity, so
    each one is drawn along the flight path. They bridge the gaps between the wake puffs:
    every particle spawned in one frame starts at the same node position, so without a
    streak the wake beads into a dotted line at low frame rates."""
    s = scale
    e = looping(Emitter(
        name="%s Streaks" % prefix,
        simulation_space=hpar.SIM_WORLD,
        inherit_velocity=inherit,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.10 * s, 0.0, 0.0),
        min_velocity=(-0.3, -0.1, -0.3), max_velocity=(0.3, 0.4, 0.3),
        min_start_size=0.26 * s, max_start_size=0.40 * s,
        gravity=(0.0, 0.0, 0.0), drag=5.0,
        render_mode=hpar.RENDER_STRETCHED, length_scale=3.5,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.45)),
        # Fades in late on purpose: a stretched quad is centred on its particle, so a
        # newborn streak would poke out ahead of the orb.
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_HOT, 0.0)),
            (0.25, rgba(FIRE_HOT, alpha)),
            (0.50, rgba(FIRE_ORANGE, alpha * 0.85)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=rate, lifetime=lifetime)
    return e


def _wake_embers(prefix, rate, lifetime, size=0.06, spread=1.6):
    return looping(Emitter(
        name="%s Embers" % prefix,
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
        min_velocity=(-spread, -spread * 0.6, -spread), max_velocity=(spread, spread, spread),
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, -2.5, 0.0), drag=1.2,
        render_mode=hpar.RENDER_STRETCHED, length_scale=3.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.08, rgba(EMBER, 1.0)),
            (0.60, rgba(FIRE_ORANGE, 0.85)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=rate, lifetime=lifetime)


def fireball_trail():
    """The flying Fireball: a ~0.6-unit churning orb with a white-hot heart, shedding a
    wake of flame puffs (~8 units at 24 u/s), bright embers, and a wisp of smoke."""
    wake_smoke = looping(Emitter(
        name="Fireball Smoke",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
        min_velocity=(-0.3, 0.2, -0.3), max_velocity=(0.3, 0.8, 0.3),
        min_start_size=0.40, max_start_size=0.60,
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.5, 0.0), drag=1.0,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (1.0, 1.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_SMOKE, 0.0)),
            (0.40, rgba(FIRE_SMOKE, 0.16)),
            (1.00, rgba(FIRE_SMOKE, 0.0))),
    ), rate=16.0, lifetime=0.5)
    return ParticleSystem(emitters=[
        wake_smoke,
        _wake_flames("Fireball", 1.25, rate=100.0, lifetime=0.36),
        _wake_streaks("Fireball", 1.0, rate=60.0, lifetime=0.26),
        _wake_embers("Fireball", rate=40.0, lifetime=0.42),
    ] + _orb("Fireball", 1.35, hot_rate=22.0, body_rate=30.0, roil_rate=45.0))


def fire_barrage_trail():
    """Three of these fly at once: a ~0.3-unit ember comet with a short wake."""
    return ParticleSystem(emitters=[
        _wake_flames("Barrage", 0.65, rate=70.0, lifetime=0.24, alpha=0.42),
        _wake_streaks("Barrage", 0.55, rate=36.0, lifetime=0.18, alpha=0.40),
        _wake_embers("Barrage", rate=24.0, lifetime=0.28, size=0.05, spread=1.0),
    ] + _orb("Barrage", 0.55, hot_rate=18.0, body_rate=20.0, roil_rate=24.0))


def fireball_burn():
    """Fireball DoT idle: small flames licking up the body (densest around the torso),
    the odd ember drifting off, and a faint warm haze. Modest -- it runs for 4 s on every
    burning target in view."""
    # Spawned low in a short cone but invisible for the first ~30% of life: by the time a
    # flame fades in it has risen into the 0.3-1.6 band, so the burn sits on the torso
    # instead of pooling at the feet (the system origin).
    flames = looping(Emitter(
        name="Burn Flames",
        simulation_space=hpar.SIM_LOCAL,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.8, 0.34),
        min_velocity=(-0.1, 1.2, -0.1), max_velocity=(0.1, 1.9, 0.1),
        min_start_size=0.36, max_start_size=0.58,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-2.5, max_angular_velocity=2.5,
        gravity=(0.0, 0.5, 0.0), drag=1.0, orbital_speed=1.2,
        noise_amplitude=1.5, noise_frequency=2.0,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.5), (0.45, 1.0), (1.0, 0.45)),
        color_over_lifetime=color_curve(
            (0.00, rgba(FIRE_HOT, 0.0)),
            (0.28, rgba(FIRE_HOT, 0.0)),
            (0.40, rgba(FIRE_HOT, 0.42)),
            (0.55, rgba(FIRE_ORANGE, 0.44)),
            (0.85, rgba(FIRE_RED, 0.22)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=34.0, lifetime=0.70)
    embers = looping(Emitter(
        name="Burn Embers",
        simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.4, 0.32),
        min_velocity=(-0.3, 0.6, -0.3), max_velocity=(0.3, 1.3, 0.3),
        min_start_size=0.045, max_start_size=0.07,
        gravity=(0.0, 0.4, 0.0), drag=0.5,
        noise_amplitude=2.0, noise_frequency=2.0,
        render_mode=hpar.RENDER_STRETCHED, length_scale=3.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WHITE_HOT, 0.0)),
            (0.10, rgba(EMBER, 1.0)),
            (0.60, rgba(FIRE_ORANGE, 0.8)),
            (1.00, rgba(FIRE_RED, 0.0))),
    ), rate=6.0, lifetime=0.95)
    return ParticleSystem(emitters=[flames, embers])


if __name__ == "__main__":
    write(fireball_impact(), "FireballImpact.hpar")
    write(fire_blast_impact(), "FireBlastImpact.hpar")
    write(fire_blast_hand(), "FireBlastHand.hpar")
    write(fire_barrage_impact(), "FireBarrageImpact.hpar")
    write(fire_hand_channel(), "FireHandChannel.hpar")
    write(fireball_trail(), "FireballTrail.hpar")
    write(fire_barrage_trail(), "FireBarrageTrail.hpar")
    write(fireball_burn(), "FireballBurn.hpar")
