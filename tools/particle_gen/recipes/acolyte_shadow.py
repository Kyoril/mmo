# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shadow-school acolyte effects. Run from the repo root::

    py -3.14 tools/particle_gen/recipes/acolyte_shadow.py

Writes into ``data/client/Particles/Acolyte/``. Read ``acolyte_common``'s docstring first:
every effect is a dark smoke body with a saturated violet rim.

Placement contract (decided by the spell visualization kits, not by this file):

========================  ============  ==========  =========================================
File                      Attach        Lifetime    Notes
========================  ============  ==========  =========================================
ShadowHandChannel         hand_r        looping     Shadowbolt cast bar; torn down at cast end
ShadowRelease             hand_r        one-shot    the bolt leaving the hand
ShadowboltTrail           projectile    looping     node flies at 20 u/s
ShadowboltImpact          spine_03      one-shot    origin = target torso
VeilBurst                 feet          one-shot    shadows wrap up the body
VeilAura                  feet          looping     30 min on every acolyte: sparsest loop here
UmbralBloom               feet          one-shot    8-unit PBAoE nova
GloamingProc              hand_r        one-shot    "next Shadowbolt is instant" proc
GloamingAura              hand_r        looping     while the proc is up (<= 10 s)
VoidResurgenceBurst       feet          one-shot    souls drawn into the caster after a kill
VoidResurgenceAura        feet          looping     15 s spirit buff
========================  ============  ==========  =========================================

Bone-attached emitters inherit the bone's rotation (``cleric_spell_visuals``): spawn velocity
is in hand space, so hand effects use symmetric velocity boxes, orbit and gravity, never a
"forward" direction.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acolyte_common as ac
from acolyte_common import (
    BEAM, GLOW, RING, SMOKE, STAR, UMBRA, VOID, VOID_DEEP, VOID_HOT, WISP, Burst,
    ParticleSystem, color_curve, e, fade, float_curve, hpar, lift, looping, rgba, write)


# =========================================================================================
# Shadowbolt
# =========================================================================================

def shadow_hand_channel():
    """Darkness gathering in the right hand: smoke and wisps spiral *inward* into a violet
    core (the opposite of a fire channel, which radiates out), with lilac motes sucked in."""
    core = looping(e(name="Channel Core",
                     min_start_size=0.34, max_start_size=0.44,
                     size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(VOID_HOT, 0.0)),
                         (0.30, rgba(VOID, 0.42)),
                         (0.70, rgba(VOID_DEEP, 0.30)),
                         (1.00, rgba(VOID_DEEP, 0.0)))), rate=14.0, lifetime=0.32)
    # Dark heart over the core: a smoke puff tumbling in place, so the centre reads as a
    # hole in the light rather than another glow.
    heart = looping(e(name="Channel Heart", material_name=SMOKE,
                      min_start_size=0.20, max_start_size=0.28,
                      min_angular_velocity=-3.0, max_angular_velocity=3.0,
                      size_over_life=float_curve((0.0, 0.7), (0.5, 1.0), (1.0, 0.8)),
                      color_over_lifetime=fade(UMBRA, 0.55, peak=0.3, hold=0.7)),
                    rate=10.0, lifetime=0.4)
    # Wisps spawn on a shell and fall inward (attractor at the hand), orbiting as they go.
    inward = looping(e(name="Channel Wisps",
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.42, 0.0, 0.0),
                       attractor_position=(0.0, 0.0, 0.0), attractor_strength=9.0, drag=2.5,
                       orbital_speed=5.0,
                       min_start_size=0.16, max_start_size=0.26,
                       min_angular_velocity=-4.0, max_angular_velocity=4.0,
                       material_name=WISP,
                       size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(VOID, 0.0)),
                           (0.30, rgba(VOID, 0.50)),
                           (0.80, rgba(VOID_HOT, 0.45)),
                           (1.00, rgba(VOID_HOT, 0.0)))), rate=22.0, lifetime=0.45)
    smoke = looping(e(name="Channel Smoke", material_name=SMOKE,
                      shape=hpar.SHAPE_SPHERE, shape_extents=(0.36, 0.0, 0.0),
                      attractor_position=(0.0, 0.0, 0.0), attractor_strength=6.0, drag=2.5,
                      orbital_speed=3.5,
                      min_start_size=0.18, max_start_size=0.28,
                      min_angular_velocity=-1.5, max_angular_velocity=1.5,
                      size_over_life=float_curve((0.0, 1.0), (1.0, 0.45)),
                      color_over_lifetime=fade(UMBRA, 0.40, peak=0.25, hold=0.7)),
                    rate=16.0, lifetime=0.5)
    motes = looping(e(name="Channel Motes", material_name=STAR,
                      shape=hpar.SHAPE_SPHERE, shape_extents=(0.5, 0.0, 0.0),
                      attractor_position=(0.0, 0.0, 0.0), attractor_strength=12.0, drag=3.0,
                      min_start_size=0.06, max_start_size=0.11,
                      min_angular_velocity=-6.0, max_angular_velocity=6.0,
                      size_over_life=float_curve((0.0, 0.4), (0.4, 1.0), (1.0, 0.2)),
                      color_over_lifetime=fade(VOID_HOT, 0.9, peak=0.3, end=VOID)),
                    rate=12.0, lifetime=0.4)
    return ParticleSystem(emitters=[smoke, core, inward, heart, motes])


