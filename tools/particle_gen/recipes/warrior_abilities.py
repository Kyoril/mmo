# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Caster-side warrior ability effects. Run from the repo root::

    python tools/particle_gen/recipes/warrior_abilities.py

These spawn on the caster. The three cooldowns that grant the warrior something -- Battlecry,
Bloodrush, Last Stand -- carry the warm accent; the two debuff shouts sit cold and
desaturated so the two groups never read as the same effect.

Two tuning notes that are not obvious from ``warrior_common`` alone and were discovered while
retuning this file against rendered previews:

* ``spark_burst`` always adds a ``direction * speed`` term on top of whatever
  ``min_velocity``/``max_velocity`` box a recipe assigns afterwards, because the spawn shape
  is a full sphere and ``direction`` is the (omnidirectional) spawn offset normalized. When a
  recipe overrides the velocity box to bias a burst toward one side, that omnidirectional
  term fights the bias and the burst reads as a symmetric ball instead of a directional sweep.
  Any spark emitter that needs real directionality also zeroes ``min_start_speed`` /
  ``max_start_speed`` after construction so the velocity box is the only thing steering it.
* ``ground_ring``'s size and colour curves both run over the particle's full normalized
  lifetime, but independently: size grows linearly to the end value over 100% of life while
  alpha peaks at 15% and fades linearly back to zero by 100%. For a ring whose end size is
  many times its start size, that means the ring is only a fraction of its final width while
  it is bright, and is already almost invisible by the time it has grown large enough to read
  as having travelled anywhere. Rings with a large size ratio get their curves overridden
  here so most of the growth happens while the ring can still be seen.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import ParticleSystem, rgba


def _fast_ring(ring, colour, alpha, growth_frac=0.35, growth_hold=0.75, hold_alpha_frac=0.7):
    """Override a ground_ring's size/colour curves so most of the expansion happens while
    the ring is still bright, instead of the size and alpha curves fading independently."""
    start = ring.min_start_size
    end_ratio = ring.size_over_life[-1].value
    ring.size_over_life = wc.float_curve(
        (0.0, 1.0),
        (growth_frac, 1.0 + (end_ratio - 1.0) * growth_hold),
        (1.0, end_ratio))
    ring.color_over_lifetime = wc.color_curve(
        (0.00, rgba(colour, 0.0)),
        (0.12, rgba(colour, alpha)),
        (growth_frac, rgba(colour, alpha * hold_alpha_frac)),
        (1.00, rgba(colour, 0.0)))
    return ring


def cleave_burst():
    """A crescent energy wave carved to the character's right. The velocity box is
    deliberately asymmetric -- that is what turns an omnidirectional burst into a
    directional sweep, and the shape is what reads as an arcane blade wave."""
    sweep = wc.spark_burst("Cleave Wave", count=36, speed=7.5, colour=wc.ARC_STEEL,
                           size=0.15, lifetime=0.42, gravity=-3.0, drag=2.0, spread=1.0)
    sweep.min_velocity = (0.3, -0.2, -1.9)
    sweep.max_velocity = (3.4, 0.8, 1.9)
    sweep.shape_extents = (0.30, 0.0, 0.0)
    # The base speed*direction term is omnidirectional (see module docstring) -- kill it so
    # the asymmetric box above is what actually steers the sweep to one side.
    sweep.min_start_speed = 0.0
    sweep.max_start_speed = 0.6
    trail = wc.energy_swirl("Cleave Trail", count=18, radius=0.9, colour=wc.ARC_GOLD,
                            alpha=0.34, size=0.18, lifetime=0.50, orbital=1.2, rise=0.8)
    return ParticleSystem(emitters=[
        sweep,
        trail,
        wc.dust_cloud("Cleave Wake", count=10, spread=1.0, colour=wc.DUST,
                      alpha=0.18, size=0.45, lifetime=0.45, rise=0.3),
    ])


