# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Arcane school mage effects. Run from the repo root::

    py -3.14 tools/particle_gen/recipes/mage_arcane.py

Writes into ``data/client/Particles/Mage/``.

Art direction: **stylized high-fantasy arcane** with WoW-like readability -- bright, elegant
and scholarly. Pink-white hot cores, saturated violet bodies, magenta accents, glittering
four-point stars, orbiting motes and clean expanding rings. Deliberately distinct from the
warrior's dark "dread" violet (``warrior_abilities.dread_burst``): that one presses low and
settles; arcane rises, orbits and sparkles.

Read ``warrior_common``'s docstring first -- there is no additive blending and no bloom, so
everything here is density at low alpha (volumetric peaks 0.2-0.45), saturated hue, and
motion. Single short-lived particles (onset flashes, a single big star) may run brighter.

Two tricks in here that are not obvious from the engine fields:

* **There is no per-emitter spawn offset.** Effects rooted at the feet that need particles at
  head or chest height use one of two lifts:

  - *Drag lift* (``_lift``): a fast upward start velocity killed by drag. The particle stops
    at roughly ``v0 / drag`` above the origin (a few percent frame-rate dependent with the
    engine's discrete ``v *= 1 - drag*dt`` damping, which is why drag stays <= ~5). The
    colour curve holds alpha at zero for the flight, so what you see is a particle that
    simply appears at height.
  - *Attractor lift*: a strong ``attractor_position`` with high drag pins particles to a
    point within a few frames (the attractor force is normalized, so they jitter by roughly
    ``strength * dt^2`` around it -- invisible on a soft flash). Used for the manastone
    flash in front of the chest.

* **Orbital swirl rotates positions, not velocities**, so a stretched (velocity-aligned)
  particle in an orbit points the wrong way. Orbiting layers here are billboards (glow or
  star sprites); stretched beams are only used for straight radial / vertical streaks.

Local forward is +X (``GameUnitC::GetForwardVector``), which is where the manastone forms.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mage_common as mc
from mage_common import (
    ARCANE_BLUE, ARCANE_HOT, ARCANE_MAGENTA, ARCANE_VIOLET,
    BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar,
    looping, rgba)
from warrior_abilities import _fast_ring

# A cold, crystalline blue-violet for the intellect buff and the manastone.
ARCANE_CRYSTAL = (0.62, 0.72, 1.00)
ARCANE_PINK = (1.00, 0.62, 0.95)


def _e(**kw):
    """An emitter with the defaults every effect here wants: one-shot, no gravity, no
    implicit upward velocity, a random sprite roll. Override anything by keyword."""
    base = dict(
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.10,
        spawn_rate=0.0,
        shape=hpar.SHAPE_POINT,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_rotation=0.0, max_start_rotation=6.28,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
    )
    base.update(kw)
    if "max_particles" not in kw:
        bursts = sum(b.count for b in base.get("bursts", []))
        rate = base["spawn_rate"] * base.get("max_lifetime", 2.0)
        base["max_particles"] = int(bursts + rate * 1.3) + 6
    return Emitter(**base)


def _flash(name, size, colour, alpha=0.65, lifetime=0.16, delay=0.0, material=GLOW):
    return _e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
              min_lifetime=lifetime, max_lifetime=lifetime,
              min_start_size=size, max_start_size=size,
              material_name=material,
              size_over_life=float_curve((0.0, 0.55), (0.3, 1.0), (1.0, 1.25)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(colour, alpha)),
                  (0.35, rgba(colour, alpha * 0.6)),
                  (1.00, rgba(colour, 0.0))))


