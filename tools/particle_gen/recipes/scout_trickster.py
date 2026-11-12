# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Scout Trickster talents (2026-11-02): Riposte, Slip the Blade, Cheap Shot, the Energy gains
of Opportunist and Slippery, and Smoke and Mirrors' counterstrike. Run from the repo root::

    py tools/particle_gen/recipes/scout_trickster.py

Writes into ``data/client/Particles/Scout/``. Palette and helpers: ``scout_common``.

The Trickster path's colour is a pale violet (its talent tree accent, 0xA389FF): the
evasion smoke's hue, lifted. It marks every effect that comes *from a dodge*, so a player
learns that violet on the blade hand means "the answer is primed". Steel stays the blade,
amber stays energy, crimson stays a wound or a vulnerability.

The arming effects (Riposte, Slip the Blade) fire every few seconds in a fight, so they are
small and quick; their idles sit on ``hand_r`` with two or three live particles.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import scout_common as sc
from scout_common import (
    BEAM, BLOOD, ENERGY, ENERGY_HOT, GLOW, MARK, MARK_HOT, RING, SHADOW, SLASH, SMOKE, STAR,
    STEEL, STEEL_HOT, Burst, ParticleSystem, blood_spray, color_curve, e, flash, float_curve,
    glint, hpar, lift, looping, rgba, ring, slash_arc, smoke_puff, sparks)

TRICK_HOT = (0.90, 0.86, 1.00)
TRICK = (0.64, 0.54, 1.00)         # the Trickster path accent


# =========================================================================================
# Riposte (talent 1428 -> buff 1429): after a dodge, the next Quick Cut is free and harder
# =========================================================================================

def riposte_ready():
    """At ``hand_r``, on arming: the blade flicks round -- a violet crescent spins once
    around the hand, a few streaks whip off it, and the edge glints."""
    flick = slash_arc("Flick Arc", 0.7, colour=TRICK, hot=TRICK_HOT, alpha=0.8, lifetime=0.26)
    flick.min_angular_velocity, flick.max_angular_velocity = 14.0, 16.0
    whip = e(name="Whip Streaks", bursts=[Burst(0.0, 8)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
             min_start_speed=2.0, max_start_speed=3.2, drag=6.0,
             min_lifetime=0.16, max_lifetime=0.24,
             min_start_size=0.035, max_start_size=0.05,
             render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
             size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(TRICK_HOT, 0.9)),
                 (1.00, rgba(TRICK, 0.0))))
    return ParticleSystem(emitters=[
        flick, whip,
        glint("Edge Glint", 0.45, colour=TRICK, hot=STEEL_HOT, lifetime=0.3, delay=0.08),
    ])


def riposte_aura():
    """Idle at ``hand_r`` while Riposte is primed (up to 8 s): a small violet crescent
    turning slowly round the blade hand -- the answer waiting. ~2 live."""
    crescent = looping(e(
        name="Primed Crescent",
        warmup_time=1.0,
        min_start_size=0.36, max_start_size=0.42,
        min_angular_velocity=3.5, max_angular_velocity=4.5,
        material_name=SLASH,
        size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
        color_over_lifetime=color_curve(
            (0.00, rgba(TRICK, 0.0)),
            (0.35, rgba(TRICK_HOT, 0.6)),
            (1.00, rgba(TRICK, 0.0)))),
        rate=1.6, lifetime=0.9)
    haze = looping(e(
        name="Primed Haze",
        warmup_time=1.0,
        min_start_size=0.28, max_start_size=0.32,
        size_over_life=float_curve((0.0, 0.9), (1.0, 1.1)),
        color_over_lifetime=color_curve(
            (0.00, rgba(TRICK, 0.0)),
            (0.50, rgba(TRICK, 0.2)),
            (1.00, rgba(TRICK, 0.0)))),
        rate=1.5, lifetime=1.2)
    return ParticleSystem(emitters=[haze, crescent])


# =========================================================================================
# Slip the Blade (talent 1425 -> buff 1434): after a dodge, the next technique crits
# =========================================================================================

