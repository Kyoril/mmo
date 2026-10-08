# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Sister Mereth, the Mourning Voice (Hollow Choir, boss 2). Run from the repo root::

    py -3 tools/particle_gen/recipes/hollow_choir_mereth.py

Writes six systems into ``data/client/Particles/HollowChoir/``:

==========================  ==========  ==========  ==========================================
File                        Attach      Lifetime    Kit
==========================  ==========  ==========  ==========================================
Mereth_LamentCast.hpar      node        one-shot    Lament CASTING (3 s interruptible cast)
Mereth_LamentRelease.hpar   node        one-shot    Lament CAST_SUCCEEDED
Mereth_VoicesCast.hpar      node        one-shot    Mourning Voices CASTING (2 s)
Mereth_ChorusVeil.hpar      node        looping     Mourning Chorus AURA_IDLE on Mereth
Mereth_SilentGround.hpar    ground      looping     Silent Place GROUND_ACTIVE (12 s, r 4.5)
Mereth_SilentPulse.hpar     node        one-shot    Silent Place pulse IMPACT (every 1 s)
==========================  ==========  ==========  ==========================================

"node" means: no ``attach_bone``. The origin is the unit's feet, y up. Every effect here is
built for that on purpose. A bone-attached emitter inherits the bone's rotation, so spawn
velocities would point wherever the bone happens to point. The chest-height parts are
placed by the two lifts below instead, which work the same on any rig.

Art direction: a ghostly precentor. Spectral pale teal and cyan with a mourning-silver
accent; motifs are wails (expanding sound rings), tears (small bright stretched droplets
that rise, because she is a ghost), veils (tall faint vertical strands) and the hush (a
dark indigo pool that swallows sound: ripples run *inward*, motes sink *down*).

There is no additive blending and no bloom (see ``warrior_common``). Volumetric layers
stay at peak alpha 0.15-0.45; rings, tears and single flashes run brighter. A dark colour
at moderate alpha *darkens* whatever is behind it, which is exactly what the hush pool
wants: in this renderer, darkness is the one thing alpha blending does for free.

Placement tricks (no per-emitter offset exists):

* **Attractor pin** (``_pin``): a point attractor at chest height with heavy drag pulls a
  particle spawned at the feet up to the chest within ~0.2 s. The colour and size curves
  are shifted so the particle is invisible for that flight. Used for the wail rings and
  the chest glow. The normalized attractor jitters the particle by ~strength*dt^2 around
  the point. That is invisible on a soft ring of size 1+.
* **Column flash**: a cone with zero base radius spawns uniformly along the y axis, so a
  handful of glows at once reads as a vertical flash with no lift delay.
* **Radius by kick and drag**: a zero-height cone throws particles horizontally; drag stops
  them at roughly ``start_speed / drag``. Upward gravity against the same drag gives a
  steady climb, and once the kick has died the velocity is purely vertical, so stretched
  sprites stand upright. That is the veil strands and the spiral wisps.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mage_common as mc
from mage_common import (BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem,
                         color_curve, float_curve, hpar, rgba)

# --- Palette -----------------------------------------------------------------------------
SPECTRAL_HOT = (0.88, 1.00, 0.98)   # near-white with a teal cast: flashes, tear heads
TEAL = (0.40, 0.96, 0.90)           # the body colour of every Mereth effect
CYAN = (0.32, 0.82, 0.98)
DEEP = (0.12, 0.56, 0.72)           # exit colour of teal layers
SILVER = (0.80, 0.88, 0.94)         # mourning silver: veils, echo rings
MIST = (0.62, 0.88, 0.90)
HUSH_DARK = (0.05, 0.04, 0.13)      # darkens the floor
HUSH_INDIGO = (0.20, 0.15, 0.44)
HUSH_VIOLET = (0.48, 0.40, 0.90)
HUSH_RIM = (0.55, 0.95, 1.00)       # rim of the hush pool: bright on dark stone

CHEST = 1.35

OUT_DIR = os.path.join(mc.ROOT, "data", "client", "Particles", "HollowChoir")


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)" % (path, len(system.emitters), os.path.getsize(path)))


# --- Building blocks ---------------------------------------------------------------------

def _e(**kw):
    """One-shot, local-space, no gravity, random roll. ``max_particles`` is sized from the
    bursts and ``spawn_rate * max_lifetime`` unless given."""
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


def _loop(emitter, rate, cycle=1.0):
    """Make ``emitter`` a continuous loop with a cap that never thins it out."""
    emitter.loop = True
    emitter.duration = cycle
    emitter.spawn_rate = rate
    bursts = sum(b.count for b in emitter.bursts)
    emitter.max_particles = int(rate * emitter.max_lifetime * 1.3 + bursts) + 6
    return emitter


