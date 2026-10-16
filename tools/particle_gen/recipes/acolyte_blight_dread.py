# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Blight- and dread-school acolyte effects. Run from the repo root::

    py -3.14 tools/particle_gen/recipes/acolyte_blight_dread.py

Writes into ``data/client/Particles/Acolyte/``. Read ``acolyte_common``'s docstring first.

**Blight** (Corrupting Touch, Leeching Rot) is wet and organic: acid-green droplets that fall
under gravity, bubbling motes, olive-black miasma rising off the afflicted. **Dread** (Fear,
Embrace of Dread) is crimson on black: skulls, the void sigil, smoke that presses down.

Placement contract:

========================  ============  ==========  =========================================
File                      Attach        Lifetime    Notes
========================  ============  ==========  =========================================
BlightHandChannel         hand_r        looping     Corrupting Touch cast bar
CorruptionApply           spine_03      one-shot    the curse landing
CorruptionAura            feet          looping     12-16 s DoT idle
CorruptionTick            spine_03      one-shot    each damage tick (every 3 s)
LeechingRotHeal           spine_03      one-shot    caster healed by each tick: keep it small
DreadHandChannel          hand_r        looping     Fear cast bar
FearApply                 head          one-shot    a skull screams up out of the victim
FearAura                  feet          looping     skull + wisps circling over the head
EmbraceBurst              feet          one-shot    the acolyte becomes an avatar of dread
EmbraceAura               feet          looping     20 s
========================  ============  ==========  =========================================
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acolyte_common as ac
from acolyte_common import (
    BEAM, BLIGHT, BLIGHT_DEEP, BLIGHT_HOT, DREAD, DREAD_DEEP, DREAD_HOT, GLOW, MIASMA, SKULL,
    SMOKE, STAR, UMBRA, VOID_SIGIL, WISP, Burst, ParticleSystem, color_curve, e, fade,
    float_curve, hpar, lift, looping, rgba, write)