def slip_the_blade_ready():
    """At ``hand_r``, on arming: a puff of violet smoke slips off the blade and a big
    violet-white star flares on the edge -- bigger than Riposte, it is the crit."""
    return ParticleSystem(emitters=[
        smoke_puff("Slip Smoke", 5, 0.08, 0.32, 0.45, colour=TRICK, alpha=0.28, rise=0.3,
                   spread=0.9),
        ring("Slip Ring", 0.15, 0.8, TRICK_HOT, alpha=0.5, lifetime=0.22, horizontal=False),
        glint("Crit Flare", 0.75, colour=TRICK, hot=STEEL_HOT, lifetime=0.42, delay=0.05,
              spin=4.0),
        flash("Crit Glow", 0.5, TRICK, alpha=0.4, lifetime=0.3, delay=0.05),
    ])


def slip_the_blade_aura():
    """Idle at ``hand_r`` while the crit is primed (up to 6 s): a violet star twinkling on
    the weapon and a thread of smoke curling off it. ~3 live."""
    twinkle = looping(e(
        name="Primed Twinkle",
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.06, 0.0, 0.0),
        warmup_time=1.0,
        min_start_size=0.2, max_start_size=0.28,
        min_angular_velocity=2.5, max_angular_velocity=4.5,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.4, 1.0), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(TRICK_HOT, 0.0)),
            (0.40, rgba(TRICK_HOT, 0.95)),
            (1.00, rgba(TRICK, 0.0)))),
        rate=2.6, lifetime=0.6)
    curl = looping(e(
        name="Smoke Thread", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.05, 0.0, 0.0),
        min_velocity=(-0.05, 0.25, -0.05), max_velocity=(0.05, 0.45, 0.05),
        min_start_size=0.14, max_start_size=0.2,
        min_angular_velocity=-1.0, max_angular_velocity=1.0,
        material_name=SMOKE,
        size_over_life=float_curve((0.0, 0.5), (1.0, 1.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(TRICK, 0.0)),
            (0.30, rgba(TRICK, 0.22)),
            (1.00, rgba(SHADOW, 0.0)))),
        rate=3.0, lifetime=0.8)
    return ParticleSystem(emitters=[curl, twinkle])


# =========================================================================================
# Energy gains (Opportunist 1431, Slippery 1432) -- frequent, so small and silent
# =========================================================================================

def energy_gain():
    """Feet origin: a quick amber flicker at the chest -- a few sparks pop and a short
    streak of energy runs up the body. Readable, never a column: it can fire every 2 s."""
    pop = e(name="Energy Sparks", bursts=[Burst(0.0, 10)],
            shape=hpar.SHAPE_SPHERE, shape_extents=(0.18, 0.0, 0.0),
            min_start_speed=1.0, max_start_speed=2.0, drag=5.0,
            min_lifetime=0.2, max_lifetime=0.3,
            min_start_size=0.16, max_start_size=0.24,
            min_angular_velocity=-8.0, max_angular_velocity=8.0,
            material_name=STAR,
            size_over_life=float_curve((0.0, 0.5), (0.2, 1.0), (1.0, 0.2)),
            color_over_lifetime=color_curve(
                (0.00, rgba(ENERGY_HOT, 0.95)),
                (1.00, rgba(ENERGY, 0.0))))
    lift(pop, height=1.15, drag=9.0, hidden=0.0, spread=0.15)
    streaks = e(name="Energy Streaks", bursts=[Burst(0.0, 9)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.3, 0.35),
                min_velocity=(0.0, 2.6, 0.0), max_velocity=(0.0, 4.0, 0.0),
                drag=3.0, orbital_speed=1.2,
                min_lifetime=0.3, max_lifetime=0.42,
                min_start_size=0.06, max_start_size=0.09,
                render_mode=hpar.RENDER_STRETCHED, length_scale=6.0, material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(ENERGY_HOT, 0.0)),
                    (0.15, rgba(ENERGY_HOT, 0.9)),
                    (1.00, rgba(ENERGY, 0.0))))
    return ParticleSystem(emitters=[streaks, pop])


