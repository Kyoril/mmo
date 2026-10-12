# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shared palette and helpers for the scout ability effects (``data/client/Particles/Scout/``).

Read ``warrior_common``'s docstring first: there is **no additive blending and no bloom**,
so everything is density at low alpha, saturated hue, and motion. The scout adds three
sprites of its own (``make_sprites.py``):

* ``SLASH`` -- a crescent blade cut. Billboarded with a random roll it reads as a blade that
  just passed through, which a straight streak cannot: the curve is the swing.
* ``SMOKE`` -- a lumpy soft puff, for smoke bombs and shadow steps. Round glow blobs read as
  light, not as smoke.
* ``CROSSHAIR`` -- the hunter's mark hovering over a marked target.

Identity: **quick, precise, dirty.** Cold silver steel and glints for the blades, a vivid
stylized crimson for wounds, charcoal-violet smoke for evasion, amber for the energy
abilities, gold stars for the stun. Effects are short and sharp -- a scout attacks every
second, so nothing may linger or fill the screen.

Effect lifetimes follow the mage rules (``mage_common``): one-shots have every emitter
``loop=False``; aura idles loop with a low live count and are torn down by aura removal.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import (  # noqa: F401  (re-exported for the scout recipes)
    BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar,
    rgba)

SLASH = "Particles/Particle_Slash.hmi"
SMOKE = "Particles/Particle_Smoke.hmi"
CROSSHAIR = "Particles/Particle_Crosshair.hmi"
SIGIL = "Particles/Particle_Sigil.hmi"
RAYS = "Particles/Particle_Rays.hmi"

# --- Palette -------------------------------------------------------------------------------
STEEL_HOT = (0.96, 0.98, 1.00)     # the cutting edge
STEEL = (0.68, 0.82, 0.98)         # cold silver-blue blade energy
BLOOD = (0.86, 0.06, 0.10)         # stylized wound, brighter than real blood
BLOOD_DARK = (0.48, 0.02, 0.06)
SHADOW = (0.30, 0.27, 0.40)        # charcoal-violet smoke, lifted so it reads on dark ground
SHADOW_HOT = (0.66, 0.60, 0.82)
ENERGY_HOT = (1.00, 0.97, 0.72)    # the energy bar's yellow, pushed
ENERGY = (1.00, 0.76, 0.18)
MARK_HOT = (1.00, 0.78, 0.62)
MARK = (1.00, 0.18, 0.14)          # predator crimson, distinct from the darker blood
STUN_HOT = (1.00, 0.97, 0.80)
STUN = (1.00, 0.84, 0.30)
WIND = (0.86, 0.93, 0.97)
DUST = (0.62, 0.56, 0.46)

ROOT = wc.ROOT
OUT_DIR = os.path.join(ROOT, "data", "client", "Particles", "Scout")


def write(system, filename):
    """Write ``system`` to ``data/client/Particles/Scout/<filename>``."""
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))


def e(**kw):
    """An emitter with the defaults every effect here wants: one-shot, local space, no
    gravity, no implicit upward velocity, a random sprite roll, and ``max_particles`` sized
    from its bursts and rate. Override anything by keyword."""
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
        lifetime = base.get("max_lifetime", 2.0)
        window = lifetime if base["loop"] else min(base.get("duration", 1.0), lifetime)
        rate = base["spawn_rate"] * window
        base["max_particles"] = int(bursts + rate * 1.3) + 6
    return Emitter(**base)


def looping(emitter, rate, lifetime=None):
    """Turn an emitter into a continuous loop with an honest particle cap."""
    emitter.loop = True
    emitter.bursts = []
    emitter.spawn_rate = rate
    emitter.duration = 1.0
    if lifetime is not None:
        emitter.min_lifetime = lifetime * 0.7
        emitter.max_lifetime = lifetime
    emitter.max_particles = int(rate * emitter.max_lifetime * 1.3) + 4
    return emitter