def _twinkle_star(name, size, colour, alpha=0.9, lifetime=0.35, delay=0.0, spin=4.0):
    """One big spinning four-point star -- the 'glint' that sells a sharp arcane moment."""
    return _e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
              min_lifetime=lifetime, max_lifetime=lifetime,
              min_start_size=size, max_start_size=size,
              min_start_rotation=0.0, max_start_rotation=0.0,
              min_angular_velocity=spin, max_angular_velocity=spin,
              material_name=STAR,
              size_over_life=float_curve((0.0, 0.2), (0.25, 1.0), (1.0, 0.35)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ARCANE_HOT, 0.0)),
                  (0.15, rgba(ARCANE_HOT, alpha)),
                  (0.55, rgba(colour, alpha * 0.7)),
                  (1.00, rgba(colour, 0.0))))


def _lift(emitter, height, drag=4.0, hidden=0.3):
    """Drag lift (see module docstring): throw the particles up so they come to rest at
    ``height`` and keep the colour curve invisible for the first ``hidden`` of their life.
    Keeps the velocity box's x/z spread, replaces its y."""
    v0 = height * drag / (1.0 - drag / 60.0)   # discrete-damping correction at 60 fps
    lo, hi = emitter.min_velocity, emitter.max_velocity
    emitter.min_velocity = (lo[0], v0 * 0.94, lo[2])
    emitter.max_velocity = (hi[0], v0 * 1.06, hi[2])
    emitter.drag = drag
    keys = [(0.0, (k.color[0], k.color[1], k.color[2], 0.0)) for k in emitter.color_over_lifetime[:1]]
    rest = [(hidden + k.time * (1.0 - hidden), tuple(k.color)) for k in emitter.color_over_lifetime]
    emitter.color_over_lifetime = color_curve(keys[0], (hidden * 0.98, keys[0][1]), *rest)
    return emitter


# =========================================================================================
# 1. Arcane Pulse -- caster-centred 8-unit AoE nova
# =========================================================================================

