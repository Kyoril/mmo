# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Scout weapon techniques: strike impacts, the bleed, the stun, the throwing knife and the
hunter's mark. Run from the repo root::

    py -3.14 tools/particle_gen/recipes/scout_strikes.py

Writes into ``data/client/Particles/Scout/``. Palette and helpers: ``scout_common``.

Strike impacts are attached to the target's ``spine_03``. Bone-attached emitters inherit the
bone's rotation (spawn velocity is in bone space even in world simulation; only gravity is
world space), so nothing here relies on a velocity *direction* -- sprays are near-symmetric
and fall under gravity, and the blade cuts are billboards whose roll does the orienting.

Every strike is built from the same three beats, scaled per technique so the four of them
read apart at a glance:

1. the **cut** -- one or more crescent ``SLASH`` sprites, hot white edge -> cold steel;
2. the **ting** -- a spinning star glint and a few stretched steel sparks;
3. the **wound** -- crimson droplets and a soft blood puff (none for the knockout blow).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import scout_common as sc
from scout_common import (
    BEAM, BLOOD, BLOOD_DARK, CROSSHAIR, GLOW, MARK, MARK_HOT, RING, SIGIL, STAR, STEEL,
    STEEL_HOT, STUN, STUN_HOT, Burst, ParticleSystem, blood_mist, blood_spray, color_curve, e,
    flash, float_curve, glint, hpar, lift, looping, rgba, ring, slash_arc, sparks)


# =========================================================================================
# Strike impacts
# =========================================================================================

def wounding_strike_impact():
    """A clean diagonal cut that opens a wound: one big crescent, a steel ting, a generous
    crimson spray. The wound is the point of the technique, so the blood leads."""
    return ParticleSystem(emitters=[
        flash("Cut Flash", 0.7, STEEL_HOT, alpha=0.55, lifetime=0.10),
        slash_arc("Cut", 1.55, roll=(0.5, 1.1)),
        slash_arc("Cut Echo", 1.35, colour=BLOOD, hot=STEEL_HOT, alpha=0.45, lifetime=0.26,
                  delay=0.03, roll=(0.5, 1.1)),
        glint("Edge Glint", 0.55, lifetime=0.26),
        sparks("Steel Sparks", 8, 3.5),
        blood_spray("Blood Spray", 18, 2.6),
        blood_mist("Blood Mist", 4),
    ])


def ruthless_strike_impact():
    """The finisher: a heavy crossing double cut, a bright flash, a hard steel burst and a
    deep spray. Bigger and brighter than Wounding Strike so a consumed opener reads."""
    return ParticleSystem(emitters=[
        flash("Hit Flash", 1.0, STEEL_HOT, alpha=0.6, lifetime=0.12),
        slash_arc("Cut A", 1.85, roll=(0.4, 0.9), alpha=0.9),
        slash_arc("Cut B", 1.85, roll=(3.6, 4.1), alpha=0.9, delay=0.06),
        ring("Impact Ring", 0.4, 1.6, STEEL, alpha=0.5, lifetime=0.22, horizontal=False),
        glint("Edge Glint", 0.75, lifetime=0.32, spin=6.5),
        sparks("Steel Sparks", 14, 4.5, size=0.09),
        blood_spray("Blood Spray", 24, 3.2, size=0.11),
        blood_mist("Blood Mist", 6, size=0.55),
    ])


def quick_cut_impact():
    """A fast flick: one small sharp crescent and a bright ting, very little blood. It must
    stay small -- Quick Cut is off the global cooldown and lands between other techniques."""
    return ParticleSystem(emitters=[
        slash_arc("Flick", 1.05, lifetime=0.16, alpha=0.8, roll=(5.0, 5.6), grow=1.15),
        glint("Flick Glint", 0.45, lifetime=0.2, spin=8.0),
        sparks("Flick Sparks", 6, 3.0, size=0.06, lifetime=0.24),
        blood_spray("Flick Blood", 6, 2.0, size=0.08, lifetime=0.4),
    ])


def twin_slash_impact():
    """Two blades in an X: two crescents mirrored and staggered by a beat, a glint at the
    crossing, blood from both cuts."""
    return ParticleSystem(emitters=[
        flash("X Flash", 0.8, STEEL_HOT, alpha=0.5, lifetime=0.10),
        slash_arc("Cut Right", 1.65, roll=(0.6, 0.9), alpha=0.88),
        slash_arc("Cut Left", 1.65, roll=(2.25, 2.55), alpha=0.88, delay=0.09),
        glint("Cross Glint", 0.6, lifetime=0.3, delay=0.09, spin=-6.0),
        sparks("Steel Sparks A", 7, 3.6),
        sparks("Steel Sparks B", 7, 3.6, delay=0.09),
        blood_spray("Blood A", 12, 2.6),
        blood_spray("Blood B", 12, 2.6, delay=0.09),
        blood_mist("Blood Mist", 5, delay=0.05),
    ])


