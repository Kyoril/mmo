# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Effects for the acolyte kit overhaul (debuffs, drain, summons, rot field, pet buffs). Run from
the repo root::

    py -3.14 tools/particle_gen/recipes/acolyte_kit.py

Writes into ``data/client/Particles/Acolyte/``. Read ``acolyte_common``'s docstring first.

========================  ===========  ==========  =========================================
File                      Attach       Lifetime    Notes
========================  ===========  ==========  =========================================
FrailtyApply              spine_03     one-shot    Frailty lands on the target
FrailtyAura               feet         looping     grey-violet motes sagging off the shoulders
DrainChannel              hand_r       looping     drain cast bar
DrainBeamMotes            impact       one-shot    each tick, on the target: motes drift up/away
MireApply                 feet         one-shot    tar rises round the legs
MireAura                  feet         looping     tar tendrils at the feet, crimson rim
PlagueApply               spine_03     one-shot    spore burst
PlagueAura                feet         looping     green spores
GloomlingSummon           caster       one-shot    two small violet portals + smoke puff
RotFieldGround            ground       looping     GROUND_ACTIVE, radius 8, loop for the field
RotFieldTick              spine_03     one-shot    small green splash per tick on a unit
TongueAura                feet         looping     violet runes/wisps circling the head
WailBurst                 caster       one-shot    crimson shockwave and skulls
EmpowerAura               feet         looping     violet flames on the pet
BroodSummon               caster       one-shot    big void sigil + smoke pillar
RootTendrils              feet         one-shot    ~2 s crimson-black tendrils gripping feet
HealingBlight             feet         looping     sickly red-grey motes on the cursed target
SharedNightHeal           spine_03     one-shot    violet motes converging on the pet
========================  ===========  ==========  =========================================
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acolyte_common as ac
from acolyte_common import (
    BEAM, BLIGHT, BLIGHT_DEEP, BLIGHT_HOT, DREAD, DREAD_DEEP, DREAD_HOT, GLOW, MIASMA, RING, SKULL,
    SMOKE, STAR, UMBRA, VOID, VOID_DEEP, VOID_HOT, WISP, Burst, ParticleSystem,
    color_curve, e, fade, float_curve, hpar, lift, looping, rgba, write)

ASH = (0.55, 0.50, 0.58)          # sickly grey-violet
TAR = (0.05, 0.03, 0.05)          # tar black
BLOODGREY = (0.62, 0.40, 0.44)    # sickly red-grey
BLOODGREY_DEEP = (0.30, 0.16, 0.20)


def _motes(name, rate, lifetime, colour, hot, alpha=0.55, size=(0.06, 0.11), material=STAR,
           **kw):
    """A looping trickle of small motes; pass shape/velocity keywords through."""
    return looping(e(name=name, simulation_space=hpar.SIM_WORLD, material_name=material,
                     min_start_size=size[0], max_start_size=size[1],
                     min_angular_velocity=-3.0, max_angular_velocity=3.0,
                     size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.4)),
                     color_over_lifetime=fade(hot, alpha, peak=0.15, end=colour), **kw),
                   rate=rate, lifetime=lifetime)


def _body_cone(height=1.6, radius=0.30):
    return dict(shape=hpar.SHAPE_CONE, shape_extents=(0.0, height, radius))


def _smoke_loop(name, rate, lifetime, colour, rim, alpha, size=(0.28, 0.44), vel=(0.25, 0.55),
                **kw):
    return looping(e(name=name, simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
                     min_velocity=(-0.1, vel[0], -0.1), max_velocity=(0.1, vel[1], 0.1),
                     min_start_size=size[0], max_start_size=size[1],
                     min_angular_velocity=-1.0, max_angular_velocity=1.0,
                     noise_amplitude=0.5, noise_frequency=1.0,
                     size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(rim, 0.0)),
                         (0.20, rgba(rim, alpha)),
                         (0.60, rgba(colour, alpha * 0.85)),
                         (1.00, rgba(colour, 0.0))), **kw),
                   rate=rate, lifetime=lifetime)