def arcane_pulse():
    ring = mc.ground_ring("Pulse Ring", start_size=1.6, end_size=24.0, colour=ARCANE_MAGENTA,
                          alpha=0.55, lifetime=0.80)
    _fast_ring(ring, growth_frac=0.40, growth_hold=0.86, hold_alpha_frac=0.75)
    ring2 = mc.ground_ring("Pulse Ring Echo", start_size=1.4, end_size=20.0, colour=ARCANE_VIOLET,
                           alpha=0.45, lifetime=0.75, delay=0.12)
    _fast_ring(ring2, growth_frac=0.40, growth_hold=0.86, hold_alpha_frac=0.7)
    ring_core = mc.ground_ring("Pulse Ring Core", start_size=0.8, end_size=6.0, colour=ARCANE_HOT,
                               alpha=0.50, lifetime=0.45)
    _fast_ring(ring_core, growth_frac=0.35, growth_hold=0.8, hold_alpha_frac=0.6)

    disc = _e(name="Pulse Ground Glow", bursts=[Burst(0.0, 1)],
              min_lifetime=0.55, max_lifetime=0.55,
              min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
              min_start_size=3.0, max_start_size=3.0,
              render_mode=hpar.RENDER_HORIZONTAL,
              size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.8)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ARCANE_PINK, 0.0)),
                  (0.10, rgba(ARCANE_PINK, 0.45)),
                  (1.00, rgba(ARCANE_VIOLET, 0.0))))

    # Streaks and stars carried outward along the ground by radial acceleration: a flat box
    # at the feet means "away from the centre" is horizontal, and drag caps them at a
    # terminal speed that keeps pace with the ring.
    streaks = _e(name="Pulse Streaks", simulation_space=hpar.SIM_WORLD,
                 bursts=[Burst(0.0, 48)],
                 shape=hpar.SHAPE_BOX, shape_extents=(0.9, 0.3, 0.9),
                 min_velocity=(0.0, 0.2, 0.0), max_velocity=(0.0, 0.9, 0.0),
                 min_lifetime=0.45, max_lifetime=0.75,
                 min_start_size=0.22, max_start_size=0.36,
                 radial_acceleration=95.0, drag=5.0,
                 render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
                 material_name=BEAM,
                 size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                 color_over_lifetime=color_curve(
                     (0.00, rgba(ARCANE_HOT, 0.0)),
                     (0.08, rgba(ARCANE_HOT, 0.85)),
                     (0.45, rgba(ARCANE_MAGENTA, 0.6)),
                     (1.00, rgba(ARCANE_VIOLET, 0.0))))
    stars = _e(name="Pulse Stars", simulation_space=hpar.SIM_WORLD,
               bursts=[Burst(0.0, 22), Burst(0.08, 14)],
               shape=hpar.SHAPE_BOX, shape_extents=(1.2, 0.6, 1.2),
               min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 1.6, 0.0),
               min_lifetime=0.6, max_lifetime=1.0,
               min_start_size=0.40, max_start_size=0.70,
               min_angular_velocity=-5.0, max_angular_velocity=5.0,
               radial_acceleration=90.0, drag=5.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.2, 1.0), (1.0, 0.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.0)),
                   (0.10, rgba(ARCANE_HOT, 0.95)),
                   (0.50, rgba(ARCANE_PINK, 0.7)),
                   (1.00, rgba(ARCANE_MAGENTA, 0.0))))
    flare = _e(name="Pulse Flare", bursts=[Burst(0.0, 26)],
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.35, 0.0, 0.0),
               min_velocity=(-0.4, 3.0, -0.4), max_velocity=(0.4, 6.5, 0.4),
               min_lifetime=0.35, max_lifetime=0.55, drag=2.0,
               min_start_size=0.2, max_start_size=0.32,
               render_mode=hpar.RENDER_STRETCHED, length_scale=7.0,
               material_name=BEAM,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.8)),
                   (0.5, rgba(ARCANE_MAGENTA, 0.5)),
                   (1.00, rgba(ARCANE_VIOLET, 0.0))))
    motes = _e(name="Pulse Motes", duration=0.5, spawn_rate=90.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 7.5),
               min_velocity=(-0.2, 0.6, -0.2), max_velocity=(0.2, 1.6, 0.2),
               min_lifetime=0.5, max_lifetime=0.75, start_delay=0.12,
               min_start_size=0.28, max_start_size=0.48,
               min_angular_velocity=-3.0, max_angular_velocity=3.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.5)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_PINK, 0.0)),
                   (0.25, rgba(ARCANE_PINK, 0.75)),
                   (1.00, rgba(ARCANE_VIOLET, 0.0))))
    return ParticleSystem(emitters=[
        disc, ring, ring2, ring_core, streaks, stars, flare, motes,
        _flash("Pulse Flash", 3.2, ARCANE_HOT, alpha=0.7, lifetime=0.2),
        _twinkle_star("Pulse Glint", 2.2, ARCANE_MAGENTA, alpha=0.85, lifetime=0.3, spin=3.0),
    ])


# =========================================================================================
# 2. Arcane Disruption -- cast flare on hand_r
# =========================================================================================

def arcane_disruption_cast():
    sparks = _e(name="Cast Sparks", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 14)],
                shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
                min_start_speed=2.0, max_start_speed=4.0,
                min_lifetime=0.18, max_lifetime=0.32, drag=5.0,
                min_start_size=0.06, max_start_size=0.1,
                render_mode=hpar.RENDER_STRETCHED, length_scale=5.0, material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(ARCANE_HOT, 1.0)),
                    (0.5, rgba(ARCANE_MAGENTA, 0.75)),
                    (1.00, rgba(ARCANE_VIOLET, 0.0))))
    stars = _e(name="Cast Stars", simulation_space=hpar.SIM_WORLD,
               bursts=[Burst(0.0, 8), Burst(0.06, 6)],
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
               min_start_speed=0.8, max_start_speed=2.0,
               min_lifetime=0.25, max_lifetime=0.4, drag=4.0,
               min_start_size=0.13, max_start_size=0.22,
               min_angular_velocity=-6.0, max_angular_velocity=6.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.25, 1.0), (1.0, 0.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.0)),
                   (0.12, rgba(ARCANE_HOT, 0.95)),
                   (1.00, rgba(ARCANE_MAGENTA, 0.0))))
    puff = _e(name="Cast Puff", bursts=[Burst(0.0, 5)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
              min_start_speed=0.2, max_start_speed=0.5,
              min_lifetime=0.3, max_lifetime=0.42,
              min_start_size=0.35, max_start_size=0.5, orbital_speed=4.0,
              size_over_life=float_curve((0.0, 0.5), (1.0, 1.3)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ARCANE_MAGENTA, 0.0)),
                  (0.15, rgba(ARCANE_MAGENTA, 0.38)),
                  (1.00, rgba(ARCANE_VIOLET, 0.0))))
    return ParticleSystem(emitters=[
        puff,
        _flash("Cast Flash", 0.8, ARCANE_HOT, alpha=0.75, lifetime=0.14),
        _twinkle_star("Cast Glint", 0.75, ARCANE_MAGENTA, alpha=0.95, lifetime=0.28, spin=7.0),
        sparks, stars,
    ])


