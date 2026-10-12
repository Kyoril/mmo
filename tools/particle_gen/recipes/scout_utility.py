# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Scout self-buffs: Evasive Step, Smoke and Mirrors, Sprint, Fever Pitch, Steady Hand. Run
from the repo root::

    py -3.14 tools/particle_gen/recipes/scout_utility.py

Writes into ``data/client/Particles/Scout/``. Palette and helpers: ``scout_common``.

Each buff has a one-shot **activation** (played with the ability sound on CAST_SUCCEEDED)
and a quiet looping **idle** (AURA_IDLE, torn down when the aura ends). The idles run on a
character who is fighting or running, so they stay sparse: a handful of live particles, low
alpha, nothing above the head that would read as a debuff marker.

World-space idles (Sprint's wind, Evasive Step's shadow wisps) are left behind as the
scout moves -- the trail *is* the effect. Local-space idles (Fever Pitch, Steady Hand)
travel with the body.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import scout_common as sc
from scout_common import (
    BEAM, DUST, ENERGY, ENERGY_HOT, GLOW, RING, SHADOW, SHADOW_HOT, SMOKE, STAR, STEEL,
    STEEL_HOT, WIND, Burst, ParticleSystem, color_curve, e, flash, float_curve, glint, hpar,
    lift, looping, rgba, ring, smoke_puff)


# =========================================================================================
# Evasive Step (spell 166) -- dodge buff, 8 s
# =========================================================================================

def evasive_step_burst():
    """A shadow side-step: a low puff of charcoal smoke at the feet, a quick sideways whip
    of dark streaks, and a faint afterimage-glow that hangs where the body was."""
    whip = e(name="Shadow Whip", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 18)],
             shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.5, 0.35),
             min_velocity=(-0.4, 1.0, -0.4), max_velocity=(0.4, 1.8, 0.4),
             min_start_speed=0.0, max_start_speed=0.0,
             radial_acceleration=30.0, drag=4.0,
             min_lifetime=0.25, max_lifetime=0.4,
             min_start_size=0.08, max_start_size=0.13,
             render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
             size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(SHADOW_HOT, 0.0)),
                 (0.15, rgba(SHADOW_HOT, 0.6)),
                 (1.00, rgba(SHADOW, 0.0))))
    afterimage = e(name="Afterimage", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 3)],
                   shape=hpar.SHAPE_BOX, shape_extents=(0.15, 1.2, 0.15),
                   min_velocity=(0.0, 0.85, 0.0), max_velocity=(0.0, 0.95, 0.0),
                   drag=6.0,
                   min_lifetime=0.45, max_lifetime=0.55,
                   min_start_size=0.9, max_start_size=1.1,
                   material_name=SMOKE,
                   size_over_life=float_curve((0.0, 0.9), (1.0, 1.3)),
                   color_over_lifetime=color_curve(
                       (0.00, rgba(SHADOW_HOT, 0.0)),
                       (0.10, rgba(SHADOW_HOT, 0.3)),
                       (1.00, rgba(SHADOW, 0.0))))
    return ParticleSystem(emitters=[
        smoke_puff("Step Smoke", 9, 0.25, 0.75, 0.75, alpha=0.32, rise=0.4, spread=1.4),
        ring("Step Ring", 0.5, 2.4, SHADOW_HOT, alpha=0.4, lifetime=0.35),
        afterimage, whip,
    ])


def evasive_step_aura():
    """Idle while evasive (8 s): thin shadow wisps peel off the legs and are left behind as
    the scout moves -- a blur of motion, never a cloud. ~8 live."""
    wisps = looping(e(
        name="Shadow Wisps", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_BOX, shape_extents=(0.35, 1.0, 0.35),
        min_velocity=(-0.15, 0.25, -0.15), max_velocity=(0.15, 0.6, 0.15),
        min_start_size=0.35, max_start_size=0.55,
        min_angular_velocity=-1.0, max_angular_velocity=1.0,
        material_name=SMOKE,
        size_over_life=float_curve((0.0, 0.5), (1.0, 1.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(SHADOW, 0.0)),
            (0.25, rgba(SHADOW, 0.30)),
            (1.00, rgba(SHADOW, 0.0)))),
        rate=7.0, lifetime=0.9)
    lift(wisps, height=0.6, drag=3.0, hidden=0.05, spread=0.6)
    streaks = looping(e(
        name="Shadow Streaks", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.4, 0.4),
        min_velocity=(0.0, 0.8, 0.0), max_velocity=(0.0, 1.4, 0.0),
        min_start_size=0.05, max_start_size=0.08,
        render_mode=hpar.RENDER_STRETCHED, length_scale=6.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(SHADOW_HOT, 0.0)),
            (0.30, rgba(SHADOW_HOT, 0.4)),
            (1.00, rgba(SHADOW, 0.0)))),
        rate=5.0, lifetime=0.4)
    return ParticleSystem(emitters=[wisps, streaks])