def _fade(colour, alpha, peak=0.15, end=None, hold=None, hold_alpha=0.8):
    end = end or colour
    keys = [(0.0, rgba(colour, 0.0)), (peak, rgba(colour, alpha))]
    if hold is not None:
        keys.append((hold, rgba(end, alpha * hold_alpha)))
    keys.append((1.0, rgba(end, 0.0)))
    return color_curve(*keys)


def _hide_start(emitter, hidden):
    """Remap the colour and size curves so the first ``hidden`` fraction of each particle's
    life is invisible and the authored curves play over the rest."""
    first = emitter.color_over_lifetime[0].color
    clear = (first[0], first[1], first[2], 0.0)
    keys = [(hidden + k.time * (1.0 - hidden), tuple(k.color)) for k in emitter.color_over_lifetime]
    emitter.color_over_lifetime = color_curve((0.0, clear), (hidden * 0.98, clear), *keys)
    s0 = emitter.size_over_life[0].value
    skeys = [(hidden + k.time * (1.0 - hidden), k.value) for k in emitter.size_over_life]
    emitter.size_over_life = float_curve((0.0, s0), (hidden * 0.98, s0), *skeys)
    return emitter


def _pin(emitter, height=CHEST, strength=90.0, drag=9.0, flight=0.22):
    """Attractor pin (module docstring): pull to ``(0, height, 0)`` and hide the flight."""
    emitter.attractor_position = (0.0, height, 0.0)
    emitter.attractor_strength = strength
    emitter.drag = drag
    return _hide_start(emitter, min(0.6, flight / emitter.min_lifetime))


def _column_flash(name, top, size, colour, alpha, lifetime, count=5, delay=0.0, bottom=0.0):
    """A vertical flash: ``count`` glows spawned along the y axis from ~bottom to ``top``."""
    return _e(name=name, start_delay=delay, bursts=[Burst(0.0, count)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, top - bottom, 0.0),
              min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
              min_lifetime=lifetime * 0.85, max_lifetime=lifetime,
              min_start_size=size * 0.8, max_start_size=size,
              size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 1.3)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(colour, alpha)),
                  (0.35, rgba(colour, alpha * 0.65)),
                  (1.00, rgba(colour, 0.0))))


def _flat_ring(name, start, end, colour, alpha, lifetime, delay=0.0, bursts=None,
               growth=0.4, hold=0.85, hot=SPECTRAL_HOT, end_colour=None):
    """Flat ring on the floor. Most of the growth happens while it is still bright (the
    ``_fast_ring`` lesson). Ring sprite: the bright band is at ~31% of the quad size, so
    ``size = 3.2 * radius``."""
    ratio = end / start
    end_colour = end_colour or colour
    return _e(name=name, start_delay=delay, bursts=bursts or [Burst(0.0, 1)],
              duration=max(0.1, (bursts[-1].time + 0.05) if bursts else 0.1),
              min_lifetime=lifetime * 0.95, max_lifetime=lifetime,
              min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.05, 0.0),
              min_start_size=start, max_start_size=start,
              render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
              size_over_life=float_curve((0.0, 1.0), (growth, 1.0 + (ratio - 1.0) * hold), (1.0, ratio)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(hot, 0.0)),
                  (0.07, rgba(hot, alpha)),
                  (growth, rgba(colour, alpha * 0.75)),
                  (1.00, rgba(end_colour, 0.0))))


# =========================================================================================
# 1. Lament -- 3 s interruptible cast
# =========================================================================================

# Wail pulses come faster as the cast builds: 9 pulses, the gap shrinking from 0.55 s to
# 0.17 s, so the last second reads as a rising scream.
WAIL_TIMES = [0.0, 0.55, 1.05, 1.47, 1.82, 2.12, 2.38, 2.6, 2.78]