# =========================================================================================
# 3. Arcane Disruption -- interrupt shatter on the target's head
# =========================================================================================

def arcane_disruption_impact():
    shards = _e(name="Shatter Shards", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 24)],
                shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
                min_start_speed=3.5, max_start_speed=6.5,
                min_lifetime=0.22, max_lifetime=0.4, drag=4.0,
                min_start_size=0.08, max_start_size=0.14,
                render_mode=hpar.RENDER_STRETCHED, length_scale=6.0, material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(ARCANE_HOT, 1.0)),
                    (0.45, rgba(ARCANE_MAGENTA, 0.8)),
                    (1.00, rgba(ARCANE_VIOLET, 0.0))))
    stars = _e(name="Shatter Stars", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 12)],
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
               min_start_speed=1.5, max_start_speed=3.0,
               min_lifetime=0.3, max_lifetime=0.5, drag=4.0,
               min_start_size=0.14, max_start_size=0.24,
               min_angular_velocity=-7.0, max_angular_velocity=7.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.2, 1.0), (1.0, 0.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.0)),
                   (0.1, rgba(ARCANE_HOT, 0.95)),
                   (1.00, rgba(ARCANE_MAGENTA, 0.0))))
    # The silence: a ring of motes that collapses back onto the head, swirling as it goes.
    collapse = _e(name="Silence Collapse", start_delay=0.12, bursts=[Burst(0.0, 32)],
                  shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 1.0),
                  min_lifetime=0.45, max_lifetime=0.6,
                  attractor_position=(0.0, 0.0, 0.0), attractor_strength=9.0, drag=1.5,
                  orbital_speed=5.0,
                  min_start_size=0.14, max_start_size=0.24,
                  size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(ARCANE_PINK, 0.0)),
                      (0.2, rgba(ARCANE_PINK, 0.85)),
                      (0.7, rgba(ARCANE_MAGENTA, 0.6)),
                      (1.00, rgba(ARCANE_VIOLET, 0.0))))
    ring = _e(name="Silence Ring", start_delay=0.1, bursts=[Burst(0.0, 1)],
              min_lifetime=0.45, max_lifetime=0.45,
              min_start_size=1.8, max_start_size=1.8,
              material_name=RING,
              size_over_life=float_curve((0.0, 1.0), (0.6, 0.35), (1.0, 0.15)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ARCANE_MAGENTA, 0.0)),
                  (0.15, rgba(ARCANE_MAGENTA, 0.6)),
                  (1.00, rgba(ARCANE_HOT, 0.0))))
    return ParticleSystem(emitters=[
        _flash("Shatter Flash", 1.3, ARCANE_MAGENTA, alpha=0.7, lifetime=0.18),
        _twinkle_star("Shatter Glint", 1.1, ARCANE_PINK, alpha=0.95, lifetime=0.3, spin=8.0),
        shards, stars, ring, collapse,
        _flash("Silence Seal", 0.8, ARCANE_MAGENTA, alpha=0.75, lifetime=0.22, delay=0.52),
    ])