# =========================================================================================
# Smoke and Mirrors (spell 1426) -- big dodge buff, 10 s
# =========================================================================================

def smoke_bomb():
    """A smoke bomb at the scout's feet: a bright pop, a billowing charcoal-violet cloud
    that swallows the silhouette for half a second, and silver glints -- the 'mirrors' --
    flickering inside it."""
    mirrors = e(name="Mirror Glints", simulation_space=hpar.SIM_WORLD,
                bursts=[Burst(0.05, 10), Burst(0.25, 8)],
                shape=hpar.SHAPE_BOX, shape_extents=(1.6, 1.8, 1.6),
                min_velocity=(0.0, 0.85, 0.0), max_velocity=(0.0, 1.0, 0.0),
                drag=1.0,
                min_lifetime=0.3, max_lifetime=0.5,
                min_start_size=0.22, max_start_size=0.36,
                min_angular_velocity=-6.0, max_angular_velocity=6.0,
                material_name=STAR,
                size_over_life=float_curve((0.0, 0.2), (0.3, 1.0), (1.0, 0.3)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(STEEL_HOT, 0.0)),
                    (0.25, rgba(STEEL_HOT, 0.95)),
                    (1.00, rgba(STEEL, 0.0))))
    return ParticleSystem(emitters=[
        flash("Pop Flash", 1.2, STEEL_HOT, alpha=0.55, lifetime=0.12),
        ring("Pop Ring", 0.6, 4.2, SHADOW_HOT, alpha=0.45, lifetime=0.4),
        smoke_puff("Cloud Low", 16, 0.4, 1.3, 1.4, alpha=0.34, rise=0.35, spread=2.2, drag=2.6),
        smoke_puff("Cloud High", 12, 0.3, 1.1, 1.6, alpha=0.26, rise=2.2, spread=1.2,
                   drag=2.2, delay=0.04),
        mirrors,
    ])


def smoke_mirrors_aura():
    """Idle while Smoke and Mirrors lasts (10 s): faint smoke curling round the body and
    the occasional silver glint flickering at a random spot -- the scout is hard to pin
    down. Local space so it stays on the body. ~10 live."""
    curl = looping(e(
        name="Smoke Curl",
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.6, 0.45),
        min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.3, 0.0),
        orbital_speed=1.4, warmup_time=1.5,
        min_start_size=0.45, max_start_size=0.7,
        min_angular_velocity=-0.8, max_angular_velocity=0.8,
        material_name=SMOKE,
        size_over_life=float_curve((0.0, 0.6), (1.0, 1.25)),
        color_over_lifetime=color_curve(
            (0.00, rgba(SHADOW, 0.0)),
            (0.35, rgba(SHADOW, 0.18)),
            (1.00, rgba(SHADOW, 0.0)))),
        rate=5.0, lifetime=1.4)
    glints = looping(e(
        name="Mirror Flicker",
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.9, 0.6),
        warmup_time=1.5,
        min_start_size=0.2, max_start_size=0.3,
        min_angular_velocity=-5.0, max_angular_velocity=5.0,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.4, 1.0), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(STEEL_HOT, 0.0)),
            (0.35, rgba(STEEL_HOT, 0.85)),
            (1.00, rgba(STEEL, 0.0)))),
        rate=3.0, lifetime=0.35)
    return ParticleSystem(emitters=[curl, glints])


# =========================================================================================
# Sprint (spell 10) -- 15 s speed buff
# =========================================================================================

def sprint_burst():
    """Push-off: dust kicked out along the ground in a low ring, a ring of air, and a
    rising whoosh of pale wind streaks."""
    dust = e(name="Kick Dust", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 14)],
             shape=hpar.SHAPE_BOX, shape_extents=(0.4, 0.1, 0.4),
             min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.5, 0.0),
             radial_acceleration=28.0, drag=3.5, gravity=(0.0, -0.3, 0.0),
             min_lifetime=0.5, max_lifetime=0.8,
             min_start_size=0.35, max_start_size=0.6,
             min_angular_velocity=-1.0, max_angular_velocity=1.0,
             material_name=SMOKE,
             size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.5)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(DUST, 0.0)),
                 (0.15, rgba(DUST, 0.34)),
                 (1.00, rgba(DUST, 0.0))))
    streaks = e(name="Wind Rise", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 16)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.4, 0.55),
                min_velocity=(0.0, 3.0, 0.0), max_velocity=(0.0, 5.0, 0.0),
                drag=3.0, orbital_speed=2.0,
                min_lifetime=0.3, max_lifetime=0.45,
                min_start_size=0.05, max_start_size=0.08,
                render_mode=hpar.RENDER_STRETCHED, length_scale=8.0, material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(WIND, 0.0)),
                    (0.15, rgba(WIND, 0.6)),
                    (1.00, rgba(WIND, 0.0))))
    return ParticleSystem(emitters=[
        ring("Air Ring", 0.5, 3.0, WIND, alpha=0.4, lifetime=0.32),
        dust, streaks,
    ])


