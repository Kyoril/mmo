# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Human racial effects. Run from the repo root::

    py -3 tools/particle_gen/recipes/human_racials.py

Writes into ``data/client/Particles/Human/``.

Art direction
-------------
*Call of the Watch* is a call to duty, not a miracle. Warm gold with a steel-blue accent
(the Watch and the Crown); no sigils or holy rays, which belong to the cleric. The activation
is a horn-blast shockwave: a bright onset flash at chest height, a gold ring and a steel
ring racing outward over the 30 yd party radius, and banner-like gold sparks rising around
the caster. The apply effect plays on every party member in range: a small gold ring at the
feet and a few rising motes, low alpha.

Both effects are one-shot; nothing here loops (``LOOPING_EFFECTS`` is empty).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cleric_common as cc  # noqa: E402
from cleric_auras import column_flash, fast_ring, ground_motes  # noqa: E402
from cleric_common import ParticleSystem, hpar  # noqa: E402

WATCH_GOLD = (1.00, 0.78, 0.30)
BANNER_GOLD = (1.00, 0.88, 0.52)
STEEL_BLUE = (0.55, 0.70, 0.95)
HOT = (1.00, 0.96, 0.82)

OUT_DIR = os.path.join(cc.ROOT, "data", "client", "Particles", "Human")
LOOPING_EFFECTS = set()


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))


def call_of_the_watch_activate():
    """Caster ROOT. Horn-blast shockwave plus rising banner sparks."""
    return ParticleSystem(emitters=[
        cc.soft_flash("Onset Flash", size=1.6, colour=HOT, alpha=0.85, lifetime=0.22),
        fast_ring("Gold Shockwave", start_size=0.8, end_size=12.0, colour=WATCH_GOLD,
                  alpha=0.6, lifetime=0.9),
        fast_ring("Steel Shockwave", start_size=0.6, end_size=8.0, colour=STEEL_BLUE,
                  alpha=0.45, lifetime=0.8, delay=0.12),
        column_flash("Rally Column", colour=BANNER_GOLD, hot=HOT, count=22, alpha=0.30),
        ground_motes("Banner Sparks", colour=WATCH_GOLD, hot=HOT, count=40, radius=2.2,
                     rise=2.6, alpha=0.75),
        cc.spark_burst("Steel Sparks", count=18, speed=3.5, colour=STEEL_BLUE, size=0.08,
                       lifetime=0.5, gravity=-3.0, drag=2.0),
    ])


def call_of_the_watch_apply():
    """Recipient ROOT, every party member in range. Small, low alpha."""
    return ParticleSystem(emitters=[
        fast_ring("Apply Ring", start_size=0.4, end_size=1.9, colour=WATCH_GOLD,
                  alpha=0.7, lifetime=0.55),
        cc.rising_motes("Apply Motes", colour=BANNER_GOLD, hot=HOT, count=12, radius=0.4,
                        rise=1.7, size=0.20, lifetime=0.7, alpha=0.65, orbital=1.0,
                        duration=0.15),
    ])


if __name__ == "__main__":
    write(call_of_the_watch_activate(), "CallOfTheWatchActivate.hpar")
    write(call_of_the_watch_apply(), "CallOfTheWatchApply.hpar")