def lament_cast():
    # Pinned camera-facing rings at the chest: the "sound leaving her chest" motif.
    # Burst times are shifted earlier by the pin flight so each ring appears on its beat.
    def wail(name, times, size, alpha):
        return _pin(_e(name=name, duration=max(0.1, times[-1] + 0.05),
                       bursts=[Burst(t, 1) for t in times],
                       min_lifetime=0.95, max_lifetime=1.0,
                       min_start_size=size, max_start_size=size,
                       material_name=RING,
                       size_over_life=float_curve((0.0, 1.0), (0.45, 4.4), (1.0, 5.4)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(SPECTRAL_HOT, 0.0)),
                           (0.08, rgba(SPECTRAL_HOT, alpha)),
                           (0.45, rgba(TEAL, alpha * 0.7)),
                           (1.00, rgba(CYAN, 0.0)))))

    # Early pulses are smaller and softer, the last five brighter and bigger: the build.
    rings = wail("Wail Rings", WAIL_TIMES[:4], 0.8, 0.6)
    rings_late = wail("Wail Rings Late", WAIL_TIMES[4:], 1.05, 0.95)
    rings_inner = _pin(_e(name="Wail Rings Inner", duration=2.8,
                          bursts=[Burst(t + 0.09, 1) for t in WAIL_TIMES],
                          min_lifetime=0.7, max_lifetime=0.75,
                          min_start_size=0.5, max_start_size=0.5,
                          material_name=RING,
                          size_over_life=float_curve((0.0, 1.0), (0.45, 3.6), (1.0, 4.4)),
                          color_over_lifetime=color_curve(
                              (0.00, rgba(SPECTRAL_HOT, 0.0)),
                              (0.08, rgba(SPECTRAL_HOT, 0.75)),
                              (0.5, rgba(SILVER, 0.5)),
                              (1.00, rgba(TEAL, 0.0)))))
    floor_rings = _flat_ring("Wail Floor Rings", 1.4, 8.5, TEAL, 0.6, 0.9,
                             bursts=[Burst(t + 0.2, 1) for t in WAIL_TIMES], growth=0.45)
    # Chest glow: two layers, the second joining half way so the source swells.
    glow = _pin(_e(name="Chest Glow", duration=2.9, spawn_rate=9.0,
                   min_lifetime=0.6, max_lifetime=0.7,
                   min_start_size=1.1, max_start_size=1.3,
                   size_over_life=float_curve((0.0, 0.8), (1.0, 1.15)),
                   color_over_lifetime=_fade(TEAL, 0.32, peak=0.3, end=CYAN)))
    glow_swell = _pin(_e(name="Chest Glow Swell", start_delay=1.4, duration=1.5, spawn_rate=10.0,
                         min_lifetime=0.6, max_lifetime=0.7,
                         min_start_size=1.8, max_start_size=2.2,
                         size_over_life=float_curve((0.0, 0.8), (1.0, 1.15)),
                         color_over_lifetime=_fade(SPECTRAL_HOT, 0.28, peak=0.3, end=TEAL)))

    def tears(name, delay, duration, rate):
        # Bright tear droplets rising off her body: stretched beams on a cone shell.
        return _e(name=name, start_delay=delay, duration=duration, spawn_rate=rate,
                  shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.9, 0.55),
                  min_velocity=(-0.15, 0.7, -0.15), max_velocity=(0.15, 1.6, 0.15),
                  min_lifetime=0.9, max_lifetime=1.4,
                  min_start_size=0.07, max_start_size=0.12,
                  gravity=(0.0, 1.0, 0.0), drag=0.3,
                  render_mode=hpar.RENDER_STRETCHED, length_scale=4.0, material_name=BEAM,
                  size_over_life=float_curve((0.0, 0.5), (0.25, 1.0), (1.0, 0.4)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(SPECTRAL_HOT, 0.0)),
                      (0.15, rgba(SPECTRAL_HOT, 0.95)),
                      (0.6, rgba(TEAL, 0.7)),
                      (1.00, rgba(CYAN, 0.0))))

    def wisps(name, delay, duration, rate, colour, size, alpha, orbital):
        # Spiral wisps: kicked out to r ~0.9 and climbing ~1 u/s while orbiting.
        return _e(name=name, start_delay=delay, duration=duration, spawn_rate=rate,
                  shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.15),
                  min_velocity=(0.0, 1.0, 0.0), max_velocity=(0.0, 3.0, 0.0),
                  min_start_speed=2.2, max_start_speed=2.8,
                  min_lifetime=1.2, max_lifetime=1.6,
                  min_start_size=size * 0.6, max_start_size=size,
                  min_angular_velocity=-1.0, max_angular_velocity=1.0,
                  gravity=(0.0, 4.2, 0.0), drag=2.8, orbital_speed=orbital,
                  size_over_life=float_curve((0.0, 0.5), (0.35, 1.0), (1.0, 1.3)),
                  color_over_lifetime=_fade(colour, alpha, peak=0.25, end=DEEP, hold=0.6))

    stars = _e(name="Tear Glints", start_delay=0.8, duration=2.2, spawn_rate=9.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 2.0, 0.7),
               min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 0.9, 0.0),
               min_lifetime=0.5, max_lifetime=0.8,
               min_start_size=0.18, max_start_size=0.3,
               min_angular_velocity=-3.0, max_angular_velocity=3.0,
               orbital_speed=1.5, material_name=STAR,
               size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.3)),
               color_over_lifetime=_fade(SPECTRAL_HOT, 0.95, peak=0.2, end=TEAL, hold=0.6))
    floor_glow = _e(name="Floor Glow", duration=2.9, spawn_rate=3.0,
                    min_lifetime=0.9, max_lifetime=1.0,
                    min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
                    min_start_size=3.0, max_start_size=3.6,
                    min_angular_velocity=-0.3, max_angular_velocity=0.3,
                    render_mode=hpar.RENDER_HORIZONTAL,
                    size_over_life=float_curve((0.0, 0.8), (1.0, 1.1)),
                    color_over_lifetime=_fade(TEAL, 0.3, peak=0.4, end=DEEP))
    return ParticleSystem(emitters=[
        floor_glow, floor_rings,
        wisps("Wisps", 0.0, 3.0, 14.0, MIST, 0.7, 0.4, 2.0),
        wisps("Wisps Build", 1.3, 1.7, 24.0, TEAL, 0.65, 0.42, -2.6),
        glow, glow_swell,
        tears("Rising Tears", 0.0, 3.0, 12.0),
        tears("Rising Tears Build", 1.6, 1.4, 26.0),
        stars,
        rings_inner, rings, rings_late,
    ])