def sprint_aura():
    """Idle while sprinting (15 s): pale wind streaks along the legs and small dust puffs
    at the feet, both left behind in world space so they trail the runner. ~14 live."""
    wind = looping(e(
        name="Leg Wind", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.0, 0.45),
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        inherit_velocity=-0.15,
        min_start_size=0.04, max_start_size=0.07,
        render_mode=hpar.RENDER_STRETCHED, length_scale=12.0, material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(WIND, 0.0)),
            (0.20, rgba(WIND, 0.42)),
            (1.00, rgba(WIND, 0.0)))),
        rate=18.0, lifetime=0.32)
    dust = looping(e(
        name="Foot Dust", simulation_space=hpar.SIM_WORLD,
        shape=hpar.SHAPE_BOX, shape_extents=(0.4, 0.05, 0.4),
        min_velocity=(-0.2, 0.15, -0.2), max_velocity=(0.2, 0.4, 0.2),
        drag=1.5,
        min_start_size=0.25, max_start_size=0.42,
        min_angular_velocity=-0.8, max_angular_velocity=0.8,
        material_name=SMOKE,
        size_over_life=float_curve((0.0, 0.5), (1.0, 1.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(DUST, 0.0)),
            (0.20, rgba(DUST, 0.22)),
            (1.00, rgba(DUST, 0.0)))),
        rate=6.0, lifetime=0.7)
    return ParticleSystem(emitters=[dust, wind])


# =========================================================================================
# Fever Pitch (spell 1409) -- doubled energy regeneration, 15 s
# =========================================================================================

def fever_pitch_burst():
    """Adrenaline surge: amber streaks shoot up the body, a hot ring pulses out, sparks
    crackle round the chest -- the energy bar's yellow made visible."""
    surge = e(name="Surge Streaks", bursts=[Burst(0.0, 22), Burst(0.1, 12)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.3, 0.5),
              min_velocity=(0.0, 3.5, 0.0), max_velocity=(0.0, 6.0, 0.0),
              drag=2.5, orbital_speed=1.5,
              min_lifetime=0.35, max_lifetime=0.55,
              min_start_size=0.06, max_start_size=0.1,
              render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
              size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ENERGY_HOT, 0.0)),
                  (0.12, rgba(ENERGY_HOT, 0.85)),
                  (0.50, rgba(ENERGY, 0.6)),
                  (1.00, rgba(ENERGY, 0.0))))
    crackle = e(name="Chest Crackle", bursts=[Burst(0.0, 10), Burst(0.15, 8)],
                shape=hpar.SHAPE_SPHERE, shape_extents=(0.25, 0.0, 0.0),
                min_start_speed=1.5, max_start_speed=3.0, drag=4.0,
                min_lifetime=0.2, max_lifetime=0.35,
                min_start_size=0.12, max_start_size=0.22,
                min_angular_velocity=-8.0, max_angular_velocity=8.0,
                material_name=STAR,
                size_over_life=float_curve((0.0, 0.5), (0.2, 1.0), (1.0, 0.2)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(ENERGY_HOT, 1.0)),
                    (1.00, rgba(ENERGY, 0.0))))
    lift(crackle, height=1.2, drag=9.0, hidden=0.0, spread=0.15)
    glow = flash("Body Glow", 1.6, ENERGY, alpha=0.35, lifetime=0.4)
    lift(glow, height=1.1, drag=9.0, hidden=0.0, spread=0.0)
    return ParticleSystem(emitters=[
        ring("Pulse Ring", 0.5, 3.4, ENERGY, alpha=0.5, lifetime=0.35),
        ring("Pulse Ring Echo", 0.5, 2.6, ENERGY_HOT, alpha=0.35, lifetime=0.3, delay=0.12),
        glow, surge, crackle,
    ])