# =========================================================================================
# 4. Arcane Intellect -- buff applied to a friendly target
# =========================================================================================

def arcane_intellect_burst():
    # A conical shell around the body (cone spawns on a circle that widens with height),
    # rising and orbiting: reads as motes spiralling up from the feet to the head.
    spiral = _e(name="Intellect Spiral", duration=0.55, spawn_rate=60.0,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.5, 0.55),
                min_velocity=(0.0, 1.3, 0.0), max_velocity=(0.0, 2.0, 0.0),
                min_lifetime=0.75, max_lifetime=1.0, orbital_speed=4.5,
                min_start_size=0.12, max_start_size=0.22,
                size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(ARCANE_CRYSTAL, 0.0)),
                    (0.2, rgba(ARCANE_HOT, 0.8)),
                    (0.6, rgba(ARCANE_BLUE, 0.6)),
                    (1.00, rgba(ARCANE_VIOLET, 0.0))))
    spiral_stars = _e(name="Intellect Spiral Stars", duration=0.55, spawn_rate=22.0,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.5, 0.55),
                      min_velocity=(0.0, 1.3, 0.0), max_velocity=(0.0, 2.0, 0.0),
                      min_lifetime=0.75, max_lifetime=1.0, orbital_speed=4.5,
                      min_start_size=0.16, max_start_size=0.28,
                      min_angular_velocity=-4.0, max_angular_velocity=4.0,
                      material_name=STAR,
                      size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.4)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(ARCANE_HOT, 0.0)),
                          (0.2, rgba(ARCANE_HOT, 0.9)),
                          (1.00, rgba(ARCANE_VIOLET, 0.0))))
    crown = _e(name="Intellect Crown Stars", start_delay=0.45, duration=0.35, spawn_rate=30.0,
               shape=hpar.SHAPE_BOX, shape_extents=(0.6, 0.0, 0.6),
               min_velocity=(-0.25, 0.0, -0.25), max_velocity=(0.25, 0.0, 0.25),
               min_lifetime=0.55, max_lifetime=0.8,
               min_start_size=0.16, max_start_size=0.32,
               min_angular_velocity=-5.0, max_angular_velocity=5.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.3)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.0)),
                   (0.25, rgba(ARCANE_HOT, 0.95)),
                   (0.6, rgba(ARCANE_CRYSTAL, 0.7)),
                   (1.00, rgba(ARCANE_BLUE, 0.0))))
    _lift(crown, height=1.8, drag=4.0, hidden=0.28)
    crown_glow = _e(name="Intellect Crown Glow", start_delay=0.45, bursts=[Burst(0.0, 1)],
                    min_lifetime=0.9, max_lifetime=0.9,
                    min_start_size=1.0, max_start_size=1.0,
                    size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 1.2)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(ARCANE_CRYSTAL, 0.0)),
                        (0.4, rgba(ARCANE_CRYSTAL, 0.4)),
                        (1.00, rgba(ARCANE_BLUE, 0.0))))
    _lift(crown_glow, height=1.8, drag=4.0, hidden=0.3)
    ring = mc.ground_ring("Intellect Ring", start_size=0.7, end_size=2.4, colour=ARCANE_BLUE,
                          alpha=0.45, lifetime=0.8)
    glow = mc.ground_ring("Intellect Ground Glow", start_size=1.2, end_size=1.8,
                          colour=ARCANE_CRYSTAL, alpha=0.3, lifetime=0.9)
    glow.material_name = GLOW
    return ParticleSystem(emitters=[glow, ring, spiral, spiral_stars, crown_glow, crown])


# =========================================================================================
# 5. Sleep -- applied on the target's head
# =========================================================================================