# =========================================================================================
# 2. Lament -- release (cast succeeded)
# =========================================================================================

def lament_release():
    # Floor sound ring out to ~17 units (size 55), plus a silver echo and a hot inner ring.
    ring = _flat_ring("Release Ring", 3.0, 55.0, TEAL, 0.8, 1.05, growth=0.5, hold=0.85)
    echo = _flat_ring("Release Echo", 2.5, 48.0, SILVER, 0.55, 1.0, delay=0.1, growth=0.5)
    inner = _flat_ring("Release Inner", 1.5, 16.0, SPECTRAL_HOT, 0.6, 0.7, delay=0.18, growth=0.45,
                       end_colour=TEAL)
    # Rolling spectral mist and streaks thrown flat along the floor (zero-height cone).
    mist = _e(name="Release Mist", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 44)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.8),
              min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.5, 0.0),
              min_start_speed=8.0, max_start_speed=22.0, drag=1.8,
              min_lifetime=0.9, max_lifetime=1.4,
              min_start_size=1.6, max_start_size=2.6,
              min_angular_velocity=-0.6, max_angular_velocity=0.6,
              size_over_life=float_curve((0.0, 0.5), (0.35, 1.0), (1.0, 1.5)),
              color_over_lifetime=_fade(MIST, 0.28, peak=0.12, end=TEAL))
    streaks = _e(name="Release Streaks", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 56)],
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.6),
                 min_velocity=(0.0, 0.2, 0.0), max_velocity=(0.0, 1.2, 0.0),
                 min_start_speed=14.0, max_start_speed=28.0, drag=1.9,
                 min_lifetime=0.5, max_lifetime=0.85,
                 min_start_size=0.16, max_start_size=0.26,
                 render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
                 size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                 color_over_lifetime=color_curve(
                     (0.00, rgba(SPECTRAL_HOT, 0.0)),
                     (0.06, rgba(SPECTRAL_HOT, 0.95)),
                     (0.45, rgba(TEAL, 0.65)),
                     (1.00, rgba(CYAN, 0.0))))
    # Ghostly vertical flare: fast stretched beams plus a slower glow pillar.
    flare = _e(name="Release Flare", bursts=[Burst(0.0, 30)],
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.6, 0.45),
               min_velocity=(-0.3, 6.0, -0.3), max_velocity=(0.3, 12.0, 0.3),
               drag=1.6, min_lifetime=0.45, max_lifetime=0.8,
               min_start_size=0.18, max_start_size=0.3,
               render_mode=hpar.RENDER_STRETCHED, length_scale=9.0, material_name=BEAM,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.35)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(SPECTRAL_HOT, 0.85)),
                   (0.45, rgba(TEAL, 0.55)),
                   (1.00, rgba(CYAN, 0.0))))
    pillar = _e(name="Release Pillar", bursts=[Burst(0.0, 18), Burst(0.08, 12)], duration=0.12,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.6, 0.35),
                min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 3.2, 0.0),
                drag=1.6, min_lifetime=0.8, max_lifetime=1.3,
                min_start_size=1.0, max_start_size=1.7,
                min_angular_velocity=-0.8, max_angular_velocity=0.8,
                size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 1.4)),
                color_over_lifetime=_fade(TEAL, 0.38, peak=0.1, end=CYAN, hold=0.45))
    tears = _e(name="Release Tears", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.05, 26)],
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.8, 0.5),
               min_start_speed=2.0, max_start_speed=5.0,
               min_velocity=(-0.5, 1.5, -0.5), max_velocity=(0.5, 3.5, 0.5),
               gravity=(0.0, 0.8, 0.0), drag=1.5,
               min_lifetime=0.8, max_lifetime=1.4,
               min_start_size=0.24, max_start_size=0.4,
               min_angular_velocity=-4.0, max_angular_velocity=4.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.2, 1.0), (1.0, 0.3)),
               color_over_lifetime=_fade(SPECTRAL_HOT, 0.95, peak=0.1, end=TEAL, hold=0.55))
    return ParticleSystem(emitters=[
        mist, ring, echo, inner, pillar, streaks, flare, tears,
        _column_flash("Release Flash", 2.4, 2.4, SPECTRAL_HOT, 0.55, 0.25, count=6),
    ])


