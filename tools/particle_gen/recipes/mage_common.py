# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shared palette and helpers for the mage spell effects (``data/client/Particles/Mage/``).

Reuses the stylized-fantasy building blocks from ``warrior_common`` -- read its module
docstring first: there is **no additive blending and no bloom**, so magic is sold by
density at low alpha, saturated hue, and motion (orbit, rings, stretched streaks, stars).

Mage effects come in two lifetimes, and they follow different rules:

* **One-shot** (casts, impacts, novas): every emitter ``loop=False`` so
  ``ParticleSystem::IsFinished`` can auto-destroy the system.
* **Looping** (cast channels on ``hand_r``, projectile trails, aura-idle states): every
  emitter ``loop=True`` with a continuous ``spawn_rate``. These are torn down by the engine
  (cast end, projectile impact, aura removal), never by themselves. Keep their live
  particle count low -- Frost Armor's idle loop runs for 30 minutes on every mage in view.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import (  # noqa: F401  (re-exported for the school recipes)
    BEAM, GLOW, RING, STAR, HOT, Burst, Emitter, ParticleSystem, color_curve, float_curve,
    hpar, rgba, spark_burst, ground_ring, dust_cloud, soft_flash, energy_swirl)

# --- Palette -----------------------------------------------------------------------------
# Frost: white-hot core -> pale cyan -> saturated azure.
FROST_HOT = (0.92, 0.98, 1.00)
FROST_ICE = (0.55, 0.88, 1.00)
FROST_DEEP = (0.20, 0.55, 1.00)
FROST_MIST = (0.70, 0.85, 0.98)

# Fire: yellow-white core -> orange -> crimson; smoke stays a lifted warm grey.
FIRE_HOT = (1.00, 0.95, 0.70)
FIRE_ORANGE = (1.00, 0.55, 0.12)
FIRE_RED = (0.95, 0.22, 0.06)
FIRE_SMOKE = (0.32, 0.26, 0.24)

# Arcane: pink-white core -> violet -> magenta accent.
ARCANE_HOT = (0.98, 0.90, 1.00)
ARCANE_VIOLET = (0.66, 0.40, 1.00)
ARCANE_MAGENTA = (0.95, 0.40, 0.90)
ARCANE_BLUE = (0.45, 0.55, 1.00)

ROOT = wc.ROOT
OUT_DIR = os.path.join(ROOT, "data", "client", "Particles", "Mage")


def write(system, filename):
    """Write ``system`` to ``data/client/Particles/Mage/<filename>``."""
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))


def looping(emitter, rate, lifetime=None):
    """Turn a burst-style emitter into a continuous loop: ``loop=True``, no bursts, a
    steady ``spawn_rate``, and ``max_particles`` sized to ``rate * max_lifetime`` plus
    headroom so the cap never thins the effect out."""
    emitter.loop = True
    emitter.bursts = []
    emitter.spawn_rate = rate
    emitter.duration = 1.0
    if lifetime is not None:
        emitter.min_lifetime = lifetime * 0.7
        emitter.max_lifetime = lifetime
    emitter.max_particles = int(rate * emitter.max_lifetime * 1.3) + 4
    return emitter