# =========================================================================================
# Wounded -- the bleed (spell 165)
# =========================================================================================

def wounded_bleed():
    """Aura idle on a bleeding target for 6 s, attached to ``spine_03``. Sparse drops that
    fall under (world) gravity and a faint crimson haze -- readable, not gory. ~10 live."""
    drops = looping(e(
        name="Bleed Drops", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.16, 0.0, 0.0),
        min_velocity=(-0.15, -0.2, -0.15), max_velocity=(0.15, 0.1, 0.15),
        min_start_size=0.05, max_start_size=0.09,
        gravity=(0.0, -6.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=2.2,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.7)),
        color_over_lifetime=color_curve(
            (0.00, rgba(BLOOD, 0.0)),
            (0.10, rgba(BLOOD, 0.9)),
            (0.70, rgba(BLOOD, 0.75)),
            (1.00, rgba(BLOOD_DARK, 0.0)))),
        rate=7.0, lifetime=0.6)
    haze = looping(e(
        name="Bleed Haze", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.18, 0.0, 0.0),
        min_velocity=(-0.1, -0.15, -0.1), max_velocity=(0.1, 0.05, 0.1),
        min_start_size=0.28, max_start_size=0.42, material_name=sc.SMOKE,
        size_over_life=float_curve((0.0, 0.6), (1.0, 1.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(BLOOD, 0.0)),
            (0.30, rgba(BLOOD, 0.16)),
            (1.00, rgba(BLOOD_DARK, 0.0)))),
        rate=2.5, lifetime=0.9)
    return ParticleSystem(emitters=[haze, drops])


def wounded_tick():
    """Every 2 s bleed tick: a small spurt so the damage number has a source."""
    return ParticleSystem(emitters=[
        blood_spray("Tick Spurt", 9, 2.2, size=0.08, lifetime=0.45),
        blood_mist("Tick Mist", 2, size=0.35, alpha=0.25),
    ])


# =========================================================================================
# Knockout -- stun (spell 161)
# =========================================================================================

def knockout_impact():
    """Pommel to the head, attached to the target's ``head``: a hard white-gold flash, a
    burst of stars flung out, a ring of 'impact lines'. Cartoonish on purpose -- the stun
    has to read instantly."""
    star_burst = e(name="Star Burst", simulation_space=hpar.SIM_WORLD,
                   bursts=[Burst(0.0, 10)],
                   shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
                   min_start_speed=2.0, max_start_speed=3.4,
                   min_lifetime=0.35, max_lifetime=0.55, drag=4.0,
                   min_start_size=0.16, max_start_size=0.26,
                   min_angular_velocity=-7.0, max_angular_velocity=7.0,
                   material_name=STAR,
                   size_over_life=float_curve((0.0, 0.5), (0.2, 1.0), (1.0, 0.4)),
                   color_over_lifetime=color_curve(
                       (0.00, rgba(STUN_HOT, 1.0)),
                       (0.50, rgba(STUN, 0.85)),
                       (1.00, rgba(STUN, 0.0))))
    lines = e(name="Impact Lines", bursts=[Burst(0.0, 14)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.05, 0.0, 0.0),
              min_start_speed=5.0, max_start_speed=7.0,
              min_lifetime=0.12, max_lifetime=0.18, drag=6.0,
              min_start_size=0.07, max_start_size=0.1,
              render_mode=hpar.RENDER_STRETCHED, length_scale=6.0, material_name=BEAM,
              size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(STUN_HOT, 0.9)),
                  (1.00, rgba(STUN, 0.0))))
    return ParticleSystem(emitters=[
        flash("Bonk Flash", 0.9, STUN_HOT, alpha=0.7, lifetime=0.12),
        ring("Bonk Ring", 0.3, 1.3, STUN, alpha=0.55, lifetime=0.22, horizontal=False),
        lines, star_burst,
        glint("Bonk Glint", 0.7, colour=STUN, hot=STUN_HOT, lifetime=0.3),
    ])