def shadow_release():
    """The bolt leaving the hand: a tight violet pop, a puff of dark smoke and a few streaks."""
    return ParticleSystem(emitters=[
        ac.flash("Release Flash", 0.65, VOID, alpha=0.55, lifetime=0.16),
        ac.dark_smoke("Release Smoke", 6, 0.08, 0.32, 0.45, alpha=0.40, speed=0.9),
        ac.streaks("Release Streaks", 10, 3.0, 0.07, 0.25, alpha=0.75, length=4.0),
        ac.ring("Release Ring", 0.25, 1.1, VOID, alpha=0.45, lifetime=0.22, horizontal=False),
    ])


def shadowbolt_trail():
    """The flying bolt: a dark roiling orb with a lilac heart and a violet rim, shedding a
    wake of smoke, a ribbon of wisps and falling lilac motes. ~5 units of wake at 20 u/s."""
    centre = looping(e(name="Bolt Heart",
                       min_start_size=0.24, max_start_size=0.30,
                       size_over_life=float_curve((0.0, 0.85), (0.5, 1.0), (1.0, 0.9)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(VOID_HOT, 0.0)),
                           (0.25, rgba(VOID_HOT, 0.70)),
                           (0.75, rgba(VOID, 0.55)),
                           (1.00, rgba(VOID, 0.0)))), rate=24.0, lifetime=0.16)
    rim = looping(e(name="Bolt Rim",
                    shape=hpar.SHAPE_SPHERE, shape_extents=(0.06, 0.0, 0.0),
                    min_start_size=0.55, max_start_size=0.70,
                    min_angular_velocity=-3.0, max_angular_velocity=3.0,
                    size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.9)),
                    color_over_lifetime=fade(VOID, 0.42, peak=0.25, end=VOID_DEEP, hold=0.75)),
                  rate=30.0, lifetime=0.2)
    roil = looping(e(name="Bolt Roil", material_name=SMOKE,
                     shape=hpar.SHAPE_SPHERE, shape_extents=(0.14, 0.0, 0.0),
                     min_velocity=(-0.4, -0.4, -0.4), max_velocity=(0.4, 0.4, 0.4),
                     orbital_speed=8.0, noise_amplitude=2.5, noise_frequency=3.0,
                     min_start_size=0.26, max_start_size=0.38,
                     min_angular_velocity=-4.0, max_angular_velocity=4.0,
                     size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.6)),
                     color_over_lifetime=fade(UMBRA, 0.50, peak=0.15, hold=0.6)),
                   rate=40.0, lifetime=0.22)
    wake = looping(e(name="Bolt Wake Smoke", simulation_space=hpar.SIM_WORLD,
                     material_name=SMOKE,
                     shape=hpar.SHAPE_SPHERE, shape_extents=(0.16, 0.0, 0.0),
                     min_velocity=(-0.5, -0.2, -0.5), max_velocity=(0.5, 0.6, 0.5),
                     min_start_size=0.40, max_start_size=0.62,
                     min_angular_velocity=-2.0, max_angular_velocity=2.0,
                     gravity=(0.0, 0.4, 0.0), drag=1.5, noise_amplitude=1.5,
                     size_over_life=float_curve((0.0, 1.0), (0.4, 0.95), (1.0, 0.4)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(VOID, 0.0)),
                         (0.06, rgba(VOID, 0.40)),
                         (0.30, rgba(UMBRA, 0.42)),
                         (1.00, rgba(UMBRA, 0.0)))), rate=80.0, lifetime=0.32)
    # Streaks inherit part of the flight velocity so the wake does not bead at low frame
    # rates (see mage_fire._wake_streaks); they fade in late so none pokes out ahead.
    streak = looping(e(name="Bolt Streaks", simulation_space=hpar.SIM_WORLD,
                       inherit_velocity=0.35,
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
                       min_velocity=(-0.3, -0.1, -0.3), max_velocity=(0.3, 0.3, 0.3),
                       min_start_size=0.20, max_start_size=0.30, drag=5.0,
                       render_mode=hpar.RENDER_STRETCHED, length_scale=3.5, material_name=BEAM,
                       size_over_life=float_curve((0.0, 1.0), (1.0, 0.45)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(VOID_HOT, 0.0)),
                           (0.25, rgba(VOID_HOT, 0.40)),
                           (0.50, rgba(VOID, 0.34)),
                           (1.00, rgba(VOID_DEEP, 0.0)))), rate=50.0, lifetime=0.24)
    spirits = looping(e(name="Bolt Spirits", simulation_space=hpar.SIM_WORLD,
                        material_name=WISP,
                        shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
                        min_velocity=(-0.9, -0.3, -0.9), max_velocity=(0.9, 0.9, 0.9),
                        min_start_size=0.22, max_start_size=0.36,
                        min_angular_velocity=-3.5, max_angular_velocity=3.5,
                        drag=1.5,
                        size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.1)),
                        color_over_lifetime=fade(VOID, 0.50, peak=0.15, end=VOID_DEEP, hold=0.5)),
                      rate=24.0, lifetime=0.42)
    motes = looping(e(name="Bolt Motes", simulation_space=hpar.SIM_WORLD,
                      material_name=STAR,
                      shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
                      min_velocity=(-0.8, -0.4, -0.8), max_velocity=(0.8, 0.6, 0.8),
                      min_start_size=0.06, max_start_size=0.10,
                      min_angular_velocity=-6.0, max_angular_velocity=6.0,
                      gravity=(0.0, -1.2, 0.0), drag=1.0,
                      size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                      color_over_lifetime=fade(VOID_HOT, 0.95, peak=0.08, end=VOID)),
                    rate=26.0, lifetime=0.5)
    return ParticleSystem(emitters=[wake, streak, spirits, motes, rim, roil, centre])


