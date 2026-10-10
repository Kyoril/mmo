# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Effects of the Hollow Choir's loot procs. Run from the repo root::

    py -3 tools/particle_gen/recipes/hollow_choir_loot.py

Writes two systems into ``data/client/Particles/HollowChoir/``:

==========================  ==========  ==========  ==========================================
File                        Attach      Lifetime    Kit
==========================  ==========  ==========  ==========================================
Loot_CoffinRotHit.hpar      node        one-shot    Coffin Rot IMPACT (Coffin Nail Dagger proc)
Loot_CoffinRotAura.hpar     node        looping     Coffin Rot AURA_IDLE on the victim (9 s DoT)
==========================  ==========  ==========  ==========================================

Art direction: grave rot driven in by a coffin nail. Sickly bone-green over a deep shadow
violet, so it reads as shadow damage yet stays apart from Mereth's spectral teal and Veyr's
magenta. The hit is a small dark burst at the waist with a few green splinters; the aura is
quiet (it sits on an enemy for 9 s, often on several at once): a thin rot haze curling up the
legs and a handful of sinking spores. Helpers and placement tricks come from the Mereth recipe.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hollow_choir_mereth import _e, _fade, _loop, write  # noqa: E402
from mage_common import RING, STAR, Burst, ParticleSystem, color_curve, float_curve, hpar, rgba  # noqa: E402

ROT_HOT = (0.80, 1.00, 0.62)     # splinter heads
ROT_GREEN = (0.46, 0.78, 0.30)   # body of the rot
BILE = (0.30, 0.48, 0.16)
SHADOW = (0.24, 0.12, 0.38)      # shadow violet under the green
DARK = (0.05, 0.03, 0.08)

WAIST = 1.0


def coffin_rot_hit():
    """~0.7 s: a dark puff at the waist, green splinters thrown out of it, a small ring."""
    puff = _e(name="Rot Puff", bursts=[Burst(0.0, 6)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.3, 0.25),
              min_velocity=(0.0, 0.2, 0.0), max_velocity=(0.0, 0.6, 0.0),
              min_start_speed=0.6, max_start_speed=1.2, drag=2.5,
              min_lifetime=0.55, max_lifetime=0.7,
              min_start_size=0.55, max_start_size=0.85,
              min_angular_velocity=-1.0, max_angular_velocity=1.0,
              size_over_life=float_curve((0.0, 0.6), (1.0, 1.3)),
              color_over_lifetime=_fade(SHADOW, 0.6, peak=0.12, end=ROT_GREEN))
    splinters = _e(name="Rot Splinters", bursts=[Burst(0.0, 9)],
                   shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.3, 0.15),
                   min_velocity=(-2.2, 0.4, -2.2), max_velocity=(2.2, 1.8, 2.2),
                   gravity=(0.0, -3.0, 0.0), drag=1.5,
                   min_lifetime=0.4, max_lifetime=0.6,
                   min_start_size=0.10, max_start_size=0.18,
                   render_mode=hpar.RENDER_STRETCHED, length_scale=4.0, material_name=STAR,
                   size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                   color_over_lifetime=_fade(ROT_HOT, 0.9, peak=0.1, end=ROT_GREEN))
    ring = _e(name="Rot Ring", bursts=[Burst(0.0, 1)],
              min_lifetime=0.5, max_lifetime=0.5,
              min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
              min_start_size=0.6, max_start_size=0.6,
              render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
              size_over_life=float_curve((0.0, 1.0), (1.0, 2.6)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ROT_GREEN, 0.0)),
                  (0.12, rgba(ROT_GREEN, 0.6)),
                  (1.00, rgba(SHADOW, 0.0))))
    return ParticleSystem(emitters=[ring, puff, splinters])


def coffin_rot_aura():
    """Quiet loop: rot haze curling up the legs and a few sinking green spores. ~20 live."""
    haze = _loop(_e(name="Rot Haze",
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.2, 0.4),
                    min_velocity=(0.0, 0.5, 0.0), max_velocity=(0.0, 0.9, 0.0),
                    gravity=(0.0, 0.4, 0.0), orbital_speed=1.1, drag=0.5,
                    min_lifetime=1.8, max_lifetime=2.2,
                    min_start_size=0.55, max_start_size=0.85,
                    min_angular_velocity=-0.6, max_angular_velocity=0.6,
                    size_over_life=float_curve((0.0, 0.6), (1.0, 1.3)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(BILE, 0.0)),
                        (0.25, rgba(ROT_GREEN, 0.30)),
                        (0.65, rgba(SHADOW, 0.24)),
                        (1.00, rgba(DARK, 0.0)))), rate=7.0)
    spores = _loop(_e(name="Rot Spores",
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.5, 0.4),
                      min_velocity=(0.0, -0.5, 0.0), max_velocity=(0.0, -0.2, 0.0),
                      min_lifetime=1.2, max_lifetime=1.6,
                      min_start_size=0.08, max_start_size=0.14,
                      min_angular_velocity=-2.0, max_angular_velocity=2.0,
                      material_name=STAR,
                      size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 0.5)),
                      color_over_lifetime=_fade(ROT_HOT, 0.85, peak=0.3, end=ROT_GREEN)), rate=6.0)
    system = ParticleSystem(emitters=[haze, spores])
    for e in system.emitters:
        e.warmup_time = 1.5
    return system


EFFECTS = [
    (coffin_rot_hit, "Loot_CoffinRotHit.hpar"),
    (coffin_rot_aura, "Loot_CoffinRotAura.hpar"),
]


def main():
    only = set(sys.argv[1:])
    for build, filename in EFFECTS:
        if not only or filename in only or filename.split(".")[0] in only:
            write(build(), filename)


if __name__ == "__main__":
    main()