def _tendrils(name, count, radius, height, lifetime, colour, rim, alpha=0.55, delay=0.0,
              loop_rate=None):
    """Wisps that writhe up from a ring round the feet."""
    em = e(name=name, simulation_space=hpar.SIM_LOCAL, material_name=WISP, start_delay=delay,
           bursts=[Burst(0.0, count)],
           shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.05, radius),
           min_velocity=(-0.15, height / lifetime * 0.7, -0.15),
           max_velocity=(0.15, height / lifetime * 1.2, 0.15),
           orbital_speed=1.6, noise_amplitude=0.7, noise_frequency=1.6,
           min_lifetime=lifetime * 0.7, max_lifetime=lifetime,
           min_start_size=0.28, max_start_size=0.46,
           min_angular_velocity=-1.5, max_angular_velocity=1.5,
           size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.8)),
           color_over_lifetime=color_curve(
               (0.00, rgba(colour, 0.0)),
               (0.15, rgba(colour, alpha)),
               (0.55, rgba(rim, alpha * 0.9)),
               (1.00, rgba(colour, 0.0))))
    if loop_rate:
        looping(em, loop_rate, lifetime)
    return em


# =========================================================================================
# Frailty
# =========================================================================================

def frailty_apply():
    return ParticleSystem(emitters=[
        ac.flash("Frailty Flash", 0.9, VOID, alpha=0.4, lifetime=0.18),
        ac.dark_smoke("Frailty Smoke", 9, 0.2, 0.55, 0.9, alpha=0.42, speed=0.9, rise=-0.2),
        ac.wisps("Frailty Wisps", 7, 1.2, 0.34, 0.8, hot=ASH, colour=VOID_DEEP, alpha=0.5,
                 drag=2.5, rise=-0.4, gravity=-0.8),
        ac.ring("Frailty Ring", 0.3, 1.5, VOID, alpha=0.4, lifetime=0.3, horizontal=False),
    ])


def frailty_aura():
    """Grey-violet motes drooping off the shoulders: spawned high, falling slowly."""
    sag = looping(e(name="Sagging Motes", simulation_space=hpar.SIM_WORLD, material_name=WISP,
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.55, 0.34),
                    min_velocity=(-0.05, -0.55, -0.05), max_velocity=(0.05, -0.25, 0.05),
                    min_start_size=0.18, max_start_size=0.28,
                    min_angular_velocity=-1.5, max_angular_velocity=1.5,
                    gravity=(0.0, -0.3, 0.0),
                    size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.1)),
                    color_over_lifetime=fade(VOID, 0.45, peak=0.2, end=ASH)),
                  rate=4.0, lifetime=1.2)
    smoke = _smoke_loop("Frailty Weight", 5.0, 1.2, UMBRA, ASH, 0.34, vel=(-0.2, 0.05),
                        **_body_cone(1.7, 0.32))
    # Lit ash flakes drifting down from the shoulders: the dark motes alone vanish against a
    # dark target, these keep the curse readable at range.
    # Origin is the feet: lift to the shoulders, then gravity lets them sag down the body.
    ash = looping(e(name="Frailty Ash", material_name=STAR,
                    shape=hpar.SHAPE_SPHERE, shape_extents=(0.34, 0.0, 0.0),
                    min_start_size=0.10, max_start_size=0.16,
                    min_angular_velocity=-2.0, max_angular_velocity=2.0,
                    gravity=(0.0, -0.9, 0.0),
                    size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.5)),
                    color_over_lifetime=fade(ASH, 0.85, peak=0.2, end=VOID)),
                  rate=12.0, lifetime=1.8)
    lift(ash, 1.45, drag=5.0, hidden=0.2, spread=0.06)
    return ParticleSystem(emitters=[smoke, sag, ash])


# =========================================================================================
# Drain
# =========================================================================================

