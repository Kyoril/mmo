# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Particle effects for the Hollow Choir's trash, built on the Oswin recipe's helpers and palette
so the Candlebearers read as part of Brother Oswin's vigil.

    py -3 tools/particle_gen/recipes/hollow_choir_trash.py

Candlebearer_WaxPool.hpar -- GROUND_ACTIVE of Spilled Wax (spell 268): the flame a Candlebearer
leaves where it falls. A small looping pool of burning wax, radius 2.2, held for the zone's
6 s; the engine stops its emitters when the zone ends and lets them burn out (max 4 s).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hollow_choir_oswin as oswin  # noqa: E402
from hollow_choir_oswin import (AMBER, CANDLE_GOLD, CANDLE_HOT, EMBER, FLAME, INCENSE, RING_BAND,  # noqa: E402
                                _e, _fade, write)
from warrior_common import BEAM, RING, color_curve, float_curve, hpar, rgba  # noqa: E402

POOL_RADIUS = 2.2


def wax_pool():
    ring_size = 2.0 * POOL_RADIUS / RING_BAND
    rim = _e(name="Pool Rim", loop=True, duration=1.0, spawn_rate=1.6, max_particles=6,
             min_lifetime=2.4, max_lifetime=2.4,
             min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
             min_start_size=ring_size, max_start_size=ring_size,
             min_angular_velocity=-0.3, max_angular_velocity=0.3,
             render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
             size_over_life=float_curve((0.0, 0.97), (1.0, 1.02)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(AMBER, 0.0)),
                 (0.30, rgba(CANDLE_GOLD, 0.40)),
                 (0.70, rgba(AMBER, 0.40)),
                 (1.00, rgba(AMBER, 0.0))))
    # Molten wax on the floor: soft amber discs overlapping into a glowing puddle.
    puddle = _e(name="Molten Wax", loop=True, duration=1.0, spawn_rate=7.0,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, POOL_RADIUS * 0.55),
                min_lifetime=1.6, max_lifetime=2.0,
                min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.03, 0.0),
                min_start_size=1.6, max_start_size=2.4,
                min_angular_velocity=-0.25, max_angular_velocity=0.25,
                render_mode=hpar.RENDER_HORIZONTAL,
                size_over_life=float_curve((0.0, 0.8), (1.0, 1.1)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(AMBER, 0.0)),
                    (0.30, rgba(CANDLE_GOLD, 0.26)),
                    (0.80, rgba(AMBER, 0.22)),
                    (1.00, rgba(AMBER, 0.0))))
    # Flames licking up across the pool, out of sync.
    flames = _e(name="Wax Flames", loop=True, duration=1.0, spawn_rate=26.0,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, POOL_RADIUS * 0.85),
                min_lifetime=0.55, max_lifetime=0.85,
                min_velocity=(0.0, 0.35, 0.0), max_velocity=(0.0, 0.8, 0.0),
                min_start_size=0.35, max_start_size=0.65,
                min_start_rotation=-0.2, max_start_rotation=0.2,
                material_name=FLAME,
                size_over_life=float_curve((0.0, 0.3), (0.25, 1.0), (0.7, 0.85), (1.0, 0.3)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(CANDLE_HOT, 0.0)),
                    (0.15, rgba(CANDLE_HOT, 0.9)),
                    (0.55, rgba(CANDLE_GOLD, 0.8)),
                    (1.00, rgba(AMBER, 0.0))))
    embers = _e(name="Wax Embers", loop=True, duration=1.0, spawn_rate=10.0,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, POOL_RADIUS),
                min_lifetime=0.8, max_lifetime=1.2,
                min_velocity=(-0.15, 0.7, -0.15), max_velocity=(0.15, 1.5, 0.15),
                min_start_size=0.05, max_start_size=0.09,
                gravity=(0.0, 0.3, 0.0), noise_amplitude=1.2, noise_frequency=1.5,
                render_mode=hpar.RENDER_STRETCHED, length_scale=2.5,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
                color_over_lifetime=_fade(EMBER, 0.95, peak=0.12, hold=0.6, end=AMBER))
    smoke = _e(name="Wax Smoke", loop=True, duration=1.0, spawn_rate=4.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, POOL_RADIUS * 0.6),
               min_lifetime=1.4, max_lifetime=1.8,
               min_start_size=0.6, max_start_size=0.9,
               min_angular_velocity=-0.6, max_angular_velocity=0.6,
               gravity=(0.0, 1.2, 0.0), noise_amplitude=0.8, noise_frequency=1.0,
               size_over_life=float_curve((0.0, 0.5), (1.0, 1.7)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(INCENSE, 0.0)),
                   (0.30, rgba(INCENSE, 0.16)),
                   (1.00, rgba(INCENSE, 0.0))))
    return oswin.ParticleSystem(emitters=[puddle, rim, smoke, flames, embers])


EFFECTS = [
    (wax_pool, "Candlebearer_WaxPool.hpar"),
]


if __name__ == "__main__":
    for build, filename in EFFECTS:
        write(build(), filename)