def _drops(name, count, speed, size=0.09, lifetime=0.7, delay=0.0, alpha=0.9,
           colour=BLIGHT, hot=BLIGHT_HOT):
    """Acid droplets: a quick spray that arcs down under gravity, stretched along the fall."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
             min_velocity=(-0.8, 0.2, -0.8), max_velocity=(0.8, 1.6, 0.8),
             min_start_speed=speed * 0.4, max_start_speed=speed,
             min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
             min_start_size=size * 0.5, max_start_size=size,
             gravity=(0.0, -8.0, 0.0), drag=1.0,
             render_mode=hpar.RENDER_STRETCHED, length_scale=2.4, material_name=GLOW,
             size_over_life=float_curve((0.0, 1.0), (1.0, 0.6)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, alpha)),
                 (0.40, rgba(colour, alpha * 0.85)),
                 (1.00, rgba(BLIGHT_DEEP, 0.0))))


# =========================================================================================
# Blight
# =========================================================================================

def blight_hand_channel():
    """Rot festering in the hand: acid-green bubbles swelling and popping, miasma curling off,
    and slow drops falling from the fingers."""
    core = looping(e(name="Blight Core",
                     min_start_size=0.30, max_start_size=0.40,
                     size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
                     color_over_lifetime=fade(BLIGHT, 0.36, peak=0.3, end=BLIGHT_DEEP, hold=0.7)),
                   rate=12.0, lifetime=0.35)
    bubbles = looping(e(name="Blight Bubbles",
                        shape=hpar.SHAPE_SPHERE, shape_extents=(0.14, 0.0, 0.0),
                        min_velocity=(-0.1, -0.1, -0.1), max_velocity=(0.1, 0.1, 0.1),
                        orbital_speed=2.5,
                        min_start_size=0.05, max_start_size=0.11,
                        # A bubble swells and pops: grows, then vanishes in the last 10%.
                        size_over_life=float_curve((0.0, 0.3), (0.85, 1.0), (1.0, 1.4)),
                        color_over_lifetime=color_curve(
                            (0.00, rgba(BLIGHT_HOT, 0.0)),
                            (0.20, rgba(BLIGHT_HOT, 0.85)),
                            (0.85, rgba(BLIGHT, 0.75)),
                            (1.00, rgba(BLIGHT, 0.0)))), rate=24.0, lifetime=0.45)
    miasma = looping(e(name="Blight Miasma", simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
                       min_velocity=(-0.15, 0.25, -0.15), max_velocity=(0.15, 0.6, 0.15),
                       min_start_size=0.18, max_start_size=0.28,
                       min_angular_velocity=-1.5, max_angular_velocity=1.5,
                       noise_amplitude=0.8, noise_frequency=1.5,
                       size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.5)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(BLIGHT_DEEP, 0.0)),
                           (0.15, rgba(BLIGHT_DEEP, 0.40)),
                           (0.55, rgba(MIASMA, 0.36)),
                           (1.00, rgba(MIASMA, 0.0)))), rate=14.0, lifetime=0.8)
    drip = looping(e(name="Blight Drips", simulation_space=hpar.SIM_WORLD,
                     shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
                     min_velocity=(-0.05, -0.2, -0.05), max_velocity=(0.05, 0.0, 0.05),
                     min_start_size=0.04, max_start_size=0.07,
                     gravity=(0.0, -6.0, 0.0),
                     render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
                     size_over_life=float_curve((0.0, 1.0), (1.0, 0.7)),
                     color_over_lifetime=fade(BLIGHT_HOT, 0.9, peak=0.1, end=BLIGHT, hold=0.6)),
                   rate=6.0, lifetime=0.55)
    return ParticleSystem(emitters=[miasma, core, bubbles, drip])


def corruption_apply():
    """The curse lands: a wet green splash, a cloud of miasma, a faint green skull leering out
    of it for a moment, droplets and bubbles sprayed off."""
    return ParticleSystem(emitters=[
        ac.flash("Curse Flash", 1.0, BLIGHT, alpha=0.5, lifetime=0.18),
        ac.dark_smoke("Curse Miasma", 12, 0.2, 0.6, 1.0, colour=MIASMA, alpha=0.45, speed=1.6,
                      rise=0.6),
        ac.wisps("Curse Wisps", 8, 2.4, 0.38, 0.7, hot=BLIGHT_HOT, colour=BLIGHT, alpha=0.55,
                 drag=3.0, rise=0.8),
        _drops("Curse Drops", 18, 3.0),
        ac.skull("Curse Skull", 0.85, BLIGHT_DEEP, BLIGHT, alpha=0.55, lifetime=0.8, rise=0.5,
                 delay=0.04),
        ac.ring("Curse Ring", 0.3, 1.8, BLIGHT, alpha=0.45, lifetime=0.3, horizontal=False),
    ])


def corruption_aura():
    """The rot while it lasts: olive-black miasma seeping up the body, acid wisps and the odd
    green drip. Lives on a target at the feet origin; ~20 particles."""
    seep = looping(e(name="Rot Seep", simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.5, 0.30),
                     min_velocity=(-0.1, 0.25, -0.1), max_velocity=(0.1, 0.55, 0.1),
                     min_start_size=0.28, max_start_size=0.44,
                     min_angular_velocity=-1.0, max_angular_velocity=1.0,
                     noise_amplitude=0.5, noise_frequency=1.0,
                     size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(BLIGHT_DEEP, 0.0)),
                         (0.20, rgba(BLIGHT_DEEP, 0.38)),
                         (0.60, rgba(MIASMA, 0.32)),
                         (1.00, rgba(MIASMA, 0.0)))), rate=8.0, lifetime=1.3)
    wisp = looping(e(name="Rot Wisps", simulation_space=hpar.SIM_WORLD, material_name=WISP,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.6, 0.28),
                     min_velocity=(-0.1, 0.35, -0.1), max_velocity=(0.1, 0.7, 0.1),
                     min_start_size=0.18, max_start_size=0.28,
                     min_angular_velocity=-2.0, max_angular_velocity=2.0,
                     size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.1)),
                     color_over_lifetime=fade(BLIGHT, 0.45, peak=0.2, end=BLIGHT_DEEP)),
                   rate=4.0, lifetime=0.9)
    drip = looping(e(name="Rot Drips", simulation_space=hpar.SIM_WORLD,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.5, 0.25),
                     min_velocity=(0.0, -0.1, 0.0), max_velocity=(0.0, 0.0, 0.0),
                     min_start_size=0.04, max_start_size=0.07,
                     gravity=(0.0, -5.0, 0.0),
                     render_mode=hpar.RENDER_STRETCHED, length_scale=2.0,
                     size_over_life=float_curve((0.0, 1.0), (1.0, 0.7)),
                     color_over_lifetime=fade(BLIGHT_HOT, 0.85, peak=0.1, end=BLIGHT, hold=0.6)),
                   rate=3.0, lifetime=0.6)
    return ParticleSystem(emitters=[seep, wisp, drip])


def corruption_tick():
    """One damage tick: a small sick puff and a spatter of drops. Every 3 s, so modest."""
    return ParticleSystem(emitters=[
        ac.flash("Tick Flash", 0.6, BLIGHT, alpha=0.38, lifetime=0.16),
        ac.dark_smoke("Tick Puff", 5, 0.12, 0.42, 0.6, colour=MIASMA, alpha=0.38, speed=0.9),
        _drops("Tick Drops", 8, 2.0, size=0.07, lifetime=0.55),
    ])


def leeching_rot_heal():
    """The tick's stolen life arriving in the acolyte: a handful of green-violet motes
    converging on the chest. Fires every 3 s while Corrupting Touch runs -- tiny on purpose."""
    motes = e(name="Leech Motes", material_name=STAR,
              bursts=[Burst(0.0, 8)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.7, 0.0, 0.0),
              attractor_position=(0.0, 0.0, 0.0), attractor_strength=14.0, drag=3.0,
              orbital_speed=3.0,
              min_lifetime=0.45, max_lifetime=0.6,
              min_start_size=0.08, max_start_size=0.14,
              min_angular_velocity=-5.0, max_angular_velocity=5.0,
              size_over_life=float_curve((0.0, 0.5), (0.5, 1.0), (1.0, 0.2)),
              color_over_lifetime=fade(BLIGHT_HOT, 0.9, peak=0.2, end=BLIGHT))
    return ParticleSystem(emitters=[
        motes,
        ac.flash("Leech Glow", 0.55, BLIGHT, alpha=0.35, lifetime=0.25, delay=0.4),
    ])


# =========================================================================================
# Dread
# =========================================================================================

def dread_hand_channel():
    """Terror gathering in the hand: crimson wisps spiralling in around a black heart, with a
    faint skull flickering in the core now and then."""
    core = looping(e(name="Dread Core",
                     min_start_size=0.32, max_start_size=0.42,
                     size_over_life=float_curve((0.0, 0.8), (0.5, 1.0), (1.0, 0.85)),
                     color_over_lifetime=fade(DREAD, 0.38, peak=0.3, end=DREAD_DEEP, hold=0.7)),
                   rate=12.0, lifetime=0.32)
    heart = looping(e(name="Dread Heart", material_name=SMOKE,
                      min_start_size=0.20, max_start_size=0.26,
                      min_angular_velocity=-3.0, max_angular_velocity=3.0,
                      size_over_life=float_curve((0.0, 0.7), (0.5, 1.0), (1.0, 0.8)),
                      color_over_lifetime=fade(UMBRA, 0.6, peak=0.3, hold=0.7)),
                    rate=10.0, lifetime=0.4)
    inward = looping(e(name="Dread Wisps", material_name=WISP,
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.45, 0.0, 0.0),
                       attractor_position=(0.0, 0.0, 0.0), attractor_strength=9.0, drag=2.5,
                       orbital_speed=-5.0,
                       min_start_size=0.16, max_start_size=0.26,
                       min_angular_velocity=-4.0, max_angular_velocity=4.0,
                       size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(DREAD_DEEP, 0.0)),
                           (0.30, rgba(DREAD, 0.50)),
                           (0.80, rgba(DREAD_HOT, 0.45)),
                           (1.00, rgba(DREAD_HOT, 0.0)))), rate=20.0, lifetime=0.45)
    face = looping(e(name="Dread Face", material_name=SKULL,
                     min_start_rotation=0.0, max_start_rotation=0.0,
                     min_start_size=0.26, max_start_size=0.30,
                     size_over_life=float_curve((0.0, 0.8), (1.0, 1.1)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(DREAD, 0.0)),
                         (0.40, rgba(DREAD_HOT, 0.42)),
                         (1.00, rgba(DREAD, 0.0)))), rate=1.5, lifetime=0.6)
    return ParticleSystem(emitters=[core, inward, heart, face])


def fear_apply():
    """Fear takes hold: a crimson skull screams up out of the victim's head and dissolves,
    a black-crimson shockwave, wisps fleeing outward in every direction."""
    return ParticleSystem(emitters=[
        ac.flash("Fear Flash", 1.0, DREAD, alpha=0.5, lifetime=0.2),
        ac.skull("Fear Skull", 1.0, DREAD, DREAD_HOT, alpha=0.85, lifetime=1.0, rise=1.6,
                 grow=1.45, drag=1.8),
        ac.skull("Fear Skull Echo", 0.8, DREAD_DEEP, DREAD, alpha=0.45, lifetime=0.8, rise=0.9,
                 grow=1.8, drag=1.8, delay=0.12),
        ac.ring("Fear Shockwave", 0.4, 2.8, DREAD, alpha=0.5, lifetime=0.35, horizontal=False),
        ac.wisps("Fear Wisps", 12, 3.0, 0.40, 0.8, hot=DREAD_HOT, colour=DREAD, alpha=0.55,
                 drag=2.5, rise=0.5),
        ac.dark_smoke("Fear Smoke", 8, 0.15, 0.5, 0.8, alpha=0.40, speed=1.4, rise=0.3),
    ])


def fear_aura():
    """While feared: a small crimson skull bobbing over the head, wisps circling it, and dark
    smoke trailing behind the fleeing body. The origin is the feet (no bone), so the head
    layers are drag-lifted to ~2.3 units."""
    skull = looping(e(name="Terror Skull", material_name=SKULL,
                      min_start_rotation=0.0, max_start_rotation=0.0,
                      min_start_size=0.40, max_start_size=0.44,
                      size_over_life=float_curve((0.0, 0.9), (0.5, 1.05), (1.0, 0.9)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(DREAD, 0.0)),
                          (0.25, rgba(DREAD_HOT, 0.65)),
                          (0.75, rgba(DREAD, 0.55)),
                          (1.00, rgba(DREAD, 0.0)))), rate=2.0, lifetime=1.0)
    lift(skull, 2.3, drag=5.0, hidden=0.2, spread=0.01)
    circle = looping(e(name="Terror Wisps", material_name=WISP,
                       shape=hpar.SHAPE_SPHERE, shape_extents=(0.35, 0.0, 0.0),
                       orbital_speed=4.0,
                       min_start_size=0.16, max_start_size=0.24,
                       min_angular_velocity=-3.0, max_angular_velocity=3.0,
                       size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 0.8)),
                       color_over_lifetime=fade(DREAD, 0.5, peak=0.25, end=DREAD_DEEP)),
                     rate=8.0, lifetime=0.9)
    lift(circle, 2.2, drag=5.0, hidden=0.2, spread=0.05)
    trail = looping(e(name="Terror Smoke", simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.2, 0.3),
                      min_velocity=(-0.1, 0.2, -0.1), max_velocity=(0.1, 0.5, 0.1),
                      min_start_size=0.30, max_start_size=0.45,
                      min_angular_velocity=-1.0, max_angular_velocity=1.0,
                      size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
                      color_over_lifetime=fade(UMBRA, 0.36, peak=0.15)),
                    rate=6.0, lifetime=1.0)
    return ParticleSystem(emitters=[trail, skull, circle])


def embrace_burst():
    """The acolyte becomes an avatar of dread: the crimson void sigil burns onto the ground,
    a column of black smoke and crimson streaks erupts, three skulls spiral up the body and
    a shockwave of dark smoke rolls out."""
    skulls = e(name="Ascending Skulls", material_name=SKULL,
               bursts=[Burst(0.05, 1), Burst(0.18, 1), Burst(0.31, 1)],
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.7),
               min_velocity=(0.0, 1.8, 0.0), max_velocity=(0.0, 2.2, 0.0),
               orbital_speed=3.5, drag=0.6,
               min_start_rotation=0.0, max_start_rotation=0.0,
               min_lifetime=0.9, max_lifetime=1.0,
               min_start_size=0.55, max_start_size=0.65,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(DREAD_HOT, 0.0)),
                   (0.15, rgba(DREAD_HOT, 0.75)),
                   (0.55, rgba(DREAD, 0.6)),
                   (1.00, rgba(DREAD, 0.0))))
    column = ac.streaks("Dread Column", 26, 1.0, 0.26, 0.6, hot=DREAD_HOT, colour=DREAD,
                        alpha=0.8, radius=0.45, drag=1.2, length=7.0, up=1.0)
    column.min_velocity = (-0.3, 4.5, -0.3)
    column.max_velocity = (0.3, 7.5, 0.3)
    pillar = e(name="Dread Pillar", simulation_space=hpar.SIM_LOCAL, material_name=SMOKE,
               duration=0.4, spawn_rate=45.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.6),
               min_velocity=(0.0, 1.6, 0.0), max_velocity=(0.0, 3.0, 0.0),
               orbital_speed=3.0, drag=1.0,
               min_lifetime=0.7, max_lifetime=1.0,
               min_start_size=0.55, max_start_size=0.85,
               min_angular_velocity=-1.2, max_angular_velocity=1.2,
               size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(DREAD_DEEP, 0.0)),
                   (0.12, rgba(DREAD_DEEP, 0.45)),
                   (0.50, rgba(UMBRA, 0.42)),
                   (1.00, rgba(UMBRA, 0.0))))
    return ParticleSystem(emitters=[
        ac.sigil("Dread Sigil", 3.6, DREAD, alpha=0.75, lifetime=1.6, spin=0.8, grow=1.1),
        ac.ring("Dread Shockwave", 1.0, 9.0, DREAD, alpha=0.38, lifetime=0.55, hold=0.5),
        ac.dark_smoke("Dread Ground Smoke", 16, 0.6, 0.9, 1.0, alpha=0.45, speed=3.0, rise=0.2,
                      drag=3.5),
        ac.flash("Dread Flash", 2.0, DREAD, alpha=0.45, lifetime=0.3),
        pillar, column, skulls,
        ac.wisps("Dread Wisps", 14, 2.2, 0.45, 0.9, hot=DREAD_HOT, colour=DREAD, alpha=0.5,
                 radius=0.5, drag=2.0, rise=1.6, orbital=3.0),
    ])


def embrace_aura():
    """20 s as an avatar of dread: the sigil smoulders under the feet, black-crimson flames
    of smoke lick up the body and crimson embers rise off the shoulders."""
    sigil = looping(e(name="Avatar Sigil", material_name=VOID_SIGIL,
                      render_mode=hpar.RENDER_HORIZONTAL,
                      min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.02, 0.0),
                      min_angular_velocity=0.5, max_angular_velocity=0.5,
                      min_start_size=3.0, max_start_size=3.0,
                      color_over_lifetime=color_curve(
                          (0.00, rgba(DREAD, 0.0)),
                          (0.30, rgba(DREAD, 0.30)),
                          (0.70, rgba(DREAD_DEEP, 0.30)),
                          (1.00, rgba(DREAD_DEEP, 0.0)))), rate=1.2, lifetime=2.0)
    flames = looping(e(name="Avatar Flames", simulation_space=hpar.SIM_WORLD, material_name=SMOKE,
                       shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.4, 0.32),
                       min_velocity=(-0.1, 0.6, -0.1), max_velocity=(0.1, 1.1, 0.1),
                       min_start_size=0.30, max_start_size=0.46,
                       min_angular_velocity=-1.5, max_angular_velocity=1.5,
                       noise_amplitude=0.8, noise_frequency=1.5,
                       size_over_life=float_curve((0.0, 0.6), (0.4, 1.0), (1.0, 0.5)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(DREAD, 0.0)),
                           (0.15, rgba(DREAD, 0.38)),
                           (0.50, rgba(DREAD_DEEP, 0.40)),
                           (1.00, rgba(UMBRA, 0.0)))), rate=12.0, lifetime=0.8)
    embers = looping(e(name="Avatar Embers", simulation_space=hpar.SIM_WORLD,
                       shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.7, 0.30),
                       min_velocity=(-0.15, 0.7, -0.15), max_velocity=(0.15, 1.3, 0.15),
                       min_start_size=0.05, max_start_size=0.08,
                       noise_amplitude=1.2, noise_frequency=2.0,
                       render_mode=hpar.RENDER_STRETCHED, length_scale=2.5, material_name=BEAM,
                       size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                       color_over_lifetime=fade(DREAD_HOT, 0.95, peak=0.1, end=DREAD)),
                     rate=10.0, lifetime=0.8)
    return ParticleSystem(emitters=[sigil, flames, embers])


EFFECTS = {
    "BlightHandChannel.hpar": blight_hand_channel,
    "CorruptionApply.hpar": corruption_apply,
    "CorruptionAura.hpar": corruption_aura,
    "CorruptionTick.hpar": corruption_tick,
    "LeechingRotHeal.hpar": leeching_rot_heal,
    "DreadHandChannel.hpar": dread_hand_channel,
    "FearApply.hpar": fear_apply,
    "FearAura.hpar": fear_aura,
    "EmbraceBurst.hpar": embrace_burst,
    "EmbraceAura.hpar": embrace_aura,
}


def main():
    wanted = set(sys.argv[1:])
    for filename, build in EFFECTS.items():
        if not wanted or filename in wanted or filename[:-5] in wanted:
            write(build(), filename)


if __name__ == "__main__":
    main()