def shadowbolt_impact():
    """The bolt detonating on the chest: a violet flash swallowed by a dark smoke burst,
    shards and wisps flung out, a ring snapping open, lilac motes drifting down."""
    return ParticleSystem(emitters=[
        ac.flash("Impact Flash", 1.1, VOID_HOT, alpha=0.65, lifetime=0.14),
        ac.flash("Impact Bloom", 1.6, VOID, alpha=0.40, lifetime=0.30),
        ac.ring("Impact Ring", 0.4, 2.4, VOID, alpha=0.55, lifetime=0.28, horizontal=False),
        ac.dark_smoke("Impact Smoke", 12, 0.18, 0.62, 0.85, alpha=0.45, speed=2.2, rise=0.5,
                      delay=0.03),
        ac.streaks("Impact Shards", 20, 7.0, 0.10, 0.32, alpha=0.9, length=5.0),
        ac.wisps("Impact Wisps", 9, 3.2, 0.42, 0.65, alpha=0.6, drag=3.0, rise=0.6),
        ac.stars("Impact Motes", 10, 0.16, 0.9, alpha=0.9, speed=1.6, rise=0.2,
                 delay=0.06),
    ])


# =========================================================================================
# Veil of Shadows (30-minute self buff)
# =========================================================================================

def veil_burst():
    """Shadows wrap up the body: wisps spiral from the ground to the shoulders, a dark ring
    of smoke at the feet, a few lilac glints at the top as the veil closes."""
    spiral = e(name="Veil Spiral", simulation_space=hpar.SIM_LOCAL, material_name=WISP,
               duration=0.45, spawn_rate=60.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.75),
               min_velocity=(0.0, 1.8, 0.0), max_velocity=(0.0, 3.0, 0.0),
               orbital_speed=6.5, attractor_position=(0.0, 1.9, 0.0), attractor_strength=3.0,
               drag=0.8,
               min_lifetime=0.55, max_lifetime=0.85,
               min_start_size=0.30, max_start_size=0.48,
               min_angular_velocity=-2.5, max_angular_velocity=2.5,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.7)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(VOID, 0.0)),
                   (0.15, rgba(VOID, 0.55)),
                   (0.60, rgba(VOID_DEEP, 0.45)),
                   (1.00, rgba(VOID_DEEP, 0.0))))
    shroud = e(name="Veil Shroud", simulation_space=hpar.SIM_LOCAL, material_name=SMOKE,
               duration=0.35, spawn_rate=40.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.55),
               min_velocity=(0.0, 1.2, 0.0), max_velocity=(0.0, 2.2, 0.0),
               orbital_speed=4.5, drag=1.2,
               min_lifetime=0.6, max_lifetime=0.9,
               min_start_size=0.45, max_start_size=0.70,
               min_angular_velocity=-1.0, max_angular_velocity=1.0,
               size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.3)),
               color_over_lifetime=fade(UMBRA, 0.42, peak=0.15, hold=0.55))
    glints = ac.stars("Veil Glints", 8, 0.22, 0.6, alpha=0.95, radius=0.35, speed=0.4,
                      delay=0.35, space=hpar.SIM_LOCAL)
    glints = lift(glints, 1.5, drag=4.0, hidden=0.25, spread=0.15)
    return ParticleSystem(emitters=[
        ac.ring("Veil Ground Ring", 2.6, 0.8, VOID, alpha=0.5, lifetime=0.5),
        ac.dark_smoke("Veil Ground Smoke", 10, 0.5, 0.7, 0.8, alpha=0.40, speed=0.6, rise=0.2),
        shroud, spiral, glints,
    ])


