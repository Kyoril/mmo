# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Cantor Veyr (Hollow Choir, final boss) particle effects. Run from the repo root::

    py -3 tools/particle_gen/recipes/hollow_choir_veyr.py

Writes into ``data/client/Particles/HollowChoir/``.

Art direction: the former cantor conducting disembodied voices. **Discordant magenta and
violet with a sickly gold accent.** Motifs: jittering / beating sound rings, note-like
motes (spinning star sprites), rising choir columns.

Read ``warrior_common``'s docstring first -- there is no additive blending and no bloom, so
everything is density at low alpha (volumetric peaks 0.2-0.45), saturated hue, and motion.
Rims, sparks and single onset flashes may run bright.

Attachment conventions (see ``docs/hollow_choir_bosses.md``):

* Caster kits without a bone hang off the unit's scene node: origin at the feet, +Y up,
  +X forward. Everything here is radially symmetric, so the owner's yaw does not matter.
* Ground kits (GROUND_ACTIVE / GROUND_EXPIRED) play on an unrotated node at the zone centre,
  ``y = 0`` is the floor. Dissonance's radius is 4.0.
* The ring sprite's bright band sits at ~62 % of the quad half-size, i.e. a visible radius of
  ``0.31 * size``. ``_ring_size(r)`` converts a wanted radius into a quad size.

Two tricks used throughout:

* **Park.** There is no per-emitter offset and no "spawn on a circle" shape. A zero-height
  cone gives a horizontal radial spawn direction, so ``start_speed`` throws particles flat
  outward; drag stops them at roughly ``v0 / drag``. The same works vertically through the
  velocity box. ``_park`` computes the launch speeds (with the discrete-damping correction
  at 60 fps that ``mage_arcane._lift`` uses) and keeps the particle invisible during the
  flight. This is how sparks sit *on the 4.0 rim* and harmonic rings sit *at chest height*.
  Drag stays <= 5 so the frame-rate error stays within a few percent.
* **Beating alpha.** Sound rings flicker by giving their colour curve alternating keys whose
  spacing shrinks over life -- the beat of two detuned voices, accelerating with tension.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc  # noqa: E402
from warrior_common import (  # noqa: E402
    BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar,
    rgba)

RAYS = "Particles/Particle_Rays.hmi"   # sunburst sprite (make_sprites.py), for onset flashes

# --- Palette -----------------------------------------------------------------------------
VEYR_HOT = (1.00, 0.86, 1.00)       # pink-white core
VEYR_MAGENTA = (1.00, 0.24, 0.84)   # the signature colour
VEYR_VIOLET = (0.62, 0.30, 1.00)    # voices, dirge
VEYR_DEEP = (0.36, 0.12, 0.62)      # choir mist, darkens the floor under the rim
VEYR_GOLD = (0.96, 0.86, 0.32)      # sickly gold accent, slightly green

OUT_DIR = os.path.join(wc.ROOT, "data", "client", "Particles", "HollowChoir")

DISSONANCE_RADIUS = 4.0
# Held ground layers burst at t=0 inside a 0.1 s emission window, so a 1.9 s particle life
# makes the whole telegraph end at exactly 2.0 s, when the wave takes over.
GROUND_LIFE = 1.9


def _ring_size(radius):
    """Quad size whose ring band lands at ``radius`` (band at 62 % of the half-size)."""
    return radius / 0.31


def _e(**kw):
    """One-shot emitter with sane defaults: local space, no gravity, no implicit velocity,
    random roll. ``max_particles`` is sized from bursts + rate unless given."""
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


def _loop(emitter, rate=None, warmup=0.0):
    """Make an emitter loop forever (1 s cycle). Bursts re-arm every cycle."""
    emitter.loop = True
    emitter.duration = 1.0
    emitter.warmup_time = warmup
    if rate is not None:
        emitter.spawn_rate = rate
    bursts = sum(b.count for b in emitter.bursts)
    emitter.max_particles = int(emitter.spawn_rate * emitter.max_lifetime * 1.3
                                + bursts * (emitter.max_lifetime + 1.0)) + 6
    return emitter


def _launch(distance, drag):
    """Launch speed that comes to rest ``distance`` away under ``drag`` (60 fps discrete)."""
    return distance * drag / (1.0 - drag / 60.0)


def _hide(emitter, hidden):
    """Remap the colour curve into ``[hidden, 1]`` and keep alpha at zero before it."""
    if hidden <= 0.0:
        return emitter
    keys = list(emitter.color_over_lifetime)
    first = tuple(keys[0].color)
    invisible = (first[0], first[1], first[2], 0.0)
    rest = [(hidden + k.time * (1.0 - hidden), tuple(k.color)) for k in keys]
    rest[0] = (rest[0][0], invisible)
    emitter.color_over_lifetime = color_curve((0.0, invisible), (hidden * 0.98, invisible), *rest)
    return emitter