def sleep_apply():
    puff = _e(name="Sleep Puff", bursts=[Burst(0.0, 14)],
              shape=hpar.SHAPE_SPHERE, shape_extents=(0.25, 0.0, 0.0),
              min_start_speed=0.3, max_start_speed=0.8, drag=2.0,
              min_velocity=(0.0, 0.25, 0.0), max_velocity=(0.0, 0.5, 0.0),
              min_lifetime=0.6, max_lifetime=0.9, orbital_speed=1.2,
              min_start_size=0.45, max_start_size=0.75,
              min_angular_velocity=-0.6, max_angular_velocity=0.6,
              size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ARCANE_PINK, 0.0)),
                  (0.25, rgba(ARCANE_VIOLET, 0.32)),
                  (1.00, rgba(ARCANE_VIOLET, 0.0))))
    stars = _e(name="Sleep Drift Stars", bursts=[Burst(0.0, 4), Burst(0.15, 4)], duration=0.2,
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.35, 0.0, 0.0),
               min_velocity=(-0.15, 0.25, -0.15), max_velocity=(0.15, 0.6, 0.15),
               min_lifetime=0.8, max_lifetime=0.95, orbital_speed=0.9,
               min_start_size=0.2, max_start_size=0.3,
               min_angular_velocity=-1.5, max_angular_velocity=1.5,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.6)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.0)),
                   (0.3, rgba(ARCANE_PINK, 0.85)),
                   (1.00, rgba(ARCANE_VIOLET, 0.0))))
    return ParticleSystem(emitters=[
        _flash("Sleep Glow", 0.9, ARCANE_PINK, alpha=0.4, lifetime=0.4),
        puff, stars,
    ])


# =========================================================================================
# 6. Conjure Lesser Manastone -- completion on the caster
# =========================================================================================

STONE_POINT = (0.35, 1.05, 0.0)   # in front of the chest, between the hands (+X forward)


