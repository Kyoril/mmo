# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Weapon impact effects. Run from the repo root::

    python tools/particle_gen/recipes/warrior_impacts.py

These spawn on the *target*, attached to a bone, at the moment the server reports a hit.
Scale reference: a player capsule is ~1.8 units tall, so a 0.10 particle is a fist-sized
spark and a 0.8 ring covers a torso.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import ParticleSystem


def steel_impact():
    """Enchanted blade on armour. The cheapest effect in the set by design -- it fires on
    every warrior swing and on eight creature types -- so the arcane read has to come from
    three cheap emitters, not from volume."""
    return ParticleSystem(emitters=[
        wc.spark_burst("Arc Sparks", count=14, speed=5.0, colour=wc.ARC_STEEL,
                       size=0.08, lifetime=0.32, gravity=-8.0, drag=1.8, spread=1.1),
        wc.soft_flash("Arc Flash", size=0.32, colour=wc.HOT, alpha=0.45, lifetime=0.10),
        wc.ground_ring("Arc Pulse", start_size=0.22, end_size=0.75, colour=wc.ARC_STEEL,
                       alpha=0.30, lifetime=0.28),
    ])


def heavy_impact():
    """Execute and Shield Slam. The same language with real force behind it: a wider spark
    fan, an energy swirl thrown off the hit, kicked-up dust for ground contact, and a fast
    expanding ring."""
    return ParticleSystem(emitters=[
        wc.spark_burst("Heavy Sparks", count=30, speed=7.0, colour=wc.ARC_EMBER,
                       size=0.10, lifetime=0.42, gravity=-9.0, drag=1.4, spread=1.4),
        wc.soft_flash("Heavy Flash", size=0.55, colour=wc.HOT, alpha=0.55, lifetime=0.14),
        wc.energy_swirl("Heavy Arc", count=20, radius=0.32, colour=wc.ARC_GOLD,
                        alpha=0.34, size=0.13, lifetime=0.50, orbital=4.0, rise=0.7),
        wc.dust_cloud("Heavy Dust", count=12, spread=0.7, colour=wc.DUST,
                      alpha=0.24, size=0.35, lifetime=0.60, rise=0.35),
        wc.ground_ring("Heavy Ring", start_size=0.30, end_size=1.4, colour=wc.ARC_GOLD,
                       alpha=0.34, lifetime=0.40),
    ])


def blood_impact():
    """Rend and Crippling Strike. Stylized wound energy rather than gore: bright crimson
    motes arc down under gravity while a darker energy haze lingers where the cut was. The
    previous version used a smoke material and read as a dark cloud."""
    return ParticleSystem(emitters=[
        wc.spark_burst("Wound Spray", count=20, speed=3.6, colour=wc.ARC_BLOOD,
                       size=0.09, lifetime=0.50, gravity=-13.0, drag=0.4, spread=0.85),
        wc.soft_flash("Wound Flash", size=0.28, colour=wc.ARC_CRIMSON, alpha=0.40,
                      lifetime=0.10),
        wc.energy_swirl("Wound Arc", count=12, radius=0.20, colour=wc.ARC_BLOOD,
                        hot=wc.ARC_CRIMSON, alpha=0.32, size=0.09, lifetime=0.55,
                        orbital=2.4, rise=0.3),
        wc.dust_cloud("Wound Haze", count=8, spread=0.32, colour=wc.ARC_BLOOD,
                      alpha=0.20, size=0.20, lifetime=0.42, rise=0.12),
    ])


if __name__ == "__main__":
    wc.write(steel_impact(), "SteelImpact.hpar")
    wc.write(heavy_impact(), "HeavyImpact.hpar")
    wc.write(blood_impact(), "BloodImpact.hpar")