def drain_channel():
    """Siphon in the hand: violet-black swirl, inward wisps and a few green-tinged motes
    (stolen life) being pulled out of the hand toward the target."""
    core = looping(e(name="Drain Core",
                     min_start_size=0.30, max_start_size=0.40,
                     size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
                     color_over_lifetime=fade(VOID, 0.36, peak=0.3, end=VOID_DEEP, hold=0.7)),
                   rate=12.0, lifetime=0.33)
    heart = looping(e(name="Drain Heart", material_name=SMOKE,
                      min_start_size=0.20, max_start_size=0.26,
                      min_angular_velocity=-3.0, max_angular_velocity=3.0,
                      size_over_life=float_curve((0.0, 0.7), (0.5, 1.0), (1.0, 0.8)),
                      color_over_lifetime=fade(UMBRA, 0.55, peak=0.3, hold=0.7)),
                    rate=10.0, lifetime=0.4)
    inward = looping(e(name="Drain Wisps", material_name=WISP,
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.45, 0.0, 0.0),
                       attractor_position=(0.0, 0.0, 0.0), attractor_strength=9.0, drag=2.5,
                       orbital_speed=5.0,
                       min_start_size=0.16, max_start_size=0.26,
                       min_angular_velocity=-4.0, max_angular_velocity=4.0,
                       size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(VOID_DEEP, 0.0)),
                           (0.30, rgba(VOID, 0.50)),
                           (0.80, rgba(VOID_HOT, 0.45)),
                           (1.00, rgba(VOID_HOT, 0.0)))), rate=18.0, lifetime=0.45)
    life = looping(e(name="Drain Life Motes", material_name=STAR,
                     shape=hpar.SHAPE_SPHERE, shape_extents=(0.35, 0.0, 0.0),
                     attractor_position=(0.0, 0.0, 0.0), attractor_strength=11.0, drag=3.0,
                     min_start_size=0.05, max_start_size=0.09,
                     min_angular_velocity=-4.0, max_angular_velocity=4.0,
                     size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 0.2)),
                     color_over_lifetime=fade(BLIGHT_HOT, 0.8, peak=0.2, end=BLIGHT)),
                   rate=6.0, lifetime=0.4)
    return ParticleSystem(emitters=[core, inward, heart, life])


def drain_beam_motes():
    """Plays on the target at each tick: life motes lift off the body and drift upward and
    away (the stream toward the caster is implied by the drift)."""
    motes = e(name="Siphoned Motes", simulation_space=hpar.SIM_WORLD, material_name=STAR,
              bursts=[Burst(0.0, 8)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.25, 0.0, 0.0),
              min_velocity=(-0.3, 0.8, -0.3), max_velocity=(0.3, 1.8, 0.3),
              drag=1.0, orbital_speed=2.0,
              min_lifetime=0.4, max_lifetime=0.6,
              min_start_size=0.07, max_start_size=0.13,
              min_angular_velocity=-5.0, max_angular_velocity=5.0,
              size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.2)),
              color_over_lifetime=fade(VOID_HOT, 0.85, peak=0.15, end=VOID))
    wisps = ac.wisps("Siphon Wisps", 3, 1.0, 0.26, 0.5, hot=VOID_HOT, colour=VOID, alpha=0.5,
                     radius=0.2, drag=1.5, rise=1.6)
    return ParticleSystem(emitters=[
        ac.flash("Siphon Flash", 0.5, VOID, alpha=0.3, lifetime=0.14), motes, wisps])


# =========================================================================================
# Mire (root/slow, tar)
# =========================================================================================

def mire_apply():
    return ParticleSystem(emitters=[
        ac.dark_smoke("Mire Splash", 12, 0.35, 0.6, 0.9, colour=TAR, alpha=0.5, speed=1.4,
                      rise=0.4, drag=3.0),
        _tendrils("Mire Tendrils", 8, 0.4, 0.9, 0.8, TAR, DREAD, alpha=0.55),
        ac.ring("Mire Ring", 0.5, 2.2, DREAD, alpha=0.35, lifetime=0.4),
        ac.flash("Mire Flash", 0.8, DREAD_DEEP, alpha=0.4, lifetime=0.18),
    ])