def veil_aura():
    """The veil while it lasts: a few dark wisps slipping off the body and a rare lilac
    glint. Runs for 30 minutes on every acolyte in view, so ~10 live particles, no more."""
    drift = looping(e(name="Veil Wisps", simulation_space=hpar.SIM_WORLD, material_name=WISP,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.6, 0.30),
                      min_velocity=(-0.15, 0.25, -0.15), max_velocity=(0.15, 0.55, 0.15),
                      min_start_size=0.26, max_start_size=0.40,
                      min_angular_velocity=-1.2, max_angular_velocity=1.2,
                      noise_amplitude=0.6, noise_frequency=1.0,
                      size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.2)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(VOID_DEEP, 0.0)),
                          (0.25, rgba(VOID_DEEP, 0.38)),
                          (0.65, rgba(UMBRA, 0.30)),
                          (1.00, rgba(UMBRA, 0.0)))), rate=5.0, lifetime=1.4)
    glint = looping(e(name="Veil Glint", simulation_space=hpar.SIM_WORLD, material_name=STAR,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.7, 0.32),
                      min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.3, 0.0),
                      min_start_size=0.07, max_start_size=0.12,
                      min_angular_velocity=-4.0, max_angular_velocity=4.0,
                      size_over_life=float_curve((0.0, 0.2), (0.3, 1.0), (1.0, 0.2)),
                      color_over_lifetime=fade(VOID_HOT, 0.85, peak=0.3, end=VOID)),
                    rate=1.6, lifetime=0.8)
    return ParticleSystem(emitters=[drift, glint])