def charge_dust():
    """An energy trail dragged behind a sprinting armoured body, with staggered ground
    rings stamping the path. The wake drifts backward, opposite the travel direction."""
    wake = wc.dust_cloud("Charge Wake", count=26, spread=1.0, colour=wc.DUST,
                         alpha=0.30, size=0.70, lifetime=0.85, rise=0.6)
    wake.min_velocity = (-0.9, 0.1, -2.6)
    wake.max_velocity = (0.9, 1.0, -0.4)
    streaks = wc.spark_burst("Charge Streaks", count=30, speed=5.5, colour=wc.ARC_EMBER,
                             size=0.15, lifetime=0.55, gravity=-2.0, drag=1.2, spread=0.7)
    streaks.min_velocity = (-0.7, 0.2, -3.0)
    streaks.max_velocity = (0.7, 1.6, -0.8)
    streaks.min_start_speed = 0.0
    streaks.max_start_speed = 0.6
    ring_a = wc.ground_ring("Charge Ring A", start_size=0.6, end_size=2.2, colour=wc.ARC_EMBER,
                            alpha=0.36, lifetime=0.50)
    ring_b = wc.ground_ring("Charge Ring B", start_size=0.6, end_size=2.2, colour=wc.ARC_GOLD,
                            alpha=0.30, lifetime=0.50, delay=0.22)
    # ground_ring defaults to SIM_LOCAL (follows the owner) which is right for a stationary
    # burst but wrong here: these rings are meant to stamp the ground at the point the caster
    # was standing when each one fired, not slide along behind a sprinting body.
    ring_a.simulation_space = wc.hpar.SIM_WORLD
    ring_b.simulation_space = wc.hpar.SIM_WORLD
    return ParticleSystem(emitters=[
        streaks,
        wake,
        ring_a,
        ring_b,
    ])


def shockwave_dust():
    """The signature effect. Two ground rings racing outward at different speeds are what
    make this read as a shockwave; the previous version had no ring at all and looked like
    a dark dust cloud. Energy rings lead, dust follows underneath."""
    outward = wc.dust_cloud("Shock Wall", count=34, spread=2.6, colour=wc.DUST,
                            alpha=0.24, size=0.85, lifetime=0.85, rise=0.5)
    outward.shape_extents = (0.5, 0.0, 0.0)
    ring = wc.ground_ring("Shock Ring", start_size=0.8, end_size=7.5, colour=wc.ARC_GOLD,
                          alpha=0.44, lifetime=0.70)
    _fast_ring(ring, wc.ARC_GOLD, 0.44, growth_frac=0.30, growth_hold=0.8, hold_alpha_frac=0.65)
    ring_inner = wc.ground_ring("Shock Ring Inner", start_size=0.5, end_size=3.6, colour=wc.HOT,
                                alpha=0.36, lifetime=0.50)
    _fast_ring(ring_inner, wc.HOT, 0.36, growth_frac=0.30, growth_hold=0.8, hold_alpha_frac=0.6)
    return ParticleSystem(emitters=[
        ring,
        ring_inner,
        outward,
        wc.spark_burst("Shock Grit", count=26, speed=5.5, colour=wc.ARC_EMBER,
                       size=0.11, lifetime=0.70, gravity=-10.0, drag=1.0, spread=1.8),
        wc.soft_flash("Shock Core", size=1.1, colour=wc.HOT, alpha=0.5, lifetime=0.16),
    ])


def rally_burst():
    """Battlecry. A golden heroic bloom: an upward flare, an orbiting swirl around the
    caster, a ground pulse, and star motes that outlive everything so it trails off
    instead of cutting."""
    flare = wc.spark_burst("Rally Flare", count=34, speed=4.2, colour=wc.ARC_GOLD,
                           size=0.15, lifetime=0.70, gravity=1.4, drag=1.0, spread=0.7)
    flare.min_velocity = (-0.7, 2.4, -0.7)
    flare.max_velocity = (0.7, 5.0, 0.7)
    # Kill the omnidirectional speed*direction term so the flare actually reads as a column
    # rising off the caster instead of a starburst that happens to drift upward on average.
    flare.min_start_speed = 0.0
    flare.max_start_speed = 0.6
    swirl = wc.energy_swirl("Rally Swirl", count=24, radius=0.55, colour=wc.ARC_GOLD,
                            alpha=0.38, size=0.19, lifetime=0.85, orbital=3.6, rise=3.0)
    ring = wc.ground_ring("Rally Ring", start_size=0.7, end_size=2.8, colour=wc.ARC_GOLD,
                          alpha=0.38, lifetime=0.55)
    _fast_ring(ring, wc.ARC_GOLD, 0.38, growth_frac=0.35, growth_hold=0.8, hold_alpha_frac=0.65)
    motes = wc.dust_cloud("Rally Motes", count=20, spread=0.7, colour=wc.ARC_GOLD,
                          alpha=0.36, size=0.26, lifetime=1.10, rise=1.6)
    motes.material_name = wc.STAR
    motes.gravity = (0.0, 0.5, 0.0)
    motes.min_velocity = (-0.5, 0.6, -0.5)
    motes.max_velocity = (0.5, 1.8, 0.5)
    return ParticleSystem(emitters=[
        flare,
        swirl,
        ring,
        motes,
    ])