def manastone_conjure():
    gather = _e(name="Stone Gather", duration=0.55, spawn_rate=85.0,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.9, 1.2),
                min_velocity=(-0.3, 0.0, -0.3), max_velocity=(0.3, 0.4, 0.3),
                min_lifetime=0.45, max_lifetime=0.6,
                attractor_position=STONE_POINT, attractor_strength=14.0, drag=2.0,
                orbital_speed=2.5,
                min_start_size=0.16, max_start_size=0.28,
                size_over_life=float_curve((0.0, 0.6), (1.0, 0.3)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(ARCANE_VIOLET, 0.0)),
                    (0.3, rgba(ARCANE_PINK, 0.75)),
                    (0.8, rgba(ARCANE_HOT, 0.8)),
                    (1.00, rgba(ARCANE_CRYSTAL, 0.0))))

    def pinned(e):
        """Attractor lift onto the stone point (see module docstring)."""
        e.attractor_position = STONE_POINT
        e.attractor_strength = 260.0
        e.drag = 24.0
        return e

    core = pinned(_e(name="Stone Core", start_delay=0.1, duration=0.5, spawn_rate=24.0,
                     min_lifetime=0.4, max_lifetime=0.5,
                     min_start_size=0.25, max_start_size=0.4,
                     size_over_life=float_curve((0.0, 0.3), (1.0, 1.0)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(ARCANE_VIOLET, 0.0)),
                         (0.3, rgba(ARCANE_VIOLET, 0.0)),
                         (0.6, rgba(ARCANE_MAGENTA, 0.4)),
                         (1.00, rgba(ARCANE_CRYSTAL, 0.0)))))
    flash = pinned(_e(name="Stone Flash", start_delay=0.6, bursts=[Burst(0.0, 1)],
                      min_lifetime=0.4, max_lifetime=0.4,
                      min_start_size=1.3, max_start_size=1.3,
                      size_over_life=float_curve((0.0, 0.4), (0.35, 0.6), (0.5, 1.0), (1.0, 1.25)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(ARCANE_CRYSTAL, 0.0)),
                          (0.3, rgba(ARCANE_CRYSTAL, 0.0)),
                          (0.4, rgba(ARCANE_HOT, 0.8)),
                          (1.00, rgba(ARCANE_CRYSTAL, 0.0)))))
    glint = pinned(_e(name="Stone Glint", start_delay=0.6, bursts=[Burst(0.0, 1)],
                      min_lifetime=0.55, max_lifetime=0.55,
                      min_start_size=1.0, max_start_size=1.0,
                      min_start_rotation=0.0, max_start_rotation=0.0,
                      min_angular_velocity=5.0, max_angular_velocity=5.0,
                      material_name=STAR,
                      size_over_life=float_curve((0.0, 0.2), (0.3, 0.3), (0.45, 1.0), (1.0, 0.3)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(ARCANE_HOT, 0.0)),
                          (0.25, rgba(ARCANE_HOT, 0.0)),
                          (0.35, rgba(ARCANE_HOT, 0.95)),
                          (0.7, rgba(ARCANE_CRYSTAL, 0.7)),
                          (1.00, rgba(ARCANE_BLUE, 0.0)))))
    # Sparkle swarm: a weak pin (terminal speed strength/drag ~7 u/s, overshoot ~1) so
    # the stars fly in invisibly, then keep swinging through the stone point in a loose
    # glittering cloud instead of locking onto it.
    sparkle = _e(name="Stone Sparkle", start_delay=0.5, bursts=[Burst(0.0, 10), Burst(0.12, 8)],
                 duration=0.15,
                 shape=hpar.SHAPE_SPHERE, shape_extents=(0.3, 0.0, 0.0),
                 min_velocity=(-2.5, 0.5, -2.5), max_velocity=(2.5, 3.0, 2.5),
                 min_lifetime=0.75, max_lifetime=0.9,
                 attractor_position=STONE_POINT, attractor_strength=22.0, drag=3.0,
                 min_start_size=0.14, max_start_size=0.26,
                 min_angular_velocity=-6.0, max_angular_velocity=6.0,
                 material_name=STAR,
                 size_over_life=float_curve((0.0, 0.4), (0.35, 0.5), (0.5, 1.0), (1.0, 0.3)),
                 color_over_lifetime=color_curve(
                     (0.00, rgba(ARCANE_HOT, 0.0)),
                     (0.36, rgba(ARCANE_HOT, 0.0)),
                     (0.48, rgba(ARCANE_HOT, 0.95)),
                     (0.75, rgba(ARCANE_CRYSTAL, 0.7)),
                     (1.00, rgba(ARCANE_BLUE, 0.0))))
    return ParticleSystem(emitters=[gather, core, flash, glint, sparkle])


# =========================================================================================
# 7. Arcane hand channel -- looping on hand_r while casting
# =========================================================================================

def arcane_hand_channel():
    motes = looping(_e(name="Channel Motes", shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.3),
                       min_velocity=(-0.05, 0.05, -0.05), max_velocity=(0.05, 0.25, 0.05),
                       orbital_speed=6.0, warmup_time=0.6,
                       min_start_size=0.07, max_start_size=0.13,
                       size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.4)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(ARCANE_HOT, 0.0)),
                           (0.2, rgba(ARCANE_PINK, 0.85)),
                           (0.65, rgba(ARCANE_MAGENTA, 0.6)),
                           (1.00, rgba(ARCANE_VIOLET, 0.0)))),
                    rate=40.0, lifetime=0.6)
    glow = looping(_e(name="Channel Glow", shape=hpar.SHAPE_SPHERE, shape_extents=(0.05, 0.0, 0.0),
                      min_start_size=0.45, max_start_size=0.6, warmup_time=0.6,
                      min_angular_velocity=-1.0, max_angular_velocity=1.0,
                      size_over_life=float_curve((0.0, 0.7), (0.5, 1.0), (1.0, 0.8)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(ARCANE_MAGENTA, 0.0)),
                          (0.4, rgba(ARCANE_VIOLET, 0.28)),
                          (1.00, rgba(ARCANE_VIOLET, 0.0)))),
                   rate=7.0, lifetime=0.6)
    stars = looping(_e(name="Channel Stars", shape=hpar.SHAPE_SPHERE, shape_extents=(0.22, 0.0, 0.0),
                       min_velocity=(-0.05, 0.1, -0.05), max_velocity=(0.05, 0.3, 0.05),
                       orbital_speed=3.0, warmup_time=0.6,
                       min_start_size=0.10, max_start_size=0.18,
                       min_angular_velocity=-6.0, max_angular_velocity=6.0,
                       material_name=STAR,
                       size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.3)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(ARCANE_HOT, 0.0)),
                           (0.3, rgba(ARCANE_HOT, 0.9)),
                           (1.00, rgba(ARCANE_MAGENTA, 0.0)))),
                    rate=9.0, lifetime=0.55)
    return ParticleSystem(emitters=[glow, motes, stars])