# =========================================================================================
# Umbral Bloom (8-unit PBAoE)
# =========================================================================================

def umbral_bloom():
    """A flower of void opening around the caster: the sigil flares under the feet, a dark
    ring rolls out to the 8-unit edge, wisp 'petals' race outward along the ground and dark
    smoke billows after them; a short column of violet streaks marks the centre."""
    # Ring sizes: the ring sprite's band sits at ~62% of the quad half-size, so a radius-8
    # edge needs a quad of ~26 (mage_spell_visuals).
    edge = ac.ring("Bloom Edge", 2.0, 26.0, VOID, alpha=0.42, lifetime=0.7, hold=0.6)
    edge2 = ac.ring("Bloom Edge Dark", 1.6, 22.0, UMBRA, alpha=0.45, lifetime=0.75, delay=0.08,
                    hold=0.6)
    petals = e(name="Bloom Petals", simulation_space=hpar.SIM_WORLD, material_name=WISP,
               bursts=[Burst(0.0, 28), Burst(0.08, 16)],
               shape=hpar.SHAPE_BOX, shape_extents=(1.0, 0.25, 1.0),
               min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.5, 0.0),
               radial_acceleration=90.0, drag=5.0,
               min_lifetime=0.5, max_lifetime=0.8,
               min_start_size=0.45, max_start_size=0.75,
               min_angular_velocity=-3.0, max_angular_velocity=3.0,
               size_over_life=float_curve((0.0, 0.5), (0.25, 1.0), (1.0, 1.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(VOID_HOT, 0.0)),
                   (0.08, rgba(VOID_HOT, 0.7)),
                   (0.40, rgba(VOID, 0.55)),
                   (1.00, rgba(VOID_DEEP, 0.0))))
    billow = e(name="Bloom Smoke", simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
               bursts=[Burst(0.04, 22)],
               shape=hpar.SHAPE_BOX, shape_extents=(1.2, 0.3, 1.2),
               min_velocity=(0.0, 0.2, 0.0), max_velocity=(0.0, 0.8, 0.0),
               radial_acceleration=55.0, drag=4.0,
               min_lifetime=0.7, max_lifetime=1.1,
               min_start_size=0.9, max_start_size=1.4,
               min_angular_velocity=-1.0, max_angular_velocity=1.0,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.6)),
               color_over_lifetime=fade(UMBRA, 0.45, peak=0.12, hold=0.5))
    column = ac.streaks("Bloom Column", 22, 1.0, 0.24, 0.5, alpha=0.8, radius=0.5,
                        drag=1.5, length=7.0, up=1.0)
    column.min_velocity = (-0.3, 4.0, -0.3)
    column.max_velocity = (0.3, 7.0, 0.3)
    glitter = e(name="Bloom Glitter", simulation_space=hpar.SIM_WORLD, material_name=STAR,
                duration=0.45, spawn_rate=70.0, start_delay=0.1,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 7.0),
                min_velocity=(-0.2, 0.5, -0.2), max_velocity=(0.2, 1.4, 0.2),
                min_lifetime=0.5, max_lifetime=0.8,
                min_start_size=0.22, max_start_size=0.40,
                min_angular_velocity=-3.0, max_angular_velocity=3.0,
                size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.3)),
                color_over_lifetime=fade(VOID_HOT, 0.85, peak=0.15, end=VOID))
    return ParticleSystem(emitters=[
        ac.sigil("Bloom Sigil", 5.0, VOID, alpha=0.7, lifetime=0.9, spin=1.2, grow=1.5),
        ac.flash("Bloom Flash", 2.6, VOID, alpha=0.5, lifetime=0.25),
        edge2, edge, billow, petals, column, glitter,
    ])