def mire_aura():
    """Tar-black tendrils clinging round the feet with a thin crimson rim."""
    tendrils = _tendrils("Tar Tendrils", 1, 0.32, 0.55, 0.9, TAR, DREAD, alpha=0.75,
                         loop_rate=10.0)
    tendrils.simulation_space = hpar.SIM_WORLD
    smoke = _smoke_loop("Tar Smoke", 5.0, 0.9, TAR, DREAD_DEEP, 0.42, size=(0.30, 0.44),
                        vel=(0.1, 0.35), shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.1, 0.32))
    drips = _motes("Crimson Drips", 2.5, 0.8, DREAD_DEEP, DREAD, alpha=0.7,
                   shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.1, 0.3),
                   min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 0.7, 0.0),
                   size=(0.05, 0.08), material=GLOW)
    return ParticleSystem(emitters=[smoke, tendrils, drips])


# =========================================================================================
# Plague
# =========================================================================================

def plague_apply():
    spores = e(name="Spore Burst", simulation_space=hpar.SIM_WORLD, material_name=GLOW,
               bursts=[Burst(0.0, 22)],
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
               min_velocity=(-0.5, 0.0, -0.5), max_velocity=(0.5, 1.0, 0.5),
               min_start_speed=0.8, max_start_speed=2.6, drag=2.0,
               min_lifetime=0.5, max_lifetime=1.0,
               min_start_size=0.06, max_start_size=0.12,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
               color_over_lifetime=fade(BLIGHT_HOT, 0.85, peak=0.1, end=BLIGHT))
    return ParticleSystem(emitters=[
        ac.flash("Plague Flash", 0.9, BLIGHT, alpha=0.42, lifetime=0.18),
        ac.dark_smoke("Plague Cloud", 10, 0.2, 0.6, 1.0, colour=MIASMA, alpha=0.45, speed=1.4,
                      rise=0.5),
        spores,
        ac.wisps("Plague Wisps", 6, 2.0, 0.34, 0.7, hot=BLIGHT_HOT, colour=BLIGHT, alpha=0.5,
                 drag=3.0, rise=0.7),
    ])


def plague_aura():
    miasma = _smoke_loop("Plague Haze", 6.0, 1.3, MIASMA, BLIGHT_DEEP, 0.32,
                         **_body_cone(1.5, 0.30))
    spores = _motes("Plague Spores", 9.0, 1.1, BLIGHT, BLIGHT_HOT, alpha=0.75, material=GLOW,
                    size=(0.04, 0.08), noise_amplitude=0.9, noise_frequency=1.8,
                    min_velocity=(-0.1, 0.2, -0.1), max_velocity=(0.1, 0.6, 0.1),
                    **_body_cone(1.6, 0.34))
    return ParticleSystem(emitters=[miasma, spores])


# =========================================================================================
# Summons
# =========================================================================================

def _portal(name, size, colour, rim, delay=0.0, lifetime=1.0):
    """A small upright ring portal: a ring sprite that swells and a swirling disc behind."""
    return [
        ac.ring(name + " Ring", size * 0.4, size, rim, alpha=0.5, lifetime=lifetime,
                delay=delay, horizontal=False, hold=0.7),
        e(name=name + " Swirl", start_delay=delay, bursts=[Burst(0.0, 1)], material_name=SMOKE,
          min_lifetime=lifetime, max_lifetime=lifetime,
          min_start_size=size * 0.8, max_start_size=size * 0.8,
          min_angular_velocity=1.8, max_angular_velocity=1.8,
          size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.9)),
          color_over_lifetime=fade(colour, 0.5, peak=0.2, hold=0.6)),
    ]


def gloomling_summon():
    """Two small violet portals with a smoke puff."""
    emitters = []
    emitters += _portal("Portal A", 1.1, UMBRA, VOID, delay=0.0)
    emitters += _portal("Portal B", 1.1, UMBRA, VOID, delay=0.12)
    emitters += [
        ac.dark_smoke("Summon Puff", 10, 0.3, 0.6, 0.9, alpha=0.42, speed=1.2, rise=0.5),
        ac.wisps("Summon Wisps", 8, 1.6, 0.32, 0.8, alpha=0.5, drag=2.5, rise=0.8),
        ac.stars("Summon Stars", 8, 0.1, 0.7, radius=0.4, speed=0.6),
        ac.flash("Summon Flash", 0.9, VOID, alpha=0.35, lifetime=0.2),
    ]
    return ParticleSystem(emitters=emitters)