# =========================================================================================
# 8. Sleep aura -- looping on a slept enemy (origin at the feet)
# =========================================================================================

def sleep_aura():
    motes = _e(name="Dream Motes", shape=hpar.SHAPE_BOX, shape_extents=(0.8, 0.0, 0.8),
               min_velocity=(-0.6, 0.0, -0.6), max_velocity=(0.6, 0.0, 0.6),
               gravity=(0.0, 1.2, 0.0), orbital_speed=0.5, warmup_time=3.0,
               min_start_size=0.10, max_start_size=0.18,
               size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 0.6)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_PINK, 0.0)),
                   (0.3, rgba(ARCANE_PINK, 0.6)),
                   (1.00, rgba(ARCANE_VIOLET, 0.0))))
    _lift(motes, height=1.95, drag=4.0, hidden=0.2)
    looping(motes, rate=5.0, lifetime=2.8)
    stars = _e(name="Dream Stars", shape=hpar.SHAPE_BOX, shape_extents=(0.7, 0.0, 0.7),
               min_velocity=(-0.5, 0.0, -0.5), max_velocity=(0.5, 0.0, 0.5),
               gravity=(0.0, 1.0, 0.0), orbital_speed=0.5, warmup_time=3.0,
               min_start_size=0.16, max_start_size=0.26,
               min_angular_velocity=-1.2, max_angular_velocity=1.2,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.4, 1.0), (1.0, 0.5)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(ARCANE_HOT, 0.0)),
                   (0.35, rgba(ARCANE_HOT, 0.85)),
                   (0.7, rgba(ARCANE_PINK, 0.55)),
                   (1.00, rgba(ARCANE_VIOLET, 0.0))))
    _lift(stars, height=2.0, drag=4.0, hidden=0.2)
    looping(stars, rate=2.5, lifetime=3.0)
    halo = _e(name="Dream Halo", min_start_size=0.85, max_start_size=0.95,
              min_angular_velocity=0.6, max_angular_velocity=0.8, warmup_time=3.0,
              render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
              size_over_life=float_curve((0.0, 0.8), (1.0, 1.15)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(ARCANE_VIOLET, 0.0)),
                  (0.5, rgba(ARCANE_MAGENTA, 0.35)),
                  (1.00, rgba(ARCANE_VIOLET, 0.0))))
    _lift(halo, height=2.0, drag=4.0, hidden=0.3)
    looping(halo, rate=1.0, lifetime=2.6)
    return ParticleSystem(emitters=[halo, motes, stars])


if __name__ == "__main__":
    mc.write(arcane_pulse(), "ArcanePulse.hpar")
    mc.write(arcane_disruption_cast(), "ArcaneDisruptionCast.hpar")
    mc.write(arcane_disruption_impact(), "ArcaneDisruptionImpact.hpar")
    mc.write(arcane_intellect_burst(), "ArcaneIntellectBurst.hpar")
    mc.write(sleep_apply(), "SleepApply.hpar")
    mc.write(manastone_conjure(), "ManastoneConjure.hpar")
    mc.write(arcane_hand_channel(), "ArcaneHandChannel.hpar")
    mc.write(sleep_aura(), "SleepAura.hpar")