# =========================================================================================
# 4. Mourning Voices -- 2 s cast
# =========================================================================================

def voices_cast():
    def spiral(name, rate, radius_speed, orbital, size, colour, alpha, material=GLOW,
               spin=1.0, delay=0.0, duration=1.75):
        return _e(name=name, start_delay=delay, duration=duration, spawn_rate=rate,
                  shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.12),
                  min_velocity=(0.0, 0.5, 0.0), max_velocity=(0.0, 1.5, 0.0),
                  min_start_speed=radius_speed * 0.9, max_start_speed=radius_speed * 1.1,
                  min_lifetime=1.2, max_lifetime=1.6,
                  min_start_size=size * 0.6, max_start_size=size,
                  min_angular_velocity=-spin, max_angular_velocity=spin,
                  gravity=(0.0, 5.0, 0.0), drag=2.6, orbital_speed=orbital,
                  material_name=material,
                  size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.7)),
                  color_over_lifetime=_fade(colour, alpha, peak=0.2, end=DEEP, hold=0.65))

    pulses = [Burst(0.0, 1), Burst(0.65, 1), Burst(1.25, 1)]
    # Rising voices: stretched streaks on the same helix. Once the outward kick has died
    # the velocity is vertical, so the streaks stand upright while orbit carries them round.
    streaks = spiral("Choir Streaks", 18.0, 2.5, 3.2, 0.1, SPECTRAL_HOT, 0.8, material=BEAM)
    streaks.render_mode = hpar.RENDER_STRETCHED
    streaks.length_scale = 6.0
    _hide_start(streaks, 0.35)
    floor = _flat_ring("Voices Floor Rings", 1.0, 6.0, TEAL, 0.5, 0.8, bursts=pulses)
    disc = _e(name="Voices Floor Glow", duration=1.8, spawn_rate=3.0,
              min_lifetime=0.8, max_lifetime=0.9,
              min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
              min_start_size=2.6, max_start_size=3.2,
              render_mode=hpar.RENDER_HORIZONTAL,
              size_over_life=float_curve((0.0, 0.8), (1.0, 1.15)),
              color_over_lifetime=_fade(TEAL, 0.28, peak=0.4, end=DEEP))
    return ParticleSystem(emitters=[
        disc, floor,
        spiral("Voices Outer", 10.0, 3.4, -2.2, 1.0, SILVER, 0.2),
        spiral("Choir Wisps", 30.0, 2.5, 3.2, 0.55, MIST, 0.45),
        streaks,
        spiral("Choir Notes", 11.0, 2.5, 3.2, 0.32, SPECTRAL_HOT, 0.95, material=STAR, spin=3.0),
        _column_flash("Voices Crescendo", 2.6, 1.8, TEAL, 0.4, 0.3, count=5, delay=1.65, bottom=0.6),
        _flat_ring("Voices Crescendo Ring", 1.5, 10.0, SPECTRAL_HOT, 0.6, 0.55, delay=1.65,
                   end_colour=TEAL),
    ])


# =========================================================================================
# 5. Mourning Chorus -- looping veil while the choristers live
# =========================================================================================

