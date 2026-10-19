# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shared palette and helpers for the acolyte spell effects (``data/client/Particles/Acolyte/``).

Read ``warrior_common``'s docstring first: there is **no additive blending and no bloom**, so
magic is sold by density at low alpha, saturated hue, and motion. Shadow magic has one extra
problem on top of that: *darkness* cannot glow at all. An all-black effect vanishes on a dark
forest floor and reads as dirt on a bright one. Every acolyte effect therefore pairs a **dark
body** (charcoal-violet smoke, the ``SMOKE`` sprite, alpha 0.3-0.5 -- this is the one place
the volumetric budget may run higher, because a dark layer occludes rather than brightens)
with a **saturated rim** of the school colour (violet, blight green or dread crimson) carried
by glow, wisp and star sprites. The dark layer gives the silhouette, the rim makes it magic.

The acolyte adds three sprites of its own (``make_sprites.py``):

* ``SKULL`` -- a scowling skull with hollow eyes, for fear, dread and curses. Billboards are
  never rolled (``max_start_rotation=0``) or the face tilts.
* ``WISP`` -- a soft head trailing an S-curved tail. Rolled billboards read as restless
  spirits curling off a body; ``RENDER_STRETCHED`` lays the tail along the flight.
* ``VOID_SIGIL`` -- a broken occult circle with a heptagram, for ground glyphs.

Identity per school:

==========  ==========================================  ===================================
School      Look                                        Used by
==========  ==========================================  ===================================
Shadow      violet rim on charcoal smoke, curling       Shadowbolt, Veil of Shadows, Umbral
            wisps, lilac stars                          Bloom, Gloaming Trance, Void Resurgence
Blight      acid green on olive-black miasma, wet       Corrupting Touch, Leeching Rot
            drips, bubbling motes
Dread       crimson on black, skulls, sigils            Fear, Embrace of Dread
==========  ==========================================  ===================================

Lifetimes follow the mage rules (``mage_common``): one-shots have every emitter ``loop=False``;
loops (cast channels, projectile trails, aura idles) keep a low live count and are torn down by
the engine (cast end, projectile impact, aura removal). Veil of Shadows idles for 30 minutes on
every acolyte in view, so its loop is the sparsest in the set.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import (  # noqa: F401  (re-exported for the acolyte recipes)
    BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar,
    rgba)

SMOKE = "Particles/Particle_Smoke.hmi"
SKULL = "Particles/Particle_Skull.hmi"
WISP = "Particles/Particle_Wisp.hmi"
VOID_SIGIL = "Particles/Particle_VoidSigil.hmi"

# --- Palette -------------------------------------------------------------------------------
VOID_HOT = (0.97, 0.80, 1.00)      # pale lilac core: the only near-white in the set
# In the engine (0.64, 0.30, 1.0) read as plain blue; violet needs red at ~0.8.
VOID = (0.80, 0.26, 1.00)          # saturated violet rim
VOID_DEEP = (0.50, 0.10, 0.74)
UMBRA = (0.20, 0.08, 0.26)         # charcoal-violet smoke body: dark, but not black
BLIGHT_HOT = (0.86, 1.00, 0.55)
BLIGHT = (0.52, 0.92, 0.20)        # acid green
BLIGHT_DEEP = (0.24, 0.50, 0.10)
MIASMA = (0.16, 0.21, 0.09)        # olive-black rot smoke
DREAD_HOT = (1.00, 0.70, 0.76)
DREAD = (0.94, 0.16, 0.30)         # crimson
DREAD_DEEP = (0.46, 0.04, 0.16)

ROOT = wc.ROOT
OUT_DIR = os.path.join(ROOT, "data", "client", "Particles", "Acolyte")


def write(system, filename):
    """Write ``system`` to ``data/client/Particles/Acolyte/<filename>``."""
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
        # A rate emitter can have ``rate * max_lifetime`` particles alive at once.
        rate = base["spawn_rate"] * lifetime
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
    """Drag lift (see ``mage_arcane``): throw particles up so they come to rest at ``height``
    above a feet origin, invisible during the flight. There is no per-emitter offset, so this
    is how a feet-rooted aura draws over the head."""
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


def fade(colour, alpha, peak=0.12, end=None, hold=0.55):
    """In from zero, hold, out to zero; optionally shifting hue to ``end`` on the way out."""
    end = end or colour
    return color_curve(
        (0.00, rgba(colour, 0.0)),
        (peak, rgba(colour, alpha)),
        (hold, rgba(end, alpha * 0.7)),
        (1.00, rgba(end, 0.0)))


def flash(name, size, colour, alpha=0.6, lifetime=0.16, delay=0.0, material=GLOW):
    """One brief soft onset blob."""
    return e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
             min_lifetime=lifetime, max_lifetime=lifetime,
             min_start_size=size, max_start_size=size, material_name=material,
             size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 1.25)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(colour, alpha)),
                 (0.40, rgba(colour, alpha * 0.55)),
                 (1.00, rgba(colour, 0.0))))


def dark_smoke(name, count, radius, size, lifetime, colour=UMBRA, alpha=0.42, speed=1.2,
               rise=0.4, delay=0.0, drag=2.6, space=hpar.SIM_WORLD):
    """The dark body: lumpy smoke puffs pushed out, slowed by drag, swelling as they thin."""
    return e(name=name, simulation_space=space, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
             min_velocity=(-0.2, 0.0, -0.2), max_velocity=(0.2, rise, 0.2),
             min_start_speed=speed * 0.4, max_start_speed=speed,
             min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
             min_start_size=size * 0.6, max_start_size=size,
             min_angular_velocity=-0.8, max_angular_velocity=0.8,
             gravity=(0.0, 0.2, 0.0), drag=drag, material_name=SMOKE,
             size_over_life=float_curve((0.0, 0.45), (0.3, 1.0), (1.0, 1.55)),
             color_over_lifetime=fade(colour, alpha, peak=0.10, hold=0.5))