def stun_stars():
    """Aura idle while knocked out (6 s), feet origin. A halo of little gold stars circling
    above the head -- the universal 'seeing stars' read. Drag-lifted to ~2.05 units and
    held there by zero gravity; the orbit carries them round. ~12 live."""
    stars = e(name="Circling Stars", shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.38),
              min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
              orbital_speed=3.4, warmup_time=1.5,
              min_start_size=0.17, max_start_size=0.24,
              min_angular_velocity=-3.0, max_angular_velocity=3.0,
              material_name=STAR,
              size_over_life=float_curve((0.0, 0.5), (0.25, 1.0), (1.0, 0.6)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(STUN_HOT, 0.0)),
                  (0.25, rgba(STUN_HOT, 0.95)),
                  (0.70, rgba(STUN, 0.8)),
                  (1.00, rgba(STUN, 0.0))))
    lift(stars, height=2.05, drag=5.0, hidden=0.3)
    looping(stars, rate=8.0, lifetime=1.6)
    motes = e(name="Daze Motes", shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.3),
              min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
              orbital_speed=-2.2, warmup_time=1.5,
              min_start_size=0.10, max_start_size=0.16,
              size_over_life=float_curve((0.0, 0.6), (1.0, 1.0)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(STUN, 0.0)),
                  (0.40, rgba(STUN, 0.45)),
                  (1.00, rgba(STUN, 0.0))))
    lift(motes, height=2.0, drag=5.0, hidden=0.25)
    looping(motes, rate=6.0, lifetime=1.0)
    return ParticleSystem(emitters=[motes, stars])


# =========================================================================================
# Throwing Knife (spell 167)
# =========================================================================================

def throw_release():
    """At ``hand_r`` on the throw: a quick steel glint and a puff of air."""
    return ParticleSystem(emitters=[
        glint("Release Glint", 0.5, lifetime=0.22, spin=9.0),
        flash("Release Flash", 0.35, STEEL_HOT, alpha=0.45, lifetime=0.1),
    ])


def throwing_knife_trail():
    """On the knife (15 u/s). A thin bright blade core riding with the projectile and
    stretched along the flight, a tumbling star glint that reads as a spinning knife catching
    the light, and a fine silver wake. Steady state ~30 live."""
    blade = looping(e(
        name="Knife Blade", simulation_space=hpar.SIM_WORLD, inherit_velocity=1.0,
        min_start_size=0.09, max_start_size=0.11,
        min_start_rotation=0.0, max_start_rotation=0.0,
        render_mode=hpar.RENDER_STRETCHED, length_scale=4.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.9)),
        color_over_lifetime=color_curve(
            (0.00, rgba(STEEL_HOT, 0.0)),
            (0.25, rgba(STEEL_HOT, 0.95)),
            (0.70, rgba(STEEL, 0.8)),
            (1.00, rgba(STEEL, 0.0)))),
        rate=50.0, lifetime=0.08)
    spin = looping(e(
        name="Knife Spin Glint",
        min_start_size=0.34, max_start_size=0.40,
        min_angular_velocity=22.0, max_angular_velocity=26.0,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.8), (1.0, 1.0)),
        color_over_lifetime=color_curve(
            (0.00, rgba(STEEL_HOT, 0.0)),
            (0.30, rgba(STEEL_HOT, 0.85)),
            (1.00, rgba(STEEL, 0.0)))),
        rate=14.0, lifetime=0.18)
    wake = looping(e(
        name="Knife Wake", simulation_space=hpar.SIM_WORLD, inherit_velocity=0.2,
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.03, 0.0, 0.0),
        min_velocity=(-0.1, -0.1, -0.1), max_velocity=(0.1, 0.1, 0.1),
        min_start_size=0.05, max_start_size=0.08,
        render_mode=hpar.RENDER_STRETCHED, length_scale=8.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(STEEL_HOT, 0.55)),
            (1.00, rgba(STEEL, 0.0)))),
        rate=40.0, lifetime=0.22)
    return ParticleSystem(emitters=[wake, spin, blade])


def throwing_knife_impact():
    """The knife sinks in at ``spine_03``: a bright ting, a burst of sparks, a little blood."""
    return ParticleSystem(emitters=[
        flash("Thunk Flash", 0.55, STEEL_HOT, alpha=0.55, lifetime=0.1),
        glint("Thunk Glint", 0.6, lifetime=0.28, spin=7.0),
        sparks("Thunk Sparks", 10, 3.8, size=0.08),
        blood_spray("Thunk Blood", 10, 2.2, size=0.09, lifetime=0.45),
    ])


# =========================================================================================
# Mark Weakness (spell 169)
# =========================================================================================