def chorus_veil():
    """Tall faint silver strands standing on a ~1.05-radius shell and drifting slowly round
    her, a few orbiting teal motes, and a faint ring at the feet. Steady state ~50 live."""
    strands = _loop(_e(name="Veil Strands",
                       shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.1),
                       min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 4.0, 0.0),
                       min_start_speed=3.0, max_start_speed=3.4,
                       min_lifetime=2.6, max_lifetime=3.2,
                       min_start_size=0.16, max_start_size=0.26,
                       gravity=(0.0, 1.5, 0.0), drag=3.0, orbital_speed=0.7,
                       render_mode=hpar.RENDER_STRETCHED, length_scale=7.0, material_name=BEAM,
                       size_over_life=float_curve((0.0, 0.6), (0.5, 1.0), (1.0, 0.8)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(SILVER, 0.0)),
                           (0.28, rgba(SILVER, 0.0)),
                           (0.5, rgba(SILVER, 0.28)),
                           (0.75, rgba(TEAL, 0.18)),
                           (1.00, rgba(DEEP, 0.0)))), rate=10.0)
    haze = _loop(_e(name="Veil Haze",
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.1),
                    min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 3.5, 0.0),
                    min_start_speed=3.0, max_start_speed=3.4,
                    min_lifetime=2.6, max_lifetime=3.2,
                    min_start_size=0.7, max_start_size=1.0,
                    min_angular_velocity=-0.4, max_angular_velocity=0.4,
                    gravity=(0.0, 1.2, 0.0), drag=3.0, orbital_speed=0.7,
                    size_over_life=float_curve((0.0, 0.7), (1.0, 1.3)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(MIST, 0.0)),
                        (0.25, rgba(MIST, 0.0)),
                        (0.5, rgba(MIST, 0.12)),
                        (1.00, rgba(TEAL, 0.0)))), rate=5.0)
    motes = _loop(_e(name="Veil Motes",
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.1),
                     min_velocity=(0.0, 1.0, 0.0), max_velocity=(0.0, 6.0, 0.0),
                     min_start_speed=3.3, max_start_speed=3.6,
                     min_lifetime=2.2, max_lifetime=2.8,
                     min_start_size=0.14, max_start_size=0.24,
                     min_angular_velocity=-1.5, max_angular_velocity=1.5,
                     gravity=(0.0, 0.6, 0.0), drag=3.0, orbital_speed=-1.1,
                     material_name=STAR,
                     size_over_life=float_curve((0.0, 0.5), (0.5, 1.0), (1.0, 0.5)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(SPECTRAL_HOT, 0.0)),
                         (0.22, rgba(SPECTRAL_HOT, 0.0)),
                         (0.45, rgba(SPECTRAL_HOT, 0.75)),
                         (0.75, rgba(TEAL, 0.5)),
                         (1.00, rgba(TEAL, 0.0)))), rate=5.0)
    ring = _loop(_e(name="Veil Ring",
                    min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
                    min_lifetime=2.4, max_lifetime=2.6,
                    min_start_size=3.4, max_start_size=3.5,
                    min_angular_velocity=-0.4, max_angular_velocity=0.4,
                    render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                    size_over_life=float_curve((0.0, 0.96), (1.0, 1.04)),
                    color_over_lifetime=_fade(TEAL, 0.3, peak=0.5, end=SILVER)), rate=1.2)
    system = ParticleSystem(emitters=[ring, haze, strands, motes])
    for e in system.emitters:
        e.warmup_time = 3.0
    return system


# =========================================================================================
# 6. Silent Place -- 12 s ground zone, radius 4.5 (unrotated node, y = 0 is the floor)
# =========================================================================================

RADIUS = 4.5
RIM_SIZE = RADIUS * 3.2