def _park(emitter, radius=0.0, height=0.0, drag=4.5, hidden=0.3, jitter=0.04):
    """Throw particles out to ``radius`` (flat, radial) and up to ``height``, stopped by
    drag, invisible for the first ``hidden`` of their life (see module docstring)."""
    if radius > 0.0:
        emitter.shape = hpar.SHAPE_CONE
        # Spawn offsets shorter than the engine's 1e-3 direction threshold
        # (particle_emitter.cpp, cone shape) fall back to a +Y direction and get launched
        # straight up as strays. The chance is 1e-3 / cone radius per particle; a 0.6 cone
        # keeps it to ~0.17 % at the cost of +-0.3 units of radial spread.
        emitter.shape_extents = (0.0, 0.0, 0.6)
        v = _launch(radius - 0.3, drag)
        emitter.min_start_speed = v * (1.0 - jitter)
        emitter.max_start_speed = v * (1.0 + jitter)
    vy = _launch(height, drag) if height > 0.0 else 0.0
    lo, hi = emitter.min_velocity, emitter.max_velocity
    emitter.min_velocity = (lo[0], vy * (1.0 - jitter) + lo[1], lo[2])
    emitter.max_velocity = (hi[0], vy * (1.0 + jitter) + hi[1], hi[2])
    emitter.drag = drag
    return _hide(emitter, hidden)


def _beat(colour_a, colour_b, peaks, base=0.0, end=1.0):
    """Colour curve that pulses: ``peaks`` is a list of (time, alpha) beats; between beats
    alpha drops to ``base`` fraction of the neighbouring peaks. Starts and ends at 0."""
    keys = [(0.0, rgba(colour_a, 0.0))]
    prev_t = 0.0
    for i, (t, a) in enumerate(peaks):
        if i > 0:
            mid = (prev_t + t) * 0.5
            keys.append((mid, rgba(colour_a if i % 2 else colour_b, a * base)))
        keys.append((t, rgba(colour_b if i % 2 else colour_a, a)))
        prev_t = t
    keys.append((end, rgba(colour_b, 0.0)))
    return color_curve(*keys)


def _flash(name, size, colour, alpha=0.7, lifetime=0.18, delay=0.0, material=GLOW, lift=0.0):
    e = _e(name=name, start_delay=delay, bursts=[Burst(0.0, 1)], max_particles=2,
           min_lifetime=lifetime, max_lifetime=lifetime,
           min_start_size=size, max_start_size=size,
           material_name=material,
           size_over_life=float_curve((0.0, 0.55), (0.3, 1.0), (1.0, 1.25)),
           color_over_lifetime=color_curve(
               (0.00, rgba(colour, alpha)),
               (0.35, rgba(colour, alpha * 0.65)),
               (1.00, rgba(colour, 0.0))))
    if lift > 0.0:
        _park(e, height=lift, drag=14.0, hidden=0.0, jitter=0.0)
    return e


def _hring(name, start, end, colour, alpha, lifetime, delay=0.0, hot=VEYR_HOT,
           growth=0.4, hold=0.85, count=1, lift=0.04):
    """Flat expanding sound ring. Size and alpha are shaped together so the growth happens
    while the ring is still bright (the warrior ``_fast_ring`` lesson)."""
    ratio = end / start
    return _e(name=name, start_delay=delay, bursts=[Burst(0.0, count)],
              min_lifetime=lifetime * 0.95, max_lifetime=lifetime,
              min_velocity=(0.0, lift, 0.0), max_velocity=(0.0, lift, 0.0),
              min_start_size=start, max_start_size=start,
              render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
              size_over_life=float_curve((0.0, 1.0), (growth, 1.0 + (ratio - 1.0) * hold),
                                         (1.0, ratio)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(hot, 0.0)),
                  (0.07, rgba(hot, alpha)),
                  (growth, rgba(colour, alpha * 0.75)),
                  (1.00, rgba(colour, 0.0))))


def _bring(name, start, end, colour, alpha, lifetime, delay=0.0, hot=VEYR_HOT):
    """Camera-facing shockwave ring for impacts on the body."""
    e = _hring(name, start, end, colour, alpha, lifetime, delay=delay, hot=hot, lift=0.0)
    e.render_mode = hpar.RENDER_BILLBOARD
    return e


def _sparks(name, count, speed, size, lifetime, hot=VEYR_HOT, body=VEYR_MAGENTA, alpha=1.0,
            length=5.5, gravity=-5.0, drag=2.4, **kw):
    params = dict(
        name=name, simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.1, 0.0, 0.0),
        min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
        min_start_speed=speed * 0.45, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, gravity, 0.0), drag=drag,
        render_mode=hpar.RENDER_STRETCHED, length_scale=length,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, alpha)),
            (0.40, rgba(body, alpha * 0.8)),
            (1.00, rgba(body, 0.0))))
    params.update(kw)
    return _e(**params)


def _notes(name, count, size, lifetime, colour=VEYR_MAGENTA, hot=VEYR_HOT, alpha=0.9, **kw):
    """Note-like motes: spinning star sprites, white-hot at birth, saturated as they die."""
    params = dict(
        name=name, simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, count)],
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_start_size=size * 0.55, max_start_size=size,
        min_angular_velocity=-4.0, max_angular_velocity=4.0,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.4), (0.2, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.12, rgba(hot, alpha)),
            (0.55, rgba(colour, alpha * 0.75)),
            (1.00, rgba(colour, 0.0))))
    params.update(kw)
    return _e(**params)


# =========================================================================================
# 1. Dissonance -- GROUND_ACTIVE telegraph, radius 4.0, exactly 2.0 s
# =========================================================================================