# =========================================================================================
# Gloaming Trance (proc: next Shadowbolt instant)
# =========================================================================================

def gloaming_proc():
    """The proc lighting the hand: a violet flare, a ring snapping out, wisps flicking off
    and lilac stars -- small, it fires mid-rotation."""
    return ParticleSystem(emitters=[
        ac.flash("Proc Flash", 0.8, VOID_HOT, alpha=0.6, lifetime=0.18),
        ac.ring("Proc Ring", 0.3, 1.4, VOID, alpha=0.55, lifetime=0.3, horizontal=False),
        ac.wisps("Proc Wisps", 8, 1.6, 0.26, 0.55, alpha=0.6, radius=0.08, drag=3.0,
                 space=hpar.SIM_LOCAL),
        ac.stars("Proc Stars", 7, 0.16, 0.5, alpha=0.95, radius=0.12, speed=1.2,
                 space=hpar.SIM_LOCAL),
    ])


def gloaming_aura():
    """While the proc waits: the hand smoulders with violet -- a flickering glow, dark
    tendrils licking upward and the odd star."""
    glow = looping(e(name="Trance Glow",
                     min_start_size=0.30, max_start_size=0.40,
                     size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
                     color_over_lifetime=fade(VOID, 0.32, peak=0.3, hold=0.7)),
                   rate=8.0, lifetime=0.45)
    tendrils = looping(e(name="Trance Tendrils", simulation_space=hpar.SIM_WORLD,
                         material_name=WISP,
                         shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
                         min_velocity=(-0.15, 0.4, -0.15), max_velocity=(0.15, 0.9, 0.15),
                         min_start_size=0.14, max_start_size=0.22,
                         min_angular_velocity=-2.5, max_angular_velocity=2.5,
                         noise_amplitude=1.0, noise_frequency=2.0, drag=0.8,
                         size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.1)),
                         color_over_lifetime=fade(VOID, 0.5, peak=0.15, end=VOID_DEEP, hold=0.5)),
                       rate=9.0, lifetime=0.6)
    sparkle = looping(e(name="Trance Stars", simulation_space=hpar.SIM_WORLD, material_name=STAR,
                        shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
                        min_velocity=(-0.1, 0.1, -0.1), max_velocity=(0.1, 0.4, 0.1),
                        min_start_size=0.06, max_start_size=0.10,
                        min_angular_velocity=-5.0, max_angular_velocity=5.0,
                        size_over_life=float_curve((0.0, 0.2), (0.3, 1.0), (1.0, 0.2)),
                        color_over_lifetime=fade(VOID_HOT, 0.9, peak=0.25, end=VOID)),
                      rate=4.0, lifetime=0.5)
    return ParticleSystem(emitters=[glow, tendrils, sparkle])


# =========================================================================================
# Void Resurgence (after a kill)
# =========================================================================================