def brood_summon():
    """Large void sigil burned into the ground and a pillar of smoke with violet streaks."""
    pillar = e(name="Brood Pillar", simulation_space=hpar.SIM_LOCAL, material_name=SMOKE,
               duration=0.5, spawn_rate=45.0, start_delay=0.1,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.7),
               min_velocity=(0.0, 1.8, 0.0), max_velocity=(0.0, 3.4, 0.0),
               orbital_speed=2.5, drag=1.0,
               min_lifetime=0.8, max_lifetime=1.1,
               min_start_size=0.6, max_start_size=0.9,
               min_angular_velocity=-1.2, max_angular_velocity=1.2,
               size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(VOID_DEEP, 0.0)),
                   (0.12, rgba(VOID_DEEP, 0.42)),
                   (0.50, rgba(UMBRA, 0.42)),
                   (1.00, rgba(UMBRA, 0.0))))
    column = ac.streaks("Brood Column", 24, 1.0, 0.26, 0.65, radius=0.5, drag=1.2, length=7.0,
                        up=1.0, delay=0.1)
    column.min_velocity = (-0.3, 4.5, -0.3)
    column.max_velocity = (0.3, 7.0, 0.3)
    return ParticleSystem(emitters=[
        ac.sigil("Brood Sigil", 4.4, VOID, alpha=0.7, lifetime=1.8, spin=0.8, grow=1.1),
        ac.ring("Brood Shockwave", 1.0, 8.0, VOID, alpha=0.34, lifetime=0.55, hold=0.5,
                delay=0.1),
        ac.dark_smoke("Brood Ground Smoke", 16, 0.7, 0.9, 1.0, alpha=0.45, speed=3.0, rise=0.2,
                      drag=3.5, delay=0.1),
        ac.flash("Brood Flash", 2.0, VOID, alpha=0.4, lifetime=0.3, delay=0.1),
        pillar, column,
        ac.wisps("Brood Wisps", 12, 2.0, 0.42, 0.9, alpha=0.5, radius=0.5, drag=2.0, rise=1.6,
                 orbital=3.0, delay=0.15),
    ])


# =========================================================================================
# Rot field
# =========================================================================================