def lift(emitter, height, drag=4.0, hidden=0.3, spread=0.04):
    """Drag lift (see ``mage_arcane``): throw particles up so they come to rest at
    ``height`` above a feet origin, invisible during the flight. There is no per-emitter
    offset in the engine, so this is how a feet-rooted aura draws above the head."""
    v0 = height * drag / (1.0 - drag / 60.0)
    lo, hi = emitter.min_velocity, emitter.max_velocity
    emitter.min_velocity = (lo[0], v0 * (1.0 - spread), lo[2])
    emitter.max_velocity = (hi[0], v0 * (1.0 + spread), hi[2])
    emitter.drag = drag
    first = emitter.color_over_lifetime[0].color
    hidden_key = (first[0], first[1], first[2], 0.0)
    rest = [(hidden + k.time * (1.0 - hidden), tuple(k.color)) for k in emitter.color_over_lifetime]
    emitter.color_over_lifetime = color_curve((0.0, hidden_key), (hidden * 0.98, hidden_key), *rest)
    return emitter


def flash(name, size, colour, alpha=0.6, lifetime=0.14, delay=0.0, material=GLOW):
    """One brief soft onset blob."""
    return e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
             min_lifetime=lifetime, max_lifetime=lifetime,
             min_start_size=size, max_start_size=size, material_name=material,
             size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 1.25)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(colour, alpha)),
                 (0.40, rgba(colour, alpha * 0.55)),
                 (1.00, rgba(colour, 0.0))))


def glint(name, size, colour=STEEL, alpha=0.95, lifetime=0.30, delay=0.0, spin=5.0, hot=STEEL_HOT):
    """A single spinning four-point star: the 'ting' of light on a blade edge."""
    return e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
             min_lifetime=lifetime, max_lifetime=lifetime,
             min_start_size=size, max_start_size=size,
             min_start_rotation=0.0, max_start_rotation=0.8,
             min_angular_velocity=spin, max_angular_velocity=spin,
             material_name=STAR,
             size_over_life=float_curve((0.0, 0.2), (0.2, 1.0), (1.0, 0.3)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, 0.0)),
                 (0.12, rgba(hot, alpha)),
                 (0.55, rgba(colour, alpha * 0.7)),
                 (1.00, rgba(colour, 0.0))))


def slash_arc(name, size, colour=STEEL, hot=STEEL_HOT, alpha=0.85, lifetime=0.28, delay=0.0,
              roll=(0.0, 6.28), count=1, grow=1.25):
    """The crescent cut sprite. A short life and a size that keeps growing reads as the arc
    still travelling; hot white edge -> cold steel -> gone."""
    return e(name=name, start_delay=delay, bursts=[Burst(0.0, count)],
             min_lifetime=lifetime * 0.85, max_lifetime=lifetime,
             min_start_size=size * 0.9, max_start_size=size,
             min_start_rotation=roll[0], max_start_rotation=roll[1],
             material_name=SLASH,
             size_over_life=float_curve((0.0, 0.7), (0.25, 1.0), (1.0, grow)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, 0.0)),
                 (0.08, rgba(hot, alpha)),
                 (0.45, rgba(colour, alpha * 0.7)),
                 (1.00, rgba(colour, 0.0))))