def void_resurgence_burst():
    """The fallen enemy's essence drawn into the caster: wisps swirl in from a wide circle
    and are swallowed at the chest, a dark flash and a burst of lilac stars."""
    souls = e(name="Souls", simulation_space=hpar.SIM_LOCAL, material_name=WISP,
              bursts=[Burst(0.0, 10), Burst(0.15, 8)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.4, 2.4),
              attractor_position=(0.0, 1.2, 0.0), attractor_strength=16.0, drag=2.6,
              orbital_speed=4.0,
              min_lifetime=0.6, max_lifetime=0.8,
              min_start_size=0.34, max_start_size=0.50,
              min_angular_velocity=-2.0, max_angular_velocity=2.0,
              size_over_life=float_curve((0.0, 1.0), (0.7, 0.8), (1.0, 0.2)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(VOID_HOT, 0.0)),
                  (0.20, rgba(VOID_HOT, 0.6)),
                  (0.75, rgba(VOID, 0.5)),
                  (1.00, rgba(VOID, 0.0))))
    swallow = lift(ac.flash("Swallow Flash", 1.3, VOID, alpha=0.5, lifetime=0.3, delay=0.55),
                   1.2, hidden=0.0)
    swallow.min_velocity = (0.0, 0.0, 0.0)
    swallow.max_velocity = (0.0, 0.0, 0.0)
    swallow.drag = 0.0
    # The flash is lifted with a strong attractor instead: a point emitter has no height.
    swallow.attractor_position = (0.0, 1.2, 0.0)
    swallow.attractor_strength = 400.0
    swallow.drag = 12.0
    burst = ac.stars("Resurgence Stars", 14, 0.22, 0.7, alpha=0.9, radius=0.3, speed=2.2,
                     rise=1.0, delay=0.6, space=hpar.SIM_LOCAL)
    burst = lift(burst, 1.2, drag=4.0, hidden=0.0, spread=0.2)
    return ParticleSystem(emitters=[
        ac.ring("Resurgence Ring", 3.6, 0.8, VOID, alpha=0.45, lifetime=0.7),
        souls, swallow, burst,
    ])


def void_resurgence_aura():
    """15 s of borrowed spirit: lilac motes rising slowly off the body, an occasional wisp."""
    motes = looping(e(name="Resurgence Motes", simulation_space=hpar.SIM_WORLD, material_name=STAR,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.5, 0.35),
                      min_velocity=(-0.1, 0.4, -0.1), max_velocity=(0.1, 0.8, 0.1),
                      min_start_size=0.06, max_start_size=0.11,
                      min_angular_velocity=-4.0, max_angular_velocity=4.0,
                      size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.2)),
                      color_over_lifetime=fade(VOID_HOT, 0.85, peak=0.2, end=VOID)),
                    rate=6.0, lifetime=1.0)
    wisp = looping(e(name="Resurgence Wisps", simulation_space=hpar.SIM_WORLD, material_name=WISP,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.4, 0.30),
                     min_velocity=(-0.1, 0.5, -0.1), max_velocity=(0.1, 0.9, 0.1),
                     min_start_size=0.20, max_start_size=0.30,
                     min_angular_velocity=-1.5, max_angular_velocity=1.5,
                     size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.1)),
                     color_over_lifetime=fade(VOID, 0.40, peak=0.2, end=VOID_DEEP)),
                   rate=2.5, lifetime=1.1)
    return ParticleSystem(emitters=[wisp, motes])


EFFECTS = {
    "ShadowHandChannel.hpar": shadow_hand_channel,
    "ShadowRelease.hpar": shadow_release,
    "ShadowboltTrail.hpar": shadowbolt_trail,
    "ShadowboltImpact.hpar": shadowbolt_impact,
    "VeilBurst.hpar": veil_burst,
    "VeilAura.hpar": veil_aura,
    "UmbralBloom.hpar": umbral_bloom,
    "GloamingProc.hpar": gloaming_proc,
    "GloamingAura.hpar": gloaming_aura,
    "VoidResurgenceBurst.hpar": void_resurgence_burst,
    "VoidResurgenceAura.hpar": void_resurgence_aura,
}


def main():
    wanted = set(sys.argv[1:])
    for filename, build in EFFECTS.items():
        if not wanted or filename in wanted or filename[:-5] in wanted:
            write(build(), filename)


if __name__ == "__main__":
    main()