def dissonance_ground():
    rim = _ring_size(DISSONANCE_RADIUS)

    # The rim: one crisp ring held for the whole warning, beating faster and brighter as the
    # detonation nears. The beats stay within one colour: alternating white-hot and magenta read
    # as flashing in game (user feedback 2026-10-08). Ends at exactly 2.0 s.
    rim_main = _e(name="Rim", bursts=[Burst(0.0, 1)], max_particles=2,
                  min_lifetime=GROUND_LIFE, max_lifetime=GROUND_LIFE,
                  min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
                  min_start_size=rim, max_start_size=rim,
                  min_angular_velocity=0.4, max_angular_velocity=0.4,
                  render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                  size_over_life=float_curve((0.0, 1.06), (0.08, 1.0), (1.0, 1.0)),
                  color_over_lifetime=_beat(
                      VEYR_MAGENTA, VEYR_MAGENTA,
                      [(0.06, 0.45), (0.24, 0.45), (0.40, 0.48), (0.53, 0.5), (0.64, 0.52),
                       (0.73, 0.55), (0.81, 0.58), (0.87, 0.62), (0.92, 0.66), (0.965, 0.7)],
                      base=0.8, end=1.0))
    # Detuned twins: two more rings slightly in and out of the rim, offset a hair and
    # wandering on noise -- the rim visibly shivers instead of being a printed circle.
    rim_shiver = _e(name="Rim Shiver", bursts=[Burst(0.0, 3)], max_particles=4,
                    shape=hpar.SHAPE_BOX, shape_extents=(0.14, 0.0, 0.14),
                    min_lifetime=GROUND_LIFE * 0.97, max_lifetime=GROUND_LIFE,
                    min_velocity=(0.0, 0.035, 0.0), max_velocity=(0.0, 0.035, 0.0),
                    min_start_size=rim * 0.955, max_start_size=rim * 1.035,
                    noise_amplitude=3.0, noise_frequency=3.0, drag=6.0,
                    render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                    size_over_life=float_curve((0.0, 1.0), (1.0, 1.0)),
                    color_over_lifetime=_beat(
                        VEYR_VIOLET, VEYR_VIOLET,
                        [(0.10, 0.22), (0.30, 0.26), (0.46, 0.3), (0.58, 0.32), (0.69, 0.36),
                         (0.78, 0.4), (0.85, 0.42), (0.91, 0.45), (0.96, 0.45)],
                        base=0.55, end=1.0))
    # Contracting rings: launched from the rim inward on an accelerating cadence.
    cadence = [0.0, 0.32, 0.58, 0.80, 0.98, 1.13, 1.25, 1.35]
    contract = _e(name="Contracting Rings",
                  bursts=[Burst(t, 1) for t in cadence], duration=1.40,
                  shape=hpar.SHAPE_BOX, shape_extents=(0.18, 0.0, 0.18),
                  min_lifetime=0.56, max_lifetime=0.58,
                  min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
                  min_start_size=rim * 0.98, max_start_size=rim * 1.02,
                  render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                  size_over_life=float_curve((0.0, 1.0), (0.45, 0.72), (1.0, 0.22)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(VEYR_MAGENTA, 0.0)),
                      (0.10, rgba(VEYR_MAGENTA, 0.45)),
                      (0.45, rgba(VEYR_MAGENTA, 0.38)),
                      (1.00, rgba(VEYR_VIOLET, 0.0))))
    # Floor fill: darkens and tints the floor so the inside reads as "the zone", building up.
    fill_dark = _e(name="Fill Shadow", bursts=[Burst(0.0, 1)], max_particles=2,
                   min_lifetime=GROUND_LIFE, max_lifetime=GROUND_LIFE,
                   min_velocity=(0.0, 0.015, 0.0), max_velocity=(0.0, 0.015, 0.0),
                   min_start_size=DISSONANCE_RADIUS * 2.3, max_start_size=DISSONANCE_RADIUS * 2.3,
                   render_mode=hpar.RENDER_HORIZONTAL,
                   size_over_life=float_curve((0.0, 1.0), (1.0, 1.0)),
                   color_over_lifetime=color_curve(
                       (0.00, rgba(VEYR_DEEP, 0.0)),
                       (0.15, rgba(VEYR_DEEP, 0.30)),
                       (0.90, rgba(VEYR_DEEP, 0.40)),
                       (1.00, rgba(VEYR_DEEP, 0.0))))
    fill = _e(name="Fill Glow", bursts=[Burst(0.0, 1)], max_particles=2,
              min_lifetime=GROUND_LIFE, max_lifetime=GROUND_LIFE,
              min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.02, 0.0),
              min_start_size=DISSONANCE_RADIUS * 2.1, max_start_size=DISSONANCE_RADIUS * 2.1,
              render_mode=hpar.RENDER_HORIZONTAL,
              size_over_life=float_curve((0.0, 1.0), (1.0, 0.9)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(VEYR_MAGENTA, 0.0)),
                  (0.20, rgba(VEYR_MAGENTA, 0.12)),
                  (0.85, rgba(VEYR_MAGENTA, 0.30)),
                  (1.00, rgba(VEYR_HOT, 0.0))))
    # Bead line: small glows parked exactly on the 4.0 rim -- the crisp edge the soft ring
    # sprite cannot draw on its own (its band is ~1.8 units wide at this size).
    beads = _e(name="Rim Beads", bursts=[Burst(0.0, 150)],
               min_lifetime=GROUND_LIFE * 0.97, max_lifetime=GROUND_LIFE,
               noise_amplitude=0.6, noise_frequency=3.0,
               min_start_size=0.38, max_start_size=0.52,
               size_over_life=float_curve((0.0, 1.0), (1.0, 1.0)),
               color_over_lifetime=_beat(
                   VEYR_MAGENTA, VEYR_MAGENTA,
                   [(0.10, 0.85), (0.35, 0.9), (0.55, 0.9), (0.70, 0.95), (0.82, 1.0),
                    (0.91, 1.0), (0.97, 1.0)],
                   base=0.85, end=1.0))
    _park(beads, radius=DISSONANCE_RADIUS, height=0.06, drag=5.0, hidden=0.0, jitter=0.01)
    # Sparks parked on the rim, crackling upward and shaking on noise.
    rim_sparks = _e(name="Rim Sparks", spawn_rate=95.0, duration=0.90,
                    min_lifetime=0.95, max_lifetime=1.10,
                    min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
                    gravity=(0.0, 3.0, 0.0),
                    noise_amplitude=6.0, noise_frequency=2.5,
                    min_start_size=0.10, max_start_size=0.18,
                    render_mode=hpar.RENDER_STRETCHED, length_scale=4.0,
                    material_name=BEAM,
                    size_over_life=float_curve((0.0, 1.0), (1.0, 0.6)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(VEYR_HOT, 0.0)),
                        (0.10, rgba(VEYR_HOT, 1.0)),
                        (0.50, rgba(VEYR_MAGENTA, 0.85)),
                        (1.00, rgba(VEYR_MAGENTA, 0.0))))
    _park(rim_sparks, radius=DISSONANCE_RADIUS, drag=5.0, hidden=0.45, jitter=0.03)
    # Note motes jittering on the rim (gold accent among them).
    rim_notes = _e(name="Rim Notes", spawn_rate=26.0, duration=0.95,
                   min_lifetime=0.95, max_lifetime=1.05,
                   gravity=(0.0, 2.0, 0.0),
                   noise_amplitude=8.0, noise_frequency=3.0,
                   min_start_size=0.22, max_start_size=0.36,
                   min_angular_velocity=-5.0, max_angular_velocity=5.0,
                   material_name=STAR,
                   size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.5)),
                   color_over_lifetime=color_curve(
                       (0.00, rgba(VEYR_GOLD, 0.0)),
                       (0.15, rgba(VEYR_GOLD, 0.95)),
                       (0.55, rgba(VEYR_MAGENTA, 0.75)),
                       (1.00, rgba(VEYR_MAGENTA, 0.0))))
    _park(rim_notes, radius=DISSONANCE_RADIUS * 0.97, drag=5.0, hidden=0.45, jitter=0.03)
    # Crackle inside the zone: short vertical streaks popping up across the floor.
    crackle = _e(name="Inner Crackle", spawn_rate=55.0, duration=1.55, start_delay=0.20,
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, DISSONANCE_RADIUS * 0.95),
                 min_velocity=(0.0, 1.5, 0.0), max_velocity=(0.0, 3.5, 0.0),
                 min_lifetime=0.15, max_lifetime=0.25, drag=2.0,
                 min_start_size=0.06, max_start_size=0.11,
                 render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
                 material_name=BEAM,
                 size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                 color_over_lifetime=color_curve(
                     (0.00, rgba(VEYR_HOT, 0.0)),
                     (0.15, rgba(VEYR_HOT, 0.9)),
                     (1.00, rgba(VEYR_MAGENTA, 0.0))))
    return ParticleSystem(emitters=[fill_dark, fill, contract, rim_shiver, rim_main,
                                    beads, crackle, rim_sparks, rim_notes])


