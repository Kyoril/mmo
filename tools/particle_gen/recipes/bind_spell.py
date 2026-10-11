# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Bind (spell 17, "soul binding" at an innkeeper-style binder such as Aralin the Kindling).
Run from the repo root::

    py -3 tools/particle_gen/recipes/bind_spell.py

Writes ``data/client/Particles/Bind/BindCast.hpar``.

Art direction: a hearth fire taking hold. A warm ember sigil unfolds under the caster, a ring
*contracts* onto them (being drawn in and anchored, the opposite of every outward shockwave
the other abilities use), light pours down onto the spot, then embers and soul-blue motes lift
away. One-shot, caster ROOT.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cleric_common as cc  # noqa: E402
from cleric_auras import bloom_sigil, column_flash, fast_ring, ground_motes  # noqa: E402
from cleric_common import ParticleSystem, hpar  # noqa: E402

EMBER = (1.00, 0.66, 0.26)
HEARTH_GOLD = (1.00, 0.84, 0.46)
HOT = (1.00, 0.95, 0.78)
SOUL_BLUE = (0.55, 0.86, 1.00)

OUT_DIR = os.path.join(cc.ROOT, "data", "client", "Particles", "Bind")
LOOPING_EFFECTS = set()


def bind_cast():
    return ParticleSystem(emitters=[
        bloom_sigil("Hearth Sigil", size=4.2, colour=EMBER, alpha=0.62, lifetime=2.0, spin=0.5),
        bloom_sigil("Soul Sigil", size=2.2, colour=SOUL_BLUE, alpha=0.45, lifetime=1.7,
                    spin=-1.0, delay=0.1),
        fast_ring("Binding Ring", start_size=7.0, end_size=0.9, colour=HEARTH_GOLD,
                  alpha=0.65, lifetime=0.8),
        fast_ring("Anchor Ring", start_size=4.5, end_size=0.7, colour=SOUL_BLUE,
                  alpha=0.5, lifetime=0.7, delay=0.25),
        cc.light_shaft("Hearth Light", colour=HEARTH_GOLD, hot=HOT, height=6.5, radius=0.5,
                       count=44, alpha=0.38, delay=0.45),
        column_flash("Rising Embers", colour=EMBER, hot=HOT, count=22, alpha=0.30, delay=0.7),
        ground_motes("Ember Motes", colour=EMBER, hot=HOT, count=36, radius=1.6, rise=2.4,
                     alpha=0.75, delay=0.6),
        ground_motes("Soul Motes", colour=SOUL_BLUE, hot=HOT, count=20, radius=0.9, rise=2.8,
                     alpha=0.7, delay=0.8),
    ])


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "BindCast.hpar")
    hpar.save(bind_cast(), path)
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))