def silent_ground():
    """A dark hush pool with a crisp bright rim. Ripples run inward, motes sink into it.

    Looping emitters use a 12 s cycle so their t=0 bursts fire once, giving the zone a full
    pool and rim on its first frame instead of fading in over a lifetime. All lifetimes are
    <= 4 s so the zone dies within the engine's fade window. Steady state ~110 live."""
    def zone(emitter, rate):
        return _loop(emitter, rate, cycle=12.0)

    pool = zone(_e(name="Hush Pool", bursts=[Burst(0.0, 2)],
                   min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.02, 0.0),
                   min_lifetime=3.2, max_lifetime=3.6,
                   min_start_size=11.5, max_start_size=12.0,
                   min_angular_velocity=-0.1, max_angular_velocity=0.1,
                   render_mode=hpar.RENDER_HORIZONTAL,
                   size_over_life=float_curve((0.0, 0.97), (1.0, 1.0)),
                   color_over_lifetime=color_curve(
                       (0.00, rgba(HUSH_DARK, 0.0)),
                       (0.12, rgba(HUSH_DARK, 0.6)),
                       (0.85, rgba(HUSH_INDIGO, 0.55)),
                       (1.00, rgba(HUSH_INDIGO, 0.0)))), rate=0.8)
    blots = zone(_e(name="Hush Blots", bursts=[Burst(0.0, 4)],
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.6),
                    min_velocity=(0.0, 0.025, 0.0), max_velocity=(0.0, 0.025, 0.0),
                    min_lifetime=2.6, max_lifetime=3.4,
                    min_start_size=3.0, max_start_size=4.6,
                    min_angular_velocity=-0.25, max_angular_velocity=0.25,
                    render_mode=hpar.RENDER_HORIZONTAL,
                    size_over_life=float_curve((0.0, 0.8), (1.0, 1.15)),
                    color_over_lifetime=_fade(HUSH_INDIGO, 0.4, peak=0.3, end=HUSH_DARK)), rate=2.0)
    rim = zone(_e(name="Hush Rim", bursts=[Burst(0.0, 3)],
                  min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
                  min_lifetime=1.6, max_lifetime=1.8,
                  min_start_size=RIM_SIZE, max_start_size=RIM_SIZE,
                  min_angular_velocity=-0.3, max_angular_velocity=0.3,
                  render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                  size_over_life=float_curve((0.0, 1.0), (1.0, 1.0)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(HUSH_RIM, 0.0)),
                      (0.25, rgba(HUSH_RIM, 0.38)),
                      (0.75, rgba(HUSH_VIOLET, 0.3)),
                      (1.00, rgba(HUSH_VIOLET, 0.0)))), rate=2.0)
    ripples = zone(_e(name="Hush Ripples",
                      min_velocity=(0.0, 0.045, 0.0), max_velocity=(0.0, 0.045, 0.0),
                      min_lifetime=1.9, max_lifetime=2.0,
                      min_start_size=RIM_SIZE * 0.95, max_start_size=RIM_SIZE * 0.95,
                      min_angular_velocity=-0.5, max_angular_velocity=0.5,
                      render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                      size_over_life=float_curve((0.0, 1.0), (0.55, 0.45), (1.0, 0.08)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(HUSH_VIOLET, 0.0)),
                          (0.15, rgba(HUSH_VIOLET, 0.4)),
                          (0.7, rgba(TEAL, 0.32)),
                          (1.00, rgba(TEAL, 0.0)))), rate=1.1)

    # Rim glints: kicked out from the centre and stopped by drag at ~4.5 (hidden in flight).
    drag = 5.0
    kick = (RADIUS - 0.15) * drag / (1.0 - drag / 60.0)
    glints = zone(_hide_start(_e(name="Rim Glints", bursts=[Burst(0.0, 14)],
                                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.3),
                                 min_start_speed=kick * 0.98, max_start_speed=kick * 1.02,
                                 min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 1.4, 0.0),
                                 drag=drag,
                                 min_lifetime=1.6, max_lifetime=2.2,
                                 min_start_size=0.22, max_start_size=0.36,
                                 min_angular_velocity=-2.0, max_angular_velocity=2.0,
                                 material_name=STAR,
                                 size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.4)),
                                 color_over_lifetime=_fade(SPECTRAL_HOT, 0.95, peak=0.2, end=HUSH_RIM,
                                                           hold=0.6)), 0.4), rate=10.0)
    # Rim beads: small bright glows parked on the 4.5 circle -- the crisp edge the soft,
    # wide ring sprite cannot draw. Drag 3 keeps the frame-rate error of the parked radius
    # within ~5% (4.3-4.6 between 30 and 144 fps); the flight is hidden.
    bead_drag = 3.0
    bead_kick = (RADIUS - 0.15) * bead_drag / (1.0 - bead_drag / 60.0)
    beads = zone(_hide_start(_e(name="Rim Beads", bursts=[Burst(0.0, 70)],
                                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.3),
                                min_start_speed=bead_kick, max_start_speed=bead_kick,
                                min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
                                drag=bead_drag,
                                min_lifetime=3.2, max_lifetime=3.6,
                                min_start_size=0.32, max_start_size=0.42,
                                size_over_life=float_curve((0.0, 0.8), (1.0, 1.0)),
                                color_over_lifetime=color_curve(
                                    (0.00, rgba(HUSH_RIM, 0.0)),
                                    (0.2, rgba(SPECTRAL_HOT, 0.85)),
                                    (0.75, rgba(HUSH_RIM, 0.75)),
                                    (1.00, rgba(HUSH_VIOLET, 0.0)))), 0.3), rate=26.0)
    # Sinking motes: lifted to 1.4-2.6 (hidden), then sinking at ~0.7 u/s into the pool.
    sink = zone(_hide_start(_e(name="Sinking Motes", bursts=[Burst(0.0, 10)],
                               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 4.0),
                               min_velocity=(0.0, 6.0, 0.0), max_velocity=(0.0, 10.0, 0.0),
                               gravity=(0.0, -2.4, 0.0), drag=3.5,
                               min_lifetime=3.2, max_lifetime=3.8,
                               min_start_size=0.2, max_start_size=0.34,
                               min_angular_velocity=-1.5, max_angular_velocity=1.5,
                               material_name=STAR,
                               size_over_life=float_curve((0.0, 0.6), (0.4, 1.0), (1.0, 0.3)),
                               color_over_lifetime=color_curve(
                                   (0.00, rgba(SPECTRAL_HOT, 0.0)),
                                   (0.15, rgba(SPECTRAL_HOT, 0.85)),
                                   (0.6, rgba(HUSH_RIM, 0.6)),
                                   (1.00, rgba(HUSH_VIOLET, 0.0)))), 0.22), rate=9.0)
    drips = zone(_hide_start(_e(name="Sinking Streaks",
                                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 4.0),
                                min_velocity=(0.0, 5.0, 0.0), max_velocity=(0.0, 9.0, 0.0),
                                gravity=(0.0, -3.2, 0.0), drag=3.5,
                                min_lifetime=2.6, max_lifetime=3.2,
                                min_start_size=0.06, max_start_size=0.1,
                                render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
                                material_name=BEAM,
                                size_over_life=float_curve((0.0, 1.0), (1.0, 0.6)),
                                color_over_lifetime=color_curve(
                                    (0.00, rgba(MIST, 0.0)),
                                    (0.2, rgba(MIST, 0.55)),
                                    (0.7, rgba(HUSH_VIOLET, 0.45)),
                                    (1.00, rgba(HUSH_VIOLET, 0.0)))), 0.3), rate=5.0)
    # Low dark wisps drifting inward and slowly round: the hush swallowing sound.
    wisps = zone(_e(name="Hush Wisps", bursts=[Burst(0.0, 6)],
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, RADIUS),
                    min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.3, 0.0),
                    radial_acceleration=-0.9, drag=0.6, orbital_speed=0.35,
                    min_lifetime=2.6, max_lifetime=3.4,
                    min_start_size=1.0, max_start_size=1.7,
                    min_angular_velocity=-0.5, max_angular_velocity=0.5,
                    size_over_life=float_curve((0.0, 0.7), (1.0, 0.4)),
                    color_over_lifetime=_fade(HUSH_VIOLET, 0.24, peak=0.3, end=HUSH_INDIGO)), rate=4.0)

    # Onset (one-shot): the rim snaps in from outside and a dark slam darkens the floor.
    snap = _e(name="Onset Rim Snap", bursts=[Burst(0.0, 1)],
              min_lifetime=0.45, max_lifetime=0.45,
              min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
              min_start_size=RIM_SIZE * 1.6, max_start_size=RIM_SIZE * 1.6,
              render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
              size_over_life=float_curve((0.0, 1.0), (0.6, 0.66), (1.0, 0.625)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(SPECTRAL_HOT, 0.0)),
                  (0.1, rgba(SPECTRAL_HOT, 0.8)),
                  (0.6, rgba(HUSH_RIM, 0.6)),
                  (1.00, rgba(HUSH_RIM, 0.0))))
    slam = _e(name="Onset Hush", bursts=[Burst(0.0, 1)],
              min_lifetime=0.9, max_lifetime=0.9,
              min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
              min_start_size=12.0, max_start_size=12.0,
              render_mode=hpar.RENDER_HORIZONTAL,
              size_over_life=float_curve((0.0, 1.25), (1.0, 0.9)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(HUSH_DARK, 0.7)),
                  (1.00, rgba(HUSH_INDIGO, 0.0))))
    return ParticleSystem(emitters=[
        slam, pool, blots, wisps, ripples, rim, beads, snap, drips, sink, glints,
    ])