def rot_field_ground():
    """GROUND_ACTIVE puddle, radius 8: a black-green pool, bubbles popping across it, miasma
    rising and a few motes drifting up. Loops -- the engine tears it down with the field."""
    R = 8.0
    pool = looping(e(name="Rot Pool", simulation_space=hpar.SIM_LOCAL,
                     render_mode=hpar.RENDER_HORIZONTAL, material_name=SMOKE,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.02, R * 0.92),
                     min_velocity=(0.0, 0.08, 0.0), max_velocity=(0.0, 0.08, 0.0),
                     min_start_size=3.4, max_start_size=4.8,
                     min_angular_velocity=-0.2, max_angular_velocity=0.2,
                     size_over_life=float_curve((0.0, 0.6), (0.4, 1.0), (1.0, 1.1)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(MIASMA, 0.0)),
                         (0.25, rgba(BLIGHT_DEEP, 0.48)),
                         (0.70, rgba(BLIGHT_DEEP, 0.40)),
                         (1.00, rgba(MIASMA, 0.0)))), rate=40.0, lifetime=2.5)
    # The field's edge: one ring sprite whose bright band sits on the 8 yd radius (the band is
    # at ~62 % of the quad half-size). Rises slowly so grass does not swallow it.
    ring_size = 2.0 * R / 0.62
    edge = looping(e(name="Rot Rim", simulation_space=hpar.SIM_LOCAL,
                     render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                     min_velocity=(0.0, 0.12, 0.0), max_velocity=(0.0, 0.12, 0.0),
                     min_start_size=ring_size, max_start_size=ring_size,
                     min_angular_velocity=-0.25, max_angular_velocity=0.25,
                     size_over_life=float_curve((0.0, 0.97), (1.0, 1.01)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(BLIGHT, 0.0)),
                         (0.30, rgba(BLIGHT, 0.42)),
                         (0.70, rgba(BLIGHT_DEEP, 0.40)),
                         (1.00, rgba(BLIGHT_DEEP, 0.0)))), rate=1.6, lifetime=2.4)
    bubbles = looping(e(name="Rot Bubbles", simulation_space=hpar.SIM_LOCAL,
                        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.05, R * 0.9),
                        min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.45, 0.0),
                        min_start_size=0.15, max_start_size=0.4,
                        size_over_life=float_curve((0.0, 0.3), (0.85, 1.0), (1.0, 1.4)),
                        color_over_lifetime=color_curve(
                            (0.00, rgba(BLIGHT_HOT, 0.0)),
                            (0.20, rgba(BLIGHT_HOT, 0.70)),
                            (0.85, rgba(BLIGHT, 0.60)),
                            (1.00, rgba(BLIGHT, 0.0)))), rate=22.0, lifetime=0.8)
    miasma = looping(e(name="Rot Miasma", simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
                       shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.1, R * 0.85),
                       min_velocity=(-0.1, 0.5, -0.1), max_velocity=(0.1, 1.1, 0.1),
                       min_start_size=1.0, max_start_size=1.6,
                       min_angular_velocity=-0.6, max_angular_velocity=0.6,
                       noise_amplitude=0.6, noise_frequency=0.8,
                       size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.5)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(BLIGHT_DEEP, 0.0)),
                           (0.20, rgba(BLIGHT_DEEP, 0.26)),
                           (0.60, rgba(MIASMA, 0.22)),
                           (1.00, rgba(MIASMA, 0.0)))), rate=12.0, lifetime=2.4)
    motes = looping(e(name="Rot Motes", simulation_space=hpar.SIM_WORLD, material_name=GLOW,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.1, R * 0.8),
                      min_velocity=(-0.1, 0.4, -0.1), max_velocity=(0.1, 1.0, 0.1),
                      min_start_size=0.06, max_start_size=0.12,
                      noise_amplitude=0.8, noise_frequency=1.2,
                      size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.4)),
                      color_over_lifetime=fade(BLIGHT_HOT, 0.7, peak=0.15, end=BLIGHT)),
                    rate=10.0, lifetime=2.0)
    return ParticleSystem(emitters=[pool, edge, miasma, bubbles, motes])


def rot_field_tick():
    drops = e(name="Tick Splash", simulation_space=hpar.SIM_WORLD, material_name=GLOW,
              bursts=[Burst(0.0, 8)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
              min_velocity=(-0.8, 0.2, -0.8), max_velocity=(0.8, 1.6, 0.8),
              min_start_speed=0.8, max_start_speed=2.0,
              min_lifetime=0.3, max_lifetime=0.55,
              min_start_size=0.04, max_start_size=0.08,
              gravity=(0.0, -8.0, 0.0), drag=1.0,
              render_mode=hpar.RENDER_STRETCHED, length_scale=2.2,
              size_over_life=float_curve((0.0, 1.0), (1.0, 0.6)),
              color_over_lifetime=fade(BLIGHT_HOT, 0.9, peak=0.1, end=BLIGHT))
    return ParticleSystem(emitters=[
        ac.flash("Rot Tick Flash", 0.6, BLIGHT, alpha=0.36, lifetime=0.16),
        ac.dark_smoke("Rot Tick Puff", 5, 0.12, 0.42, 0.6, colour=MIASMA, alpha=0.38, speed=0.9),
        drops,
    ])


# =========================================================================================
# Tongue, Wail
# =========================================================================================

def tongue_aura():
    """Violet runes and wisps circling the head. Origin is the feet, so lift to ~1.9."""
    runes = looping(e(name="Tongue Runes", material_name=STAR,
                      shape=hpar.SHAPE_SPHERE, shape_extents=(0.28, 0.0, 0.0),
                      orbital_speed=3.5,
                      min_start_size=0.14, max_start_size=0.22,
                      min_angular_velocity=-2.5, max_angular_velocity=2.5,
                      size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 0.6)),
                      color_over_lifetime=fade(VOID_HOT, 0.8, peak=0.2, end=VOID)),
                    rate=14.0, lifetime=1.0)
    lift(runes, 1.9, drag=5.0, hidden=0.2, spread=0.04)
    circle = looping(e(name="Tongue Wisps", material_name=WISP,
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.32, 0.0, 0.0),
                       orbital_speed=4.5,
                       min_start_size=0.16, max_start_size=0.24,
                       min_angular_velocity=-3.0, max_angular_velocity=3.0,
                       size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 0.8)),
                       color_over_lifetime=fade(VOID, 0.5, peak=0.25, end=VOID_DEEP)),
                     rate=11.0, lifetime=0.9)
    lift(circle, 1.85, drag=5.0, hidden=0.2, spread=0.05)
    return ParticleSystem(emitters=[circle, runes])