# =========================================================================================
# 2. Dissonance -- GROUND_EXPIRED detonation
# =========================================================================================

def dissonance_wave():
    rim = _ring_size(DISSONANCE_RADIUS)
    flash_disc = _e(name="Detonation Floor Flash", bursts=[Burst(0.0, 1)], max_particles=2,
                    min_lifetime=0.45, max_lifetime=0.45,
                    min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
                    min_start_size=9.0, max_start_size=9.0,
                    render_mode=hpar.RENDER_HORIZONTAL,
                    size_over_life=float_curve((0.0, 0.9), (0.3, 1.1), (1.0, 1.3)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(VEYR_HOT, 0.40)),
                        (0.30, rgba(VEYR_MAGENTA, 0.30)),
                        (1.00, rgba(VEYR_VIOLET, 0.0))))
    wave = _hring("Sonic Ring", rim, rim * 1.8, VEYR_MAGENTA, 0.9, 0.85, growth=0.35)
    wave2 = _hring("Sonic Ring Echo", rim * 0.9, rim * 1.6, VEYR_VIOLET, 0.7, 0.85,
                   delay=0.09, growth=0.38)
    wave_gold = _hring("Sonic Ring Gold", rim * 0.8, rim * 1.4, VEYR_GOLD, 0.75, 0.7,
                       delay=0.17, hot=VEYR_HOT, growth=0.4)
    inner = _hring("Inner Ring", 2.0, rim * 0.9, VEYR_HOT, 0.75, 0.45, growth=0.4)
    # Vertical shock column: streaks shot up across the whole zone (centre-dense cone).
    column = _e(name="Shock Column", bursts=[Burst(0.0, 70)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, DISSONANCE_RADIUS * 0.9),
                min_velocity=(0.0, 6.0, 0.0), max_velocity=(0.0, 13.0, 0.0),
                min_lifetime=0.28, max_lifetime=0.5, drag=3.0,
                min_start_size=0.16, max_start_size=0.3,
                render_mode=hpar.RENDER_STRETCHED, length_scale=8.0,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.35)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(VEYR_HOT, 0.95)),
                    (0.40, rgba(VEYR_MAGENTA, 0.7)),
                    (1.00, rgba(VEYR_VIOLET, 0.0))))
    sheath = _e(name="Column Sheath", bursts=[Burst(0.0, 26)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 1.6),
                min_velocity=(0.0, 4.0, 0.0), max_velocity=(0.0, 9.0, 0.0),
                min_lifetime=0.5, max_lifetime=0.8, drag=2.5, orbital_speed=2.0,
                min_start_size=0.9, max_start_size=1.5,
                render_mode=hpar.RENDER_STRETCHED, length_scale=3.0,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.8)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(VEYR_MAGENTA, 0.0)),
                    (0.12, rgba(VEYR_MAGENTA, 0.35)),
                    (1.00, rgba(VEYR_VIOLET, 0.0))))
    # Sparks thrown flat along the floor outward past the rim.
    shards = _sparks("Floor Sparks", 64, speed=20.0, size=0.18, lifetime=0.7,
                     shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.5),
                     min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 2.0, 0.0),
                     gravity=-3.0, drag=2.6, length=6.5)
    shards.min_start_speed = 8.0
    notes = _notes("Scattered Notes", 34, size=0.5, lifetime=1.6,
                   shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 3.0),
                   min_velocity=(0.0, 1.5, 0.0), max_velocity=(0.0, 4.5, 0.0),
                   min_start_speed=2.0, max_start_speed=7.0,
                   gravity=(0.0, -1.2, 0.0), drag=2.0)
    gold_notes = _notes("Scattered Gold Notes", 12, size=0.42, lifetime=1.5,
                        colour=VEYR_GOLD, hot=VEYR_GOLD,
                        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 3.0),
                        min_velocity=(0.0, 1.5, 0.0), max_velocity=(0.0, 4.0, 0.0),
                        min_start_speed=2.0, max_start_speed=6.0,
                        gravity=(0.0, -1.2, 0.0), drag=2.0)
    mist = _e(name="Pressure Mist", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 30)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.0),
              min_velocity=(0.0, 0.1, 0.0), max_velocity=(0.0, 0.6, 0.0),
              min_start_speed=6.0, max_start_speed=14.0, drag=2.4,
              min_lifetime=0.8, max_lifetime=1.25,
              min_start_size=1.4, max_start_size=2.2,
              min_angular_velocity=-0.8, max_angular_velocity=0.8,
              size_over_life=float_curve((0.0, 0.5), (0.35, 1.0), (1.0, 1.5)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(VEYR_MAGENTA, 0.0)),
                  (0.15, rgba(VEYR_MAGENTA, 0.28)),
                  (1.00, rgba(VEYR_DEEP, 0.0))))
    return ParticleSystem(emitters=[
        flash_disc, mist, wave_gold, wave2, wave, inner, sheath, column, shards,
        notes, gold_notes,
        _flash("Detonation Flash", 3.2, VEYR_HOT, alpha=0.5, lifetime=0.2, lift=1.0),
        _flash("Detonation Rays", 4.2, VEYR_MAGENTA, alpha=0.55, lifetime=0.3, lift=1.0,
               material=RAYS),
    ])