def fever_pitch_aura():
    """Idle while Fever Pitch lasts (15 s): small amber motes rising along the body and
    the odd crackling spark -- a pulse, not a fire. Local space. ~10 live."""
    motes = looping(e(
        name="Rising Motes",
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.2, 0.4),
        min_velocity=(0.0, 0.6, 0.0), max_velocity=(0.0, 1.2, 0.0),
        orbital_speed=1.0, warmup_time=1.5,
        min_start_size=0.08, max_start_size=0.14,
        size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(ENERGY_HOT, 0.0)),
            (0.25, rgba(ENERGY_HOT, 0.85)),
            (1.00, rgba(ENERGY, 0.0)))),
        rate=11.0, lifetime=0.9)
    sparks = looping(e(
        name="Crackle",
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.5, 0.35),
        warmup_time=1.5,
        min_start_size=0.12, max_start_size=0.18,
        min_angular_velocity=-9.0, max_angular_velocity=9.0,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(ENERGY_HOT, 0.0)),
            (0.25, rgba(ENERGY_HOT, 0.9)),
            (1.00, rgba(ENERGY, 0.0)))),
        rate=2.5, lifetime=0.25)
    return ParticleSystem(emitters=[motes, sparks])


# =========================================================================================
# Steady Hand (spell 1413) -- next technique crits, 20 s
# =========================================================================================

def steady_hand_focus():
    """At ``hand_r``: a calm moment of focus. Fine steel streaks draw in to the blade hand
    (direction-free: an attractor at the origin, so the hand's rotation does not matter),
    then a single big cold glint flares on the edge."""
    draw_in = e(name="Focus Streaks", bursts=[Burst(0.0, 14)],
                shape=hpar.SHAPE_SPHERE, shape_extents=(0.8, 0.0, 0.0),
                attractor_position=(0.0, 0.0, 0.0), attractor_strength=30.0, drag=3.0,
                min_lifetime=0.25, max_lifetime=0.32,
                min_start_size=0.04, max_start_size=0.06,
                render_mode=hpar.RENDER_STRETCHED, length_scale=8.0, material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(STEEL, 0.0)),
                    (0.40, rgba(STEEL_HOT, 0.8)),
                    (1.00, rgba(STEEL, 0.0))))
    return ParticleSystem(emitters=[
        draw_in,
        glint("Edge Flare", 0.7, lifetime=0.45, delay=0.22, spin=3.0),
        flash("Edge Glow", 0.5, STEEL, alpha=0.45, lifetime=0.3, delay=0.22),
        ring("Focus Ring", 0.2, 0.9, STEEL_HOT, alpha=0.5, lifetime=0.25, delay=0.22,
             horizontal=False),
    ])


def steady_hand_aura():
    """Idle at ``hand_r`` while the guaranteed crit is primed (up to 20 s, consumed by the
    next technique): a small cold glint that twinkles on the weapon hand. ~3 live."""
    twinkle = looping(e(
        name="Primed Twinkle",
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.06, 0.0, 0.0),
        warmup_time=1.0,
        min_start_size=0.16, max_start_size=0.24,
        min_angular_velocity=2.0, max_angular_velocity=4.0,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.4, 1.0), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(STEEL_HOT, 0.0)),
            (0.40, rgba(STEEL_HOT, 0.9)),
            (1.00, rgba(STEEL, 0.0)))),
        rate=2.2, lifetime=0.7)
    haze = looping(e(
        name="Primed Haze",
        warmup_time=1.0,
        min_start_size=0.3, max_start_size=0.36,
        size_over_life=float_curve((0.0, 0.9), (1.0, 1.1)),
        color_over_lifetime=color_curve(
            (0.00, rgba(STEEL, 0.0)),
            (0.50, rgba(STEEL, 0.22)),
            (1.00, rgba(STEEL, 0.0)))),
        rate=1.5, lifetime=1.2)
    return ParticleSystem(emitters=[haze, twinkle])


EFFECTS = [
    (evasive_step_burst, "EvasiveStepBurst.hpar"),
    (evasive_step_aura, "EvasiveStepAura.hpar"),
    (smoke_bomb, "SmokeBomb.hpar"),
    (smoke_mirrors_aura, "SmokeMirrorsAura.hpar"),
    (sprint_burst, "SprintBurst.hpar"),
    (sprint_aura, "SprintAura.hpar"),
    (fever_pitch_burst, "FeverPitchBurst.hpar"),
    (fever_pitch_aura, "FeverPitchAura.hpar"),
    (steady_hand_focus, "SteadyHandFocus.hpar"),
    (steady_hand_aura, "SteadyHandAura.hpar"),
]


def main():
    wanted = set(sys.argv[1:])
    for build, filename in EFFECTS:
        if not wanted or filename[:-5] in wanted:
            sc.write(build(), filename)


if __name__ == "__main__":
    main()