def wail_burst():
    skulls = e(name="Wail Skulls", simulation_space=hpar.SIM_WORLD, material_name=SKULL,
               bursts=[Burst(0.0, 5)],
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.3, 0.0, 0.0),
               min_velocity=(-1.0, 0.1, -1.0), max_velocity=(1.0, 0.6, 1.0),
               min_start_speed=1.4, max_start_speed=2.6, drag=1.8,
               min_start_rotation=0.0, max_start_rotation=0.0,
               min_lifetime=0.8, max_lifetime=1.0,
               min_start_size=0.5, max_start_size=0.7,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.4)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(DREAD_HOT, 0.0)),
                   (0.15, rgba(DREAD_HOT, 0.75)),
                   (0.55, rgba(DREAD, 0.6)),
                   (1.00, rgba(DREAD, 0.0))))
    return ParticleSystem(emitters=[
        ac.flash("Wail Flash", 1.4, DREAD, alpha=0.45, lifetime=0.22),
        ac.ring("Wail Shockwave", 0.8, 8.0, DREAD, alpha=0.4, lifetime=0.5, hold=0.5,
                horizontal=False),
        ac.ring("Wail Ground Wave", 0.8, 8.0, DREAD_DEEP, alpha=0.4, lifetime=0.55, hold=0.5),
        ac.dark_smoke("Wail Smoke", 12, 0.3, 0.8, 0.9, alpha=0.42, speed=3.2, rise=0.3,
                      drag=3.2),
        skulls,
        ac.wisps("Wail Wisps", 12, 3.2, 0.40, 0.8, hot=DREAD_HOT, colour=DREAD, alpha=0.5,
                 drag=2.5, rise=0.5),
    ])


# =========================================================================================
# Pet buffs / heals
# =========================================================================================

def empower_aura():
    """Violet flames licking up a pet (pet-sized, so lower and tighter than body auras)."""
    flames = _smoke_loop("Empower Flames", 12.0, 0.7, UMBRA, VOID, 0.36, size=(0.26, 0.40),
                         vel=(0.5, 1.0), **_body_cone(0.9, 0.28))
    flames.size_over_life = float_curve((0.0, 0.6), (0.4, 1.0), (1.0, 0.5))
    embers = _motes("Empower Embers", 9.0, 0.8, VOID, VOID_HOT, alpha=0.9, material=BEAM,
                    size=(0.05, 0.08), noise_amplitude=1.0, noise_frequency=2.0,
                    min_velocity=(-0.1, 0.6, -0.1), max_velocity=(0.1, 1.2, 0.1),
                    render_mode=hpar.RENDER_STRETCHED, length_scale=2.4,
                    **_body_cone(1.0, 0.26))
    return ParticleSystem(emitters=[flames, embers])