# =========================================================================================
# 3. Dissonance -- IMPACT on each player hit (spine_03)
# =========================================================================================

def dissonance_hit():
    return ParticleSystem(emitters=[
        _flash("Hit Flash", 1.2, VEYR_HOT, alpha=0.75, lifetime=0.15),
        _flash("Hit Rays", 1.5, VEYR_MAGENTA, alpha=0.8, lifetime=0.22, material=RAYS),
        _bring("Hit Shock", 0.5, 2.6, VEYR_MAGENTA, 0.75, 0.32),
        _bring("Hit Shock Echo", 0.4, 2.0, VEYR_VIOLET, 0.6, 0.32, delay=0.08),
        _sparks("Hit Sparks", 22, speed=7.0, size=0.12, lifetime=0.45, drag=3.0),
        _notes("Hit Notes", 7, size=0.3, lifetime=0.65,
               shape=hpar.SHAPE_SPHERE, shape_extents=(0.15, 0.0, 0.0),
               min_start_speed=1.0, max_start_speed=2.6, drag=3.0,
               min_velocity=(0.0, 0.3, 0.0), max_velocity=(0.0, 0.9, 0.0)),
    ])


# =========================================================================================
# 4. Dirge of the Grave -- CHANNELING loop on Veyr (feet origin)
# =========================================================================================