def sparks(name, count, speed, colour=STEEL, hot=STEEL_HOT, size=0.08, lifetime=0.32,
           gravity=-7.0, drag=2.0, delay=0.0):
    """Steel sparks: stretched, thrown radially, killed by drag and gravity."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(0.08, 0.0, 0.0),
             min_velocity=(-0.4, 0.2, -0.4), max_velocity=(0.4, 1.4, 0.4),
             min_start_speed=speed * 0.5, max_start_speed=speed,
             min_lifetime=lifetime * 0.5, max_lifetime=lifetime,
             min_start_size=size * 0.6, max_start_size=size,
             gravity=(0.0, gravity, 0.0), drag=drag,
             render_mode=hpar.RENDER_STRETCHED, length_scale=6.0, material_name=BEAM,
             size_over_life=float_curve((0.0, 1.0), (1.0, 0.25)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, 1.0)),
                 (0.40, rgba(colour, 0.85)),
                 (1.00, rgba(colour, 0.0))))


def blood_spray(name, count, speed, size=0.10, lifetime=0.55, delay=0.0, alpha=0.9):
    """Stylized blood droplets: a quick spray that arcs down under gravity."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(0.10, 0.0, 0.0),
             min_velocity=(-0.8, -0.2, -0.8), max_velocity=(0.8, 1.0, 0.8),
             min_start_speed=speed * 0.4, max_start_speed=speed,
             min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
             min_start_size=size * 0.5, max_start_size=size,
             gravity=(0.0, -9.0, 0.0), drag=1.2,
             render_mode=hpar.RENDER_STRETCHED, length_scale=2.6, material_name=GLOW,
             size_over_life=float_curve((0.0, 1.0), (1.0, 0.55)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(BLOOD, alpha)),
                 (0.55, rgba(BLOOD, alpha * 0.8)),
                 (1.00, rgba(BLOOD_DARK, 0.0))))


def blood_mist(name, count, size=0.45, lifetime=0.45, alpha=0.32, delay=0.0):
    """A soft crimson puff behind the droplets so a wound reads at range."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(0.12, 0.0, 0.0),
             min_velocity=(-0.5, -0.1, -0.5), max_velocity=(0.5, 0.5, 0.5),
             min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
             min_start_size=size * 0.6, max_start_size=size,
             drag=2.5, material_name=SMOKE,
             size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(BLOOD, 0.0)),
                 (0.15, rgba(BLOOD, alpha)),
                 (1.00, rgba(BLOOD_DARK, 0.0))))


def ring(name, start, end, colour, alpha=0.45, lifetime=0.4, delay=0.0, horizontal=True,
         material=RING):
    """An expanding ring that holds its alpha through the growth (``ground_ring`` fades
    before it finishes expanding at large ratios)."""
    return e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
             min_lifetime=lifetime, max_lifetime=lifetime,
             min_start_size=start, max_start_size=start,
             min_velocity=(0.0, 0.04, 0.0) if horizontal else (0.0, 0.0, 0.0),
             max_velocity=(0.0, 0.04, 0.0) if horizontal else (0.0, 0.0, 0.0),
             render_mode=hpar.RENDER_HORIZONTAL if horizontal else hpar.RENDER_BILLBOARD,
             material_name=material,
             size_over_life=float_curve((0.0, 1.0), (0.35, 0.75 * end / start), (1.0, end / start)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(colour, 0.0)),
                 (0.10, rgba(colour, alpha)),
                 (0.65, rgba(colour, alpha * 0.7)),
                 (1.00, rgba(colour, 0.0))))


def smoke_puff(name, count, radius, size, lifetime, colour=SHADOW, alpha=0.30, rise=0.5,
               spread=1.2, delay=0.0, drag=2.2):
    """Billowing smoke: soft lumpy puffs pushed outward, slowed by drag, growing as they
    thin out."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
             min_velocity=(-spread, 0.0, -spread), max_velocity=(spread, rise, spread),
             min_start_speed=spread * 0.5, max_start_speed=spread * 1.2,
             min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
             min_start_size=size * 0.6, max_start_size=size,
             min_angular_velocity=-0.6, max_angular_velocity=0.6,
             gravity=(0.0, 0.15, 0.0), drag=drag, material_name=SMOKE,
             size_over_life=float_curve((0.0, 0.45), (0.3, 1.0), (1.0, 1.6)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(colour, 0.0)),
                 (0.12, rgba(colour, alpha)),
                 (0.55, rgba(colour, alpha * 0.7)),
                 (1.00, rgba(colour, 0.0))))