def mark_weakness_apply():
    """Impact on the marked target, feet origin. A crimson reticle snaps down onto the
    target -- big and faint shrinking to tight and bright at chest height -- with four
    streaks converging on it and a ring tightening on the ground."""
    reticle = e(name="Reticle Snap", bursts=[Burst(0.0, 1)],
                min_lifetime=0.55, max_lifetime=0.55,
                min_start_size=2.6, max_start_size=2.6,
                min_start_rotation=0.785, max_start_rotation=0.785,
                min_angular_velocity=-1.4, max_angular_velocity=-1.4,
                material_name=CROSSHAIR,
                size_over_life=float_curve((0.0, 1.0), (0.35, 0.42), (1.0, 0.38)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(MARK, 0.0)),
                    (0.15, rgba(MARK, 0.55)),
                    (0.40, rgba(MARK_HOT, 0.95)),
                    (1.00, rgba(MARK, 0.0))))
    lift(reticle, height=1.25, drag=6.0, hidden=0.04)
    converge = e(name="Converging Streaks", bursts=[Burst(0.0, 16)],
                 shape=hpar.SHAPE_SPHERE, shape_extents=(1.0, 0.0, 0.0),
                 min_lifetime=0.25, max_lifetime=0.32,
                 min_start_size=0.06, max_start_size=0.09,
                 attractor_position=(0.0, 1.2, 0.0), attractor_strength=40.0, drag=3.0,
                 render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
                 size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
                 color_over_lifetime=color_curve(
                     (0.00, rgba(MARK, 0.0)),
                     (0.30, rgba(MARK_HOT, 0.8)),
                     (1.00, rgba(MARK, 0.0))))
    lift(converge, height=1.2, drag=8.0, hidden=0.05)
    ground = e(name="Closing Ring", bursts=[Burst(0.0, 1)],
               min_lifetime=0.4, max_lifetime=0.4,
               min_start_size=3.2, max_start_size=3.2,
               min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.04, 0.0),
               render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.35)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(MARK, 0.0)),
                   (0.25, rgba(MARK, 0.5)),
                   (1.00, rgba(MARK, 0.0))))
    return ParticleSystem(emitters=[ground, converge, reticle])


def mark_weakness_aura():
    """Aura idle while marked (10 s), feet origin. A slowly turning crimson reticle hovers
    above the head and breathes; a faint mark glows on the ground. One quad at a time per
    layer -- the mark must be readable in a group fight, not noisy. ~4 live."""
    reticle = e(name="Hover Reticle",
                min_start_size=0.62, max_start_size=0.62,
                min_angular_velocity=0.9, max_angular_velocity=0.9, warmup_time=2.0,
                material_name=CROSSHAIR,
                size_over_life=float_curve((0.0, 0.92), (0.5, 1.05), (1.0, 0.92)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(MARK, 0.0)),
                    (0.30, rgba(MARK_HOT, 0.85)),
                    (0.70, rgba(MARK, 0.75)),
                    (1.00, rgba(MARK, 0.0))))
    # Two overlapping at a time crossfade into one steady reticle; the hidden share of the
    # life covers the ~0.5 s the drag lift needs to settle.
    lift(reticle, height=2.25, drag=6.0, hidden=0.25, spread=0.0)
    looping(reticle, rate=0.8, lifetime=2.5)
    reticle.min_lifetime = 2.5
    ground = looping(e(
        name="Ground Mark",
        min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.04, 0.0),
        min_start_size=1.5, max_start_size=1.5,
        min_angular_velocity=-0.5, max_angular_velocity=-0.5, warmup_time=2.0,
        render_mode=hpar.RENDER_HORIZONTAL, material_name=CROSSHAIR,
        size_over_life=float_curve((0.0, 0.95), (1.0, 1.05)),
        color_over_lifetime=color_curve(
            (0.00, rgba(MARK, 0.0)),
            (0.40, rgba(MARK, 0.30)),
            (1.00, rgba(MARK, 0.0)))),
        rate=1.0, lifetime=2.0)
    return ParticleSystem(emitters=[ground, reticle])


EFFECTS = [
    (wounding_strike_impact, "WoundingStrikeImpact.hpar"),
    (ruthless_strike_impact, "RuthlessStrikeImpact.hpar"),
    (quick_cut_impact, "QuickCutImpact.hpar"),
    (twin_slash_impact, "TwinSlashImpact.hpar"),
    (wounded_bleed, "WoundedBleed.hpar"),
    (wounded_tick, "WoundedTick.hpar"),
    (knockout_impact, "KnockoutImpact.hpar"),
    (stun_stars, "StunStars.hpar"),
    (throw_release, "ThrowRelease.hpar"),
    (throwing_knife_trail, "ThrowingKnifeTrail.hpar"),
    (throwing_knife_impact, "ThrowingKnifeImpact.hpar"),
    (mark_weakness_apply, "MarkWeaknessApply.hpar"),
    (mark_weakness_aura, "MarkWeaknessAura.hpar"),
]


def main():
    wanted = set(sys.argv[1:])
    for build, filename in EFFECTS:
        if not wanted or filename[:-5] in wanted:
            sc.write(build(), filename)


if __name__ == "__main__":
    main()