def dirge_channel():
    # Voices spiralling up a column around him: parked out on a 1.0 ring, then lifted by
    # gravity against drag (terminal speed g/drag) while the orbit turns them -- a helix
    # shell around his body rather than a filled cylinder.
    def voice_layer(name, material, rate, size, colour_curve, spin=0.0):
        e = _e(name=name, min_lifetime=1.7, max_lifetime=2.1, orbital_speed=2.4,
               gravity=(0.0, 7.0, 0.0),
               min_start_size=size * 0.6, max_start_size=size,
               min_angular_velocity=-spin, max_angular_velocity=spin,
               material_name=material,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.5)),
               color_over_lifetime=colour_curve)
        _park(e, radius=1.0, drag=4.0, hidden=0.0, jitter=0.15)
        return _loop(e, rate=rate, warmup=0.6)

    voices = voice_layer("Spiral Voices", GLOW, 46.0, 0.42, color_curve(
        (0.00, rgba(VEYR_VIOLET, 0.0)),
        (0.15, rgba(VEYR_HOT, 0.7)),
        (0.55, rgba(VEYR_VIOLET, 0.55)),
        (1.00, rgba(VEYR_MAGENTA, 0.0))))
    voice_notes = voice_layer("Spiral Notes", STAR, 14.0, 0.40, color_curve(
        (0.00, rgba(VEYR_HOT, 0.0)),
        (0.15, rgba(VEYR_HOT, 0.95)),
        (0.6, rgba(VEYR_MAGENTA, 0.7)),
        (1.00, rgba(VEYR_VIOLET, 0.0))), spin=4.0)
    # Faint vertical sheath: the "choir column" silhouette, readable from across the room.
    column = _loop(_e(name="Choir Column",
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.2, 0.75),
                      min_velocity=(0.0, 1.6, 0.0), max_velocity=(0.0, 3.4, 0.0),
                      min_lifetime=1.0, max_lifetime=1.4, orbital_speed=1.4,
                      min_start_size=0.3, max_start_size=0.5,
                      render_mode=hpar.RENDER_STRETCHED, length_scale=7.0,
                      material_name=BEAM,
                      size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.8)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(VEYR_VIOLET, 0.0)),
                          (0.2, rgba(VEYR_VIOLET, 0.26)),
                          (0.8, rgba(VEYR_MAGENTA, 0.16)),
                          (1.00, rgba(VEYR_MAGENTA, 0.0)))),
                   rate=30.0, warmup=0.6)
    # Ring pulse along the floor every second (matches the damage pulse).
    pulse = _hring("Dirge Pulse", 2.0, _ring_size(6.0), VEYR_VIOLET, 0.8, 0.85,
                   hot=VEYR_HOT, growth=0.45, hold=0.85)
    _loop(pulse)
    pulse_inner = _hring("Dirge Pulse Echo", 1.6, _ring_size(4.0), VEYR_MAGENTA, 0.55, 0.7,
                         delay=0.0, growth=0.45)
    pulse_inner.bursts = [Burst(0.1, 1)]
    _loop(pulse_inner)
    # Dark choir mist swirling at the feet.
    mist = _loop(_e(name="Choir Mist",
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.1, 1.8),
                    min_velocity=(-0.1, 0.05, -0.1), max_velocity=(0.1, 0.35, 0.1),
                    min_lifetime=1.6, max_lifetime=2.2, orbital_speed=0.9,
                    min_start_size=1.2, max_start_size=2.0,
                    min_angular_velocity=-0.5, max_angular_velocity=0.5,
                    size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.3)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(VEYR_DEEP, 0.0)),
                        (0.3, rgba(VEYR_DEEP, 0.4)),
                        (1.00, rgba(VEYR_DEEP, 0.0)))),
                 rate=10.0, warmup=1.0)
    # Beacon above the head: a pulsing crown of light between the raised arms.
    crown = _e(name="Conductor Crown", bursts=[Burst(0.0, 1), Burst(0.5, 1)],
               min_lifetime=0.9, max_lifetime=0.9,
               min_start_size=1.7, max_start_size=1.7,
               size_over_life=float_curve((0.0, 0.6), (0.4, 1.0), (1.0, 1.2)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(VEYR_MAGENTA, 0.0)),
                   (0.3, rgba(VEYR_HOT, 0.6)),
                   (1.00, rgba(VEYR_MAGENTA, 0.0))))
    _park(crown, height=2.55, drag=5.0, hidden=0.2, jitter=0.0)
    _loop(crown, warmup=1.0)
    crown_stars = _e(name="Crown Notes", spawn_rate=12.0,
                     shape=hpar.SHAPE_SPHERE, shape_extents=(0.35, 0.0, 0.0),
                     min_velocity=(-0.3, 0.0, -0.3), max_velocity=(0.3, 0.0, 0.3),
                     min_lifetime=0.8, max_lifetime=1.1, orbital_speed=3.0,
                     gravity=(0.0, 1.0, 0.0),
                     min_start_size=0.2, max_start_size=0.34,
                     min_angular_velocity=-5.0, max_angular_velocity=5.0,
                     material_name=STAR,
                     size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.4)),
                     color_over_lifetime=color_curve(
                         (0.00, rgba(VEYR_HOT, 0.0)),
                         (0.2, rgba(VEYR_HOT, 0.95)),
                         (0.6, rgba(VEYR_GOLD, 0.7)),
                         (1.00, rgba(VEYR_MAGENTA, 0.0))))
    _park(crown_stars, height=2.5, drag=5.0, hidden=0.25, jitter=0.0)
    _loop(crown_stars, warmup=1.0)
    return ParticleSystem(emitters=[mist, pulse, pulse_inner, column, voices, voice_notes,
                                    crown, crown_stars])


# =========================================================================================
# 6. The Choir Rises -- CASTING 2 s one-shot on Veyr (feet origin)
# =========================================================================================

GATHER_POINT = (0.0, 2.3, 0.0)