# =========================================================================================
# Cheap Shot (talent 1421 -> debuff 1433): the Knockout target takes 15 % more damage
# =========================================================================================

def cheap_shot_apply():
    """Target, feet origin, together with Knockout's impact: the guard breaks -- crimson
    shards burst off the chest and a cracked ring flashes out around it."""
    shards = e(name="Guard Shards", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 14)],
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
               min_velocity=(-0.6, 0.0, -0.6), max_velocity=(0.6, 1.2, 0.6),
               min_start_speed=2.0, max_start_speed=3.5,
               gravity=(0.0, -6.0, 0.0), drag=2.5,
               min_lifetime=0.35, max_lifetime=0.55,
               min_start_size=0.08, max_start_size=0.14,
               min_angular_velocity=-10.0, max_angular_velocity=10.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(MARK_HOT, 1.0)),
                   (0.40, rgba(MARK, 0.8)),
                   (1.00, rgba(MARK, 0.0))))
    lift(shards, height=1.2, drag=12.0, hidden=0.0, spread=0.05)
    crack = ring("Cracked Ring", 0.3, 1.6, MARK, alpha=0.55, lifetime=0.3, horizontal=False)
    lift(crack, height=1.2, drag=12.0, hidden=0.0, spread=0.0)
    return ParticleSystem(emitters=[crack, shards])


def cheap_shot_aura():
    """Idle while the target is exposed (6 s), feet origin: faint crimson flakes peel off
    the chest and fall -- a broken guard. Distinct from Mark Weakness, which hovers a
    reticle over the head. ~5 live."""
    flakes = looping(e(
        name="Exposed Flakes", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_BOX, shape_extents=(0.3, 0.35, 0.3),
        min_velocity=(-0.15, -0.2, -0.15), max_velocity=(0.15, 0.1, 0.15),
        gravity=(0.0, -1.0, 0.0),
        min_start_size=0.08, max_start_size=0.13,
        min_angular_velocity=-4.0, max_angular_velocity=4.0,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(MARK_HOT, 0.0)),
            (0.25, rgba(MARK, 0.75)),
            (1.00, rgba(MARK, 0.0)))),
        rate=6.0, lifetime=0.8)
    lift(flakes, height=1.15, drag=10.0, hidden=0.08, spread=0.2)
    return ParticleSystem(emitters=[flakes])


# =========================================================================================
# Counterstrike (1435): Smoke and Mirrors answers a dodge
# =========================================================================================

def counterstrike_impact():
    """Target chest (``spine_03``): a violet-edged crescent cut from out of the smoke, a
    spray of steel sparks and a little blood -- the return blow."""
    return ParticleSystem(emitters=[
        flash("Counter Flash", 0.6, TRICK_HOT, alpha=0.5, lifetime=0.12),
        slash_arc("Counter Cut", 1.0, colour=TRICK, hot=STEEL_HOT, alpha=0.85, lifetime=0.3),
        smoke_puff("Counter Smoke", 4, 0.1, 0.4, 0.4, colour=TRICK, alpha=0.22, rise=0.2,
                   spread=1.0),
        sparks("Counter Sparks", 10, 3.0, colour=TRICK, hot=STEEL_HOT),
        blood_spray("Counter Blood", 6, 2.2, delay=0.03),
    ])


EFFECTS = [
    (riposte_ready, "RiposteReady.hpar"),
    (riposte_aura, "RiposteAura.hpar"),
    (slip_the_blade_ready, "SlipTheBladeReady.hpar"),
    (slip_the_blade_aura, "SlipTheBladeAura.hpar"),
    (energy_gain, "EnergyGain.hpar"),
    (cheap_shot_apply, "CheapShotApply.hpar"),
    (cheap_shot_aura, "CheapShotAura.hpar"),
    (counterstrike_impact, "CounterstrikeImpact.hpar"),
]


def main():
    wanted = set(sys.argv[1:])
    for build, filename in EFFECTS:
        if not wanted or filename[:-5] in wanted:
            sc.write(build(), filename)


if __name__ == "__main__":
    main()