# =========================================================================================
# 7. Silent Place -- per-second pulse on a player standing in it (feet origin)
# =========================================================================================

def silent_pulse():
    """Subtle on purpose (it repeats every second): a small inward ring and a few dark
    wisps pulled down round the legs, plus two teal glints sinking. ~0.6 s."""
    ring = _e(name="Pulse Ring", bursts=[Burst(0.0, 1)],
              min_lifetime=0.58, max_lifetime=0.58,
              min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
              min_start_size=2.4, max_start_size=2.4,
              render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
              size_over_life=float_curve((0.0, 1.0), (1.0, 0.25)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(HUSH_VIOLET, 0.0)),
                  (0.15, rgba(HUSH_VIOLET, 0.7)),
                  (1.00, rgba(TEAL, 0.0))))
    wisps = _e(name="Pulse Wisps", bursts=[Burst(0.0, 7)],
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.0, 0.4),
               min_velocity=(0.0, -0.6, 0.0), max_velocity=(0.0, -0.2, 0.0),
               attractor_position=(0.0, 0.0, 0.0), attractor_strength=2.5, drag=1.0,
               orbital_speed=2.0,
               min_lifetime=0.5, max_lifetime=0.6,
               min_start_size=0.5, max_start_size=0.8,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
               color_over_lifetime=_fade(HUSH_VIOLET, 0.42, peak=0.2, end=HUSH_DARK, hold=0.5,
                                         hold_alpha=1.2))
    glints = _e(name="Pulse Glints", bursts=[Burst(0.0, 4)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.1, 0.35),
                min_velocity=(0.0, -1.2, 0.0), max_velocity=(0.0, -0.6, 0.0),
                min_lifetime=0.45, max_lifetime=0.58,
                min_start_size=0.18, max_start_size=0.26,
                min_angular_velocity=-3.0, max_angular_velocity=3.0,
                material_name=STAR,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                color_over_lifetime=_fade(HUSH_RIM, 0.8, peak=0.15, end=HUSH_VIOLET))
    return ParticleSystem(emitters=[ring, wisps, glints])


EFFECTS = [
    (lament_cast, "Mereth_LamentCast.hpar"),
    (lament_release, "Mereth_LamentRelease.hpar"),
    (voices_cast, "Mereth_VoicesCast.hpar"),
    (chorus_veil, "Mereth_ChorusVeil.hpar"),
    (silent_ground, "Mereth_SilentGround.hpar"),
    (silent_pulse, "Mereth_SilentPulse.hpar"),
]


def main():
    only = set(sys.argv[1:])
    for build, filename in EFFECTS:
        if not only or filename in only or filename.split(".")[0] in only:
            write(build(), filename)


if __name__ == "__main__":
    main()