def choir_rises_cast():
    # Motes rising from a wide ring on the floor and converging above his head.
    gather = _e(name="Gathering Motes", duration=1.2, spawn_rate=55.0,
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.6),
                min_velocity=(0.0, 0.8, 0.0), max_velocity=(0.0, 1.6, 0.0),
                min_start_speed=0.3, max_start_speed=0.6,
                min_lifetime=0.75, max_lifetime=0.8,
                attractor_position=GATHER_POINT, attractor_strength=7.0, drag=1.2,
                orbital_speed=1.8,
                min_start_size=0.18, max_start_size=0.32,
                size_over_life=float_curve((0.0, 0.6), (0.6, 1.0), (1.0, 0.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(VEYR_MAGENTA, 0.0)),
                    (0.25, rgba(VEYR_MAGENTA, 0.7)),
                    (0.8, rgba(VEYR_HOT, 0.75)),
                    (1.00, rgba(VEYR_HOT, 0.0))))
    gather_notes = _e(name="Gathering Notes", duration=1.2, spawn_rate=16.0,
                      shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.4),
                      min_velocity=(0.0, 0.8, 0.0), max_velocity=(0.0, 1.6, 0.0),
                      min_lifetime=0.75, max_lifetime=0.8,
                      attractor_position=GATHER_POINT, attractor_strength=7.0, drag=1.2,
                      orbital_speed=1.8,
                      min_start_size=0.24, max_start_size=0.38,
                      min_angular_velocity=-4.0, max_angular_velocity=4.0,
                      material_name=STAR,
                      size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.4)),
                      color_over_lifetime=color_curve(
                          (0.00, rgba(VEYR_GOLD, 0.0)),
                          (0.25, rgba(VEYR_GOLD, 0.9)),
                          (0.7, rgba(VEYR_MAGENTA, 0.75)),
                          (1.00, rgba(VEYR_HOT, 0.0))))
    # Upward streaks around him -- the choir column rising.
    rise = _e(name="Rising Column", duration=1.6, spawn_rate=26.0,
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.2, 1.2),
              min_velocity=(0.0, 1.8, 0.0), max_velocity=(0.0, 3.2, 0.0),
              min_lifetime=0.3, max_lifetime=0.4, orbital_speed=1.2,
              min_start_size=0.22, max_start_size=0.36,
              render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
              material_name=BEAM,
              size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.7)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(VEYR_MAGENTA, 0.0)),
                  (0.2, rgba(VEYR_MAGENTA, 0.5)),
                  (1.00, rgba(VEYR_VIOLET, 0.0))))
    # A slowly contracting floor ring under him, and the gathered glow growing overhead.
    floor = _hring("Floor Ring", _ring_size(2.4), _ring_size(0.9), VEYR_MAGENTA, 0.6, 1.9,
                   growth=0.6, hold=0.6)
    floor.color_over_lifetime = color_curve(
        (0.00, rgba(VEYR_VIOLET, 0.0)), (0.2, rgba(VEYR_MAGENTA, 0.55)),
        (0.85, rgba(VEYR_HOT, 0.7)), (1.0, rgba(VEYR_HOT, 0.0)))
    floor_glow = _e(name="Floor Glow", bursts=[Burst(0.0, 1)], max_particles=2,
                    min_lifetime=1.9, max_lifetime=1.9,
                    min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
                    min_start_size=4.0, max_start_size=4.0,
                    render_mode=hpar.RENDER_HORIZONTAL,
                    size_over_life=float_curve((0.0, 1.0), (1.0, 0.7)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(VEYR_DEEP, 0.0)), (0.3, rgba(VEYR_MAGENTA, 0.25)),
                        (1.0, rgba(VEYR_MAGENTA, 0.0))))
    orb = _e(name="Gathered Orb", spawn_rate=10.0, duration=1.3, start_delay=0.2,
             min_lifetime=0.45, max_lifetime=0.5,
             min_start_size=0.9, max_start_size=1.5,
             size_over_life=float_curve((0.0, 0.5), (0.5, 1.0), (1.0, 1.2)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(VEYR_MAGENTA, 0.0)), (0.5, rgba(VEYR_HOT, 0.55)),
                 (1.0, rgba(VEYR_MAGENTA, 0.0))))
    orb.attractor_position = GATHER_POINT
    orb.attractor_strength = 260.0
    orb.drag = 24.0
    return ParticleSystem(emitters=[floor_glow, floor, rise, gather, gather_notes, orb])


# =========================================================================================
# 7. The Choir Rises -- CAST_SUCCEEDED burst
# =========================================================================================

def choir_rises_release():
    burst_up = _e(name="Choir Eruption", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 40)],
                  shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.3, 1.0),
                  min_velocity=(0.0, 5.0, 0.0), max_velocity=(0.0, 11.0, 0.0),
                  min_lifetime=0.35, max_lifetime=0.6, drag=2.5,
                  min_start_size=0.18, max_start_size=0.32,
                  render_mode=hpar.RENDER_STRETCHED, length_scale=7.0,
                  material_name=BEAM,
                  size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(VEYR_HOT, 0.95)),
                      (0.4, rgba(VEYR_MAGENTA, 0.7)),
                      (1.00, rgba(VEYR_VIOLET, 0.0))))
    sheath = _e(name="Choir Sheath", simulation_space=hpar.SIM_WORLD, bursts=[Burst(0.0, 22)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.2, 0.9),
                min_velocity=(0.0, 2.5, 0.0), max_velocity=(0.0, 5.5, 0.0),
                min_lifetime=0.55, max_lifetime=0.85, drag=2.0, orbital_speed=2.0,
                min_start_size=0.7, max_start_size=1.1,
                render_mode=hpar.RENDER_STRETCHED, length_scale=3.0,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.8)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(VEYR_MAGENTA, 0.0)),
                    (0.12, rgba(VEYR_MAGENTA, 0.38)),
                    (1.00, rgba(VEYR_VIOLET, 0.0))))
    notes = _notes("Released Notes", 26, size=0.42, lifetime=1.15,
                   shape=hpar.SHAPE_SPHERE, shape_extents=(0.4, 0.0, 0.0),
                   min_velocity=(0.0, 1.5, 0.0), max_velocity=(0.0, 3.5, 0.0),
                   min_start_speed=2.0, max_start_speed=5.0,
                   gravity=(0.0, -1.0, 0.0), drag=2.2)
    _park(notes, height=0.0, drag=2.2, hidden=0.0)
    notes.min_velocity = (0.0, 4.0, 0.0)
    notes.max_velocity = (0.0, 7.0, 0.0)
    gold = _notes("Released Gold Notes", 10, size=0.36, lifetime=1.1,
                  colour=VEYR_GOLD, hot=VEYR_GOLD,
                  shape=hpar.SHAPE_SPHERE, shape_extents=(0.4, 0.0, 0.0),
                  min_velocity=(0.0, 4.0, 0.0), max_velocity=(0.0, 7.0, 0.0),
                  min_start_speed=2.0, max_start_speed=4.5,
                  gravity=(0.0, -1.0, 0.0), drag=2.2)
    return ParticleSystem(emitters=[
        _hring("Release Ring", 2.0, _ring_size(5.0), VEYR_MAGENTA, 0.85, 0.7, growth=0.38),
        _hring("Release Ring Gold", 1.6, _ring_size(3.6), VEYR_GOLD, 0.6, 0.6, delay=0.1,
               hot=VEYR_GOLD),
        sheath, burst_up, notes, gold,
        _flash("Release Flash", 2.4, VEYR_HOT, alpha=0.7, lifetime=0.2, lift=1.3),
        _flash("Release Rays", 3.2, VEYR_MAGENTA, alpha=0.75, lifetime=0.32, lift=1.4,
               material=RAYS),
    ])