def wisps(name, count, speed, size, lifetime, hot=VOID_HOT, colour=VOID, alpha=0.55,
          radius=0.15, delay=0.0, drag=2.0, rise=0.3, orbital=0.0, spin=2.5,
          space=hpar.SIM_WORLD, gravity=0.0):
    """Curling spirits flung off a centre: rolled wisp billboards that spin as they go."""
    return e(name=name, simulation_space=space, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
             min_velocity=(-0.2, -0.1, -0.2), max_velocity=(0.2, rise, 0.2),
             min_start_speed=speed * 0.5, max_start_speed=speed,
             min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
             min_start_size=size * 0.6, max_start_size=size,
             min_angular_velocity=-spin, max_angular_velocity=spin,
             gravity=(0.0, gravity, 0.0), drag=drag, orbital_speed=orbital,
             material_name=WISP,
             size_over_life=float_curve((0.0, 0.4), (0.25, 1.0), (1.0, 1.2)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, 0.0)),
                 (0.12, rgba(hot, alpha)),
                 (0.45, rgba(colour, alpha * 0.8)),
                 (1.00, rgba(colour, 0.0))))


def streaks(name, count, speed, size, lifetime, hot=VOID_HOT, colour=VOID, alpha=0.8,
            radius=0.12, delay=0.0, drag=2.4, gravity=0.0, length=6.0, up=0.6):
    """Bright stretched shards thrown radially: the crack of a detonation."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
             min_velocity=(-0.3, -0.2, -0.3), max_velocity=(0.3, up, 0.3),
             min_start_speed=speed * 0.5, max_start_speed=speed,
             min_lifetime=lifetime * 0.5, max_lifetime=lifetime,
             min_start_size=size * 0.6, max_start_size=size,
             gravity=(0.0, gravity, 0.0), drag=drag,
             render_mode=hpar.RENDER_STRETCHED, length_scale=length, material_name=BEAM,
             size_over_life=float_curve((0.0, 1.0), (1.0, 0.25)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, alpha)),
                 (0.35, rgba(colour, alpha * 0.85)),
                 (1.00, rgba(colour, 0.0))))


def stars(name, count, size, lifetime, hot=VOID_HOT, colour=VOID, alpha=0.85, radius=0.4,
          speed=0.8, delay=0.0, drag=2.0, rise=0.5, spin=4.0, space=hpar.SIM_WORLD):
    """Twinkling four-point stars: the glitter that marks something as magic."""
    return e(name=name, simulation_space=space, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE, shape_extents=(radius, 0.0, 0.0),
             min_velocity=(-0.2, 0.0, -0.2), max_velocity=(0.2, rise, 0.2),
             min_start_speed=speed * 0.3, max_start_speed=speed,
             min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
             min_start_size=size * 0.5, max_start_size=size,
             min_angular_velocity=-spin, max_angular_velocity=spin,
             drag=drag, material_name=STAR,
             size_over_life=float_curve((0.0, 0.2), (0.2, 1.0), (1.0, 0.3)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, 0.0)),
                 (0.12, rgba(hot, alpha)),
                 (0.55, rgba(colour, alpha * 0.7)),
                 (1.00, rgba(colour, 0.0))))


def ring(name, start, end, colour, alpha=0.5, lifetime=0.5, delay=0.0, horizontal=True,
         material=RING, hold=0.65):
    """An expanding ring that holds its alpha through the growth."""
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
                 (hold, rgba(colour, alpha * 0.7)),
                 (1.00, rgba(colour, 0.0))))


def sigil(name, size, colour, alpha=0.6, lifetime=1.2, delay=0.0, spin=0.6, grow=1.15):
    """The void sigil drawn on the ground, slowly turning: fades in, holds, fades out."""
    return e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)],
             min_lifetime=lifetime, max_lifetime=lifetime,
             min_start_size=size, max_start_size=size,
             min_angular_velocity=spin, max_angular_velocity=spin,
             min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.02, 0.0),
             render_mode=hpar.RENDER_HORIZONTAL, material_name=VOID_SIGIL,
             size_over_life=float_curve((0.0, 0.8), (0.2, 1.0), (1.0, grow)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(colour, 0.0)),
                 (0.15, rgba(colour, alpha)),
                 (0.70, rgba(colour, alpha * 0.8)),
                 (1.00, rgba(colour, 0.0))))


def skull(name, size, colour, hot, alpha=0.75, lifetime=0.9, delay=0.0, rise=0.6,
          count=1, radius=0.0, grow=1.35, drag=1.5):
    """A spectral skull that looms up and dissolves. Never rolled: a tilted skull reads as
    a falling prop, not a face."""
    return e(name=name, simulation_space=hpar.SIM_WORLD, start_delay=delay,
             bursts=[Burst(0.0, count)],
             shape=hpar.SHAPE_SPHERE if radius > 0 else hpar.SHAPE_POINT,
             shape_extents=(radius, 0.0, 0.0),
             min_velocity=(0.0, rise * 0.8, 0.0), max_velocity=(0.0, rise, 0.0),
             min_start_rotation=0.0, max_start_rotation=0.0,
             min_lifetime=lifetime * 0.9, max_lifetime=lifetime,
             min_start_size=size * 0.9, max_start_size=size, drag=drag,
             material_name=SKULL,
             size_over_life=float_curve((0.0, 0.55), (0.25, 1.0), (1.0, grow)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(hot, 0.0)),
                 (0.15, rgba(hot, alpha)),
                 (0.50, rgba(colour, alpha * 0.75)),
                 (1.00, rgba(colour, 0.0))))