def root_tendrils():
    """~2 s: crimson-black tendrils rise round the feet and grip, then sink back."""
    grip = _tendrils("Grip Tendrils", 14, 0.35, 0.8, 1.6, TAR, DREAD, alpha=0.6)
    grip.orbital_speed = 0.8
    hold = e(name="Grip Smoke", simulation_space=hpar.SIM_LOCAL, material_name=SMOKE,
             duration=1.4, spawn_rate=10.0,
             shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.05, 0.4),
             min_velocity=(-0.05, 0.1, -0.05), max_velocity=(0.05, 0.35, 0.05),
             min_lifetime=0.6, max_lifetime=0.9,
             min_start_size=0.3, max_start_size=0.45,
             min_angular_velocity=-1.0, max_angular_velocity=1.0,
             size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.2)),
             color_over_lifetime=fade(TAR, 0.45, peak=0.2, end=DREAD_DEEP))
    return ParticleSystem(emitters=[
        ac.ring("Grip Ring", 0.6, 1.6, DREAD, alpha=0.4, lifetime=0.35),
        hold, grip,
        ac.sigil("Grip Sigil", 1.8, DREAD, alpha=0.5, lifetime=1.8, spin=0.6, grow=1.0),
    ])


def healing_blight():
    """Lingering Dread: sickly red-grey motes that rise off the cursed target and sour."""
    haze = _smoke_loop("Blight Haze", 5.0, 1.2, BLOODGREY_DEEP, BLOODGREY, 0.30,
                       vel=(0.15, 0.4), **_body_cone(1.5, 0.30))
    motes = _motes("Sick Motes", 6.0, 1.1, BLOODGREY_DEEP, BLOODGREY, alpha=0.7, material=GLOW,
                   size=(0.05, 0.10), noise_amplitude=0.7, noise_frequency=1.5,
                   min_velocity=(-0.1, 0.25, -0.1), max_velocity=(0.1, 0.65, 0.1),
                   **_body_cone(1.5, 0.30))
    return ParticleSystem(emitters=[haze, motes])


def shared_night_heal():
    motes = e(name="Night Motes", material_name=STAR,
              bursts=[Burst(0.0, 12)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.9, 0.0, 0.0),
              attractor_position=(0.0, 0.0, 0.0), attractor_strength=14.0, drag=3.0,
              orbital_speed=3.0,
              min_lifetime=0.5, max_lifetime=0.7,
              min_start_size=0.08, max_start_size=0.15,
              min_angular_velocity=-5.0, max_angular_velocity=5.0,
              size_over_life=float_curve((0.0, 0.5), (0.5, 1.0), (1.0, 0.2)),
              color_over_lifetime=fade(VOID_HOT, 0.9, peak=0.2, end=VOID))
    return ParticleSystem(emitters=[
        motes,
        ac.flash("Night Glow", 0.7, VOID, alpha=0.35, lifetime=0.28, delay=0.35),
        ac.wisps("Night Wisps", 4, 0.8, 0.28, 0.6, alpha=0.45, radius=0.3, drag=2.0, rise=0.9,
                 delay=0.35),
    ])


EFFECTS = {
    "FrailtyApply.hpar": frailty_apply,
    "FrailtyAura.hpar": frailty_aura,
    "DrainChannel.hpar": drain_channel,
    "DrainBeamMotes.hpar": drain_beam_motes,
    "MireApply.hpar": mire_apply,
    "MireAura.hpar": mire_aura,
    "PlagueApply.hpar": plague_apply,
    "PlagueAura.hpar": plague_aura,
    "GloomlingSummon.hpar": gloomling_summon,
    "RotFieldGround.hpar": rot_field_ground,
    "RotFieldTick.hpar": rot_field_tick,
    "TongueAura.hpar": tongue_aura,
    "WailBurst.hpar": wail_burst,
    "EmpowerAura.hpar": empower_aura,
    "BroodSummon.hpar": brood_summon,
    "RootTendrils.hpar": root_tendrils,
    "HealingBlight.hpar": healing_blight,
    "SharedNightHeal.hpar": shared_night_heal,
}


def main():
    wanted = set(sys.argv[1:])
    for filename, build in EFFECTS.items():
        if not wanted or filename in wanted or filename[:-5] in wanted:
            write(build(), filename)


if __name__ == "__main__":
    main()