def rage_burst():
    """Bloodrush. A crimson vortex held tight to the body -- an internal surge, not an
    outward blast, so nothing travels far and the swirl radius stays small."""
    embers = wc.spark_burst("Rage Embers", count=24, speed=2.8, colour=wc.ARC_EMBER,
                            size=0.12, lifetime=0.80, gravity=0.7, drag=1.6, spread=0.5)
    embers.min_velocity = (-0.35, 1.0, -0.35)
    embers.max_velocity = (0.35, 2.2, 0.35)
    # Held tight to the body: the omnidirectional start speed was throwing embers almost a
    # full body-height above the head. Let the (smaller, upward) box do the work instead.
    embers.min_start_speed = 0.0
    embers.max_start_speed = 0.4
    vortex = wc.energy_swirl("Rage Vortex", count=28, radius=0.42, colour=wc.ARC_CRIMSON,
                             alpha=0.42, size=0.18, lifetime=0.75, orbital=5.0, rise=2.4)
    ring = wc.ground_ring("Rage Ring", start_size=0.5, end_size=1.8, colour=wc.ARC_CRIMSON,
                          alpha=0.32, lifetime=0.50)
    return ParticleSystem(emitters=[
        vortex,
        embers,
        wc.dust_cloud("Rage Aura", count=18, spread=0.55, colour=wc.ARC_CRIMSON,
                      alpha=0.28, size=0.55, lifetime=0.75, rise=1.0),
        ring,
    ])


def dread_burst():
    """Provoke and Demoralizing Shout. Violet debuff magic pressing outward and low to the
    ground -- deliberately the opposite hue and the opposite motion from the rising golden
    rally pair, so a debuff can never be mistaken for a buff."""
    pressure = wc.dust_cloud("Dread Pressure", count=30, spread=2.1, colour=wc.ARC_VIOLET,
                             alpha=0.30, size=0.80, lifetime=0.80, rise=0.20)
    pressure.shape_extents = (0.4, 0.0, 0.0)
    sink = wc.energy_swirl("Dread Coil", count=20, radius=0.8, colour=wc.ARC_VIOLET,
                           hot=wc.ARC_VIOLET, alpha=0.32, size=0.16, lifetime=0.70,
                           orbital=-2.6, rise=0.2)
    sink.gravity = (0.0, -0.6, 0.0)   # dread settles rather than rising
    ring = wc.ground_ring("Dread Ring", start_size=0.7, end_size=4.8, colour=wc.ARC_VIOLET,
                          alpha=0.38, lifetime=0.65)
    _fast_ring(ring, wc.ARC_VIOLET, 0.38, growth_frac=0.32, growth_hold=0.8, hold_alpha_frac=0.65)
    return ParticleSystem(emitters=[
        ring,
        pressure,
        sink,
        wc.spark_burst("Dread Grit", count=14, speed=3.4, colour=wc.ARC_VIOLET,
                       size=0.09, lifetime=0.60, gravity=-7.0, drag=1.6, spread=1.4),
    ])


def guard_burst():
    """Last Stand. An azure barrier igniting around the warrior: a shell that expands and
    settles rather than exploding, so the particles slow down instead of flying apart."""
    shell = wc.spark_burst("Guard Shell", count=28, speed=3.2, colour=wc.ARC_AZURE,
                           size=0.14, lifetime=0.70, gravity=0.4, drag=3.0, spread=0.9)
    shell.min_velocity = (-1.0, 0.5, -1.0)
    shell.max_velocity = (1.0, 2.8, 1.0)
    weave = wc.energy_swirl("Guard Weave", count=22, radius=0.60, colour=wc.ARC_AZURE,
                            alpha=0.36, size=0.17, lifetime=0.90, orbital=2.8, rise=2.6)
    ring = wc.ground_ring("Guard Ring", start_size=0.9, end_size=2.4, colour=wc.ARC_AZURE,
                          alpha=0.40, lifetime=0.60)
    return ParticleSystem(emitters=[
        shell,
        weave,
        ring,
        wc.dust_cloud("Guard Haze", count=14, spread=0.7, colour=wc.ARC_AZURE,
                      alpha=0.22, size=0.55, lifetime=0.80, rise=0.5),
    ])


if __name__ == "__main__":
    wc.write(cleave_burst(), "CleaveBurst.hpar")
    wc.write(charge_dust(), "ChargeDust.hpar")
    wc.write(shockwave_dust(), "ShockwaveDust.hpar")
    wc.write(rally_burst(), "RallyBurst.hpar")
    wc.write(rage_burst(), "RageBurst.hpar")
    wc.write(dread_burst(), "DreadBurst.hpar")
    wc.write(guard_burst(), "GuardBurst.hpar")