# =========================================================================================
# 8. Choral Resonance -- AURA_IDLE loop on Veyr (feet origin)
# =========================================================================================

CHEST = 1.25


def resonance_aura():
    # Two detuned harmonic rings at chest height breathing in and out of phase: the beat.
    def harmonic(name, colour, size, lifetime, rate, alpha):
        e = _e(name=name, spawn_rate=rate,
               min_lifetime=lifetime, max_lifetime=lifetime,
               min_start_size=size * 0.97, max_start_size=size * 1.03,
               min_angular_velocity=-0.6, max_angular_velocity=0.6,
               render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
               size_over_life=float_curve((0.0, 0.88), (0.5, 1.08), (1.0, 0.92)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(colour, 0.0)),
                   (0.3, rgba(colour, alpha)),
                   (0.5, rgba(VEYR_HOT, alpha * 0.8)),
                   (0.7, rgba(colour, alpha)),
                   (1.00, rgba(colour, 0.0))))
        _park(e, height=CHEST, drag=5.0, hidden=0.18, jitter=0.0)
        return _loop(e, warmup=3.0)

    ring_a = harmonic("Harmonic Ring", VEYR_MAGENTA, _ring_size(0.95), 2.2, 1.0, 0.5)
    ring_b = harmonic("Harmonic Ring Gold", VEYR_GOLD, _ring_size(1.15), 1.7, 0.85, 0.38)
    # Orbiting note motes in a band around the chest.
    orbit = _e(name="Orbit Notes", spawn_rate=7.0,
               min_lifetime=1.8, max_lifetime=2.2, orbital_speed=1.6,
               min_velocity=(0.0, -0.1, 0.0), max_velocity=(0.0, 0.1, 0.0),
               min_start_size=0.16, max_start_size=0.26,
               min_angular_velocity=-3.0, max_angular_velocity=3.0,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.4)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(VEYR_HOT, 0.0)),
                   (0.25, rgba(VEYR_HOT, 0.85)),
                   (0.6, rgba(VEYR_MAGENTA, 0.65)),
                   (1.00, rgba(VEYR_MAGENTA, 0.0))))
    _park(orbit, radius=0.85, height=CHEST, drag=5.0, hidden=0.18, jitter=0.12)
    _loop(orbit, warmup=3.0)
    orbit_glow = _e(name="Orbit Glow", spawn_rate=9.0,
                    min_lifetime=1.6, max_lifetime=2.0, orbital_speed=1.6,
                    min_start_size=0.3, max_start_size=0.5,
                    size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 0.6)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(VEYR_MAGENTA, 0.0)),
                        (0.3, rgba(VEYR_MAGENTA, 0.3)),
                        (1.00, rgba(VEYR_VIOLET, 0.0))))
    _park(orbit_glow, radius=0.8, height=CHEST, drag=5.0, hidden=0.18, jitter=0.15)
    _loop(orbit_glow, warmup=3.0)
    return ParticleSystem(emitters=[ring_b, ring_a, orbit_glow, orbit])


EFFECTS = {
    "Veyr_DissonanceGround.hpar": dissonance_ground,
    "Veyr_DissonanceWave.hpar": dissonance_wave,
    "Veyr_DissonanceHit.hpar": dissonance_hit,
    "Veyr_DirgeChannel.hpar": dirge_channel,
    "Veyr_ChoirRisesCast.hpar": choir_rises_cast,
    "Veyr_ChoirRisesRelease.hpar": choir_rises_release,
    "Veyr_ResonanceAura.hpar": resonance_aura,
}

# Torn down by the engine (channel end / aura removal); everything else is one-shot.
LOOPING_EFFECTS = {"Veyr_DirgeChannel.hpar", "Veyr_ResonanceAura.hpar"}


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)" % (path, len(system.emitters), os.path.getsize(path)))


if __name__ == "__main__":
    only = sys.argv[1:]
    for filename, build in EFFECTS.items():
        if only and not any(o.lower() in filename.lower() for o in only):
            continue
        write(build(), filename)
