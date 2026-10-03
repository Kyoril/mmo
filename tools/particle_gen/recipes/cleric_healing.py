# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Cleric healing effects (``data/client/Particles/Cleric/``). Run from the repo root::

    py -3 tools/particle_gen/recipes/cleric_healing.py

Art direction (see ``cleric_common``): radiant stylized holy light with the shared
gold/ivory core. Healing reads **softer and slower** than damage -- light that settles onto
the target rather than striking it, motes that rise, and a HEAL_GREEN life accent used
sparingly. Character scale is 1.8 units; every effect here is authored at the target's
feet (ROOT attach), so heights are measured from the ground.

Effects and where the engine spawns them:

=============================  ========  ===========================================
HealingLightImpact.hpar        one-shot  target root, main 1.5 s direct heal
DivineVitalityImpact.hpar      one-shot  target root, 30 min stamina blessing
RenewingLightImpact.hpar       one-shot  target root, HoT application
RenewingLightIdle.hpar         LOOPING   target root, AURA_IDLE for the 12 s HoT
RenewingLightTick.hpar         one-shot  target root, every 3 s HoT tick
ResurrectionChannel.hpar       LOOPING   caster root, cast phase of the 10 s channel
ResurrectionImpact.hpar        one-shot  revived target root
=============================  ========  ===========================================

Tuning lessons from the preview loop (engine behaviour, ``particle_emitter.cpp``):

* **There is no per-emitter offset**, and every shape except the cone is centred on the
  origin -- a sphere or box spawned at the feet puts half its particles underground. Two
  workarounds are used throughout:

  - *Flat cone ring* (``ring_motes``): a cone with almost no height spawns on a disc at
    the feet, and its spawn direction is then nearly horizontal and radial, so
    ``start_speed`` pushes particles **outward** instead of up/down. With drag the outward
    travel converges to ``start_speed / drag``, which leaves a hollow-ish ring of motes
    around the body that orbital swirl and upward velocity turn into a spiral.
  - *Hover flash* (``hover_flash``): a point particle launched upward at ``h * drag`` with
    heavy drag converges on height ``h`` within ~0.3 s. That is how a flash ends up at chest
    or head height without an emitter offset.

* Drag damps *all* velocity, including the rise. A mote that should keep climbing needs
  upward ``gravity`` (buoyancy); its terminal rise speed is ``gravity.y / drag``.
* Falling layers spawned from a tall cone overshoot the ground for particles born low in the
  column -- the same compromise ``light_shaft`` makes. Terrain hides them in game; keep the
  fall distance short so the preview does not show a long tail below the ground line.
* Ring size and alpha curves run independently over the life, so a big ring fades before it
  has visibly expanded; ``_fast_ring`` front-loads the growth (lesson from
  ``warrior_abilities``).
* The preview is on black while the game is lit: everything here sits slightly brighter
  than it looks right on the sheet.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cleric_common as cc  # noqa: E402
from cleric_common import (  # noqa: E402
    BEAM, GLOW, RING, STAR, SIGIL, RAYS, FEATHER,
    HOLY_WHITE, HOLY_GOLD, SOFT_GOLD, HEAL_GREEN,
    Burst, Emitter, ParticleSystem, color_curve, float_curve, hpar, rgba)

# Green-gold blend for the renewing (HoT) family: gold with a little life in it, so the
# HoT reads as related to the heal yet distinct from it.
LIFE_GOLD = (0.88, 1.00, 0.56)


# =========================================================================================
# Helpers (healing-specific; the shared ones live in cleric_common / warrior_common)
# =========================================================================================

def soft_shaft(name, colour, hot=HOLY_WHITE, height=5.0, radius=0.45, rate=70.0,
               duration=0.45, speed=7.0, size=0.24, lifetime=0.7, alpha=0.30,
               length=6.0, delay=0.0):
    """A gentle descending shaft: ``light_shaft`` but emitted continuously over
    ``duration`` instead of in two bursts, and slower, so the light *settles* onto the
    target instead of striking it. Same cone-from-above trick as ``light_shaft``."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=duration, start_delay=delay,
        spawn_rate=rate, max_particles=int(rate * lifetime) + 12,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, height, radius),
        min_lifetime=lifetime * 0.75, max_lifetime=lifetime,
        min_velocity=(0.0, -speed, 0.0), max_velocity=(0.0, -speed * 0.7, 0.0),
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_STRETCHED, length_scale=length,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.8), (0.3, 1.0), (1.0, 0.7)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.20, rgba(hot, alpha)),
            (0.70, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def ring_motes(name, colour, hot=HOLY_WHITE, count=24, radius=0.35, out_speed=0.5,
               rise=1.4, buoyancy=0.6, drag=1.0, size=0.14, lifetime=1.3, alpha=0.6,
               orbital=1.6, duration=0.5, rate=0.0, loop=False, material=STAR,
               render=hpar.RENDER_BILLBOARD, length=1.0, delay=0.0, warmup=0.0):
    """Motes born on a disc at the feet, pushed outward and lifted -- a spiral around the
    body (see the flat-cone note in the module docstring).

    One-shot: ``count`` spread over three bursts across ``duration``. Looping: pass
    ``loop=True`` and a ``rate``."""
    if loop:
        bursts = []
    else:
        third = count // 3
        bursts = [Burst(0.0, third), Burst(duration * 0.5, third),
                  Burst(duration, count - 2 * third)]
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=loop, duration=max(duration, 0.05), start_delay=delay, warmup_time=warmup,
        spawn_rate=rate, max_particles=max(count, int(rate * lifetime * 1.3)) + 8,
        bursts=bursts,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.06, radius),
        min_lifetime=lifetime * 0.7, max_lifetime=lifetime,
        min_velocity=(0.0, rise * 0.5, 0.0), max_velocity=(0.0, rise, 0.0),
        min_start_speed=out_speed * 0.5, max_start_speed=out_speed,
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.4, max_angular_velocity=1.4,
        gravity=(0.0, buoyancy, 0.0), drag=drag, orbital_speed=orbital,
        render_mode=render, length_scale=length,
        material_name=material,
        size_over_life=float_curve((0.0, 0.3), (0.2, 1.0), (1.0, 0.45)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.18, rgba(hot, alpha)),
            (0.60, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def hover_flash(name, height, size, colour, material=GLOW, alpha=0.5, lifetime=0.5,
                spin=0.0, drag=9.0, delay=0.0):
    """One soft billboard that shoots up from the feet and parks at ``height`` (launch speed
    ``height * drag``, heavy drag), then blooms and fades -- a flash at chest or head height
    without an emitter offset. Alpha peaks after it has arrived."""
    v = height * drag
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.05, start_delay=delay,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, v, 0.0), max_velocity=(0.0, v, 0.0),
        min_start_size=size, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=spin, max_angular_velocity=spin,
        gravity=(0.0, 0.0, 0.0), drag=drag,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=material,
        size_over_life=float_curve((0.0, 0.4), (0.4, 1.0), (1.0, 1.2)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.35, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def falling_feathers(name, colour, count=8, height=3.0, radius=1.0, fall=0.55, lift=0.0,
                     size=0.30, lifetime=1.8, alpha=0.75, duration=0.3, delay=0.0):
    """Feathers spawned through a tall cone that drift down slowly while tumbling.

    ``lift`` > 0 gives them an upward puff first (drag then settles them onto the slow
    terminal fall ``fall``), which reads as feathers shaken loose rather than dropped.
    Tumble comes from angular velocity plus a little curl noise for side-to-side sway."""
    drag = 1.6
    third = count // 3
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=duration, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 4,
        bursts=[Burst(0.0, third), Burst(duration * 0.5, third), Burst(duration, count - 2 * third)],
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, height, radius),
        min_lifetime=lifetime * 0.75, max_lifetime=lifetime,
        min_velocity=(-0.25, lift * 0.6 - 0.2, -0.25), max_velocity=(0.25, lift + 0.1, 0.25),
        min_start_size=size * 0.7, max_start_size=size,
        min_start_rotation=-0.6, max_start_rotation=0.6,
        min_angular_velocity=-1.8, max_angular_velocity=1.8,
        gravity=(0.0, -fall * drag, 0.0), drag=drag,
        noise_amplitude=1.6, noise_frequency=0.7,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=FEATHER,
        size_over_life=float_curve((0.0, 0.6), (0.2, 1.0), (1.0, 0.9)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_WHITE, 0.0)),
            (0.15, rgba(HOLY_WHITE, alpha)),
            (0.70, rgba(colour, alpha * 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def light_shower(name, colour, hot=HOLY_WHITE, height=3.8, radius=0.9, rate=60.0,
                 duration=0.4, fall=2.6, size=0.12, lifetime=1.0, alpha=0.6,
                 material=STAR, render=hpar.RENDER_BILLBOARD, length=1.0, delay=0.0):
    """Motes raining gently down through a column above the target."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=duration, start_delay=delay,
        spawn_rate=rate, max_particles=int(rate * lifetime) + 12,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, height, radius),
        min_lifetime=lifetime * 0.7, max_lifetime=lifetime,
        min_velocity=(-0.05, -fall, -0.05), max_velocity=(0.05, -fall * 0.6, 0.05),
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.5, max_angular_velocity=1.5,
        gravity=(0.0, 0.0, 0.0), orbital_speed=0.6,
        render_mode=render, length_scale=length,
        material_name=material,
        size_over_life=float_curve((0.0, 0.5), (0.25, 1.0), (1.0, 0.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.20, rgba(hot, alpha)),
            (0.65, rgba(colour, alpha * 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def _fast_ring(ring, growth_frac=0.35, growth_hold=0.75, hold_alpha_frac=0.7):
    """Front-load a ground_ring's expansion so it grows while still bright (see the
    ``warrior_abilities._fast_ring`` docstring; duplicated here to keep recipes independent)."""
    peak = ring.color_over_lifetime[1].color
    colour, alpha = peak[:3], peak[3]
    end_ratio = ring.size_over_life[-1].value
    ring.size_over_life = float_curve(
        (0.0, 1.0),
        (growth_frac, 1.0 + (end_ratio - 1.0) * growth_hold),
        (1.0, end_ratio))
    ring.color_over_lifetime = color_curve(
        (0.00, rgba(colour, 0.0)),
        (0.12, rgba(colour, alpha)),
        (growth_frac, rgba(colour, alpha * hold_alpha_frac)),
        (1.00, rgba(colour, 0.0)))
    return ring


# =========================================================================================
# Effects
# =========================================================================================

def healing_light_impact():
    """Main direct heal: warm light settles on the target, motes spiral up the body, a small
    sigil blooms underfoot. ~1.5 s."""
    haze = soft_shaft("Heal Shaft Haze", SOFT_GOLD, height=4.5, radius=0.35, rate=26.0,
                      duration=0.45, speed=5.0, size=0.75, lifetime=0.8, alpha=0.14,
                      length=3.0)
    haze.material_name = GLOW
    return ParticleSystem(emitters=[
        cc.ground_sigil("Heal Sigil", size=1.8, colour=SOFT_GOLD, alpha=0.7,
                        lifetime=1.35, spin=0.5, grow=1.12, delay=0.05),
        haze,
        soft_shaft("Heal Shaft", SOFT_GOLD, height=5.0, radius=0.5, rate=90.0,
                   duration=0.45, speed=7.0, size=0.28, lifetime=0.75, alpha=0.30),
        soft_shaft("Heal Shaft Core", HOLY_WHITE, height=5.0, radius=0.18, rate=40.0,
                   duration=0.35, speed=8.0, size=0.22, lifetime=0.65, alpha=0.42,
                   length=9.0),
        ring_motes("Heal Motes", SOFT_GOLD, count=26, radius=0.35, out_speed=0.6,
                   rise=3.0, buoyancy=0.8, drag=1.2, size=0.26, lifetime=1.2,
                   alpha=0.75, orbital=2.0, duration=0.35, delay=0.1),
        ring_motes("Heal Life Motes", HEAL_GREEN, hot=SOFT_GOLD, count=9, radius=0.3,
                   out_speed=0.5, rise=2.6, buoyancy=0.7, drag=1.2, size=0.20,
                   lifetime=1.1, alpha=0.65, orbital=2.2, duration=0.3, delay=0.25),
        hover_flash("Heal Flash", height=1.1, size=1.6, colour=SOFT_GOLD, alpha=0.55,
                    lifetime=0.55, delay=0.25),
    ])


def divine_vitality_impact():
    """Stamina blessing: a spiral column of golden motes climbs to above the head, a sigil
    underfoot, a few feathers shaken loose, and a sunburst crowning it. ~1.9 s."""
    return ParticleSystem(emitters=[
        cc.ground_sigil("Vitality Sigil", size=2.2, colour=HOLY_GOLD, alpha=0.6,
                        lifetime=1.6, spin=0.45, grow=1.1),
        ring_motes("Vitality Spiral", HOLY_GOLD, count=54, radius=0.4, out_speed=0.55,
                   rise=3.2, buoyancy=1.0, drag=1.1, size=0.24, lifetime=1.4,
                   alpha=0.75, orbital=2.6, duration=0.6),
        ring_motes("Vitality Streaks", SOFT_GOLD, count=24, radius=0.45, out_speed=0.45,
                   rise=3.8, buoyancy=0.9, drag=1.0, size=0.15, lifetime=1.0,
                   alpha=0.45, orbital=2.6, duration=0.7, material=BEAM,
                   render=hpar.RENDER_STRETCHED, length=5.0),
        falling_feathers("Vitality Feathers", HOLY_GOLD, count=8, height=2.4, radius=0.8,
                         fall=0.5, lift=1.6, size=0.40, lifetime=1.5, delay=0.2),
        hover_flash("Vitality Crown", height=2.4, size=1.6, colour=SOFT_GOLD,
                    material=RAYS, alpha=0.65, lifetime=0.7, spin=1.2, delay=0.85),
    ])


def renewing_light_impact():
    """HoT application: a gentle green-gold shower falling onto the target and a thin ring
    spreading on the ground. ~1.3 s."""
    ring = cc.ground_ring("Renew Ring", start_size=0.6, end_size=2.6, colour=LIFE_GOLD,
                          alpha=0.5, lifetime=0.9, delay=0.25)
    _fast_ring(ring)
    return ParticleSystem(emitters=[
        ring,
        light_shower("Renew Shower", LIFE_GOLD, height=3.8, radius=0.9, rate=75.0,
                     duration=0.4, fall=2.8, size=0.26, lifetime=1.0, alpha=0.75),
        light_shower("Renew Streaks", SOFT_GOLD, height=3.8, radius=0.7, rate=40.0,
                     duration=0.35, fall=4.0, size=0.15, lifetime=0.8, alpha=0.45,
                     material=BEAM, render=hpar.RENDER_STRETCHED, length=4.5),
        cc.soft_flash("Renew Ground Glow", size=1.4, colour=LIFE_GOLD, alpha=0.3,
                      lifetime=0.5),
    ])


def renewing_light_idle():
    """HoT aura idle, looping for the whole 12 s: a couple of faint motes per second rising
    slowly around the body. Has to stay quiet."""
    return ParticleSystem(emitters=[
        ring_motes("Renew Idle Motes", LIFE_GOLD, hot=SOFT_GOLD, radius=0.4,
                   out_speed=0.35, rise=0.8, buoyancy=0.35, drag=0.5, size=0.22,
                   lifetime=2.6, alpha=0.55, orbital=0.9, duration=1.0, rate=3.0,
                   loop=True, warmup=2.0),
        ring_motes("Renew Idle Glow", LIFE_GOLD, hot=SOFT_GOLD, radius=0.35,
                   out_speed=0.25, rise=0.6, buoyancy=0.3, drag=0.5, size=0.45,
                   lifetime=2.4, alpha=0.18, orbital=0.7, duration=1.0, rate=1.2,
                   loop=True, warmup=2.0, material=GLOW),
    ])


def renewing_light_tick():
    """HoT tick every 3 s: one ring of motes lifting from the feet up the body and a tiny
    soft glow at the chest. ~0.8 s."""
    return ParticleSystem(emitters=[
        ring_motes("Renew Tick Ring", LIFE_GOLD, count=20, radius=0.4, out_speed=0.4,
                   rise=4.2, buoyancy=0.6, drag=1.8, size=0.22, lifetime=0.8,
                   alpha=0.75, orbital=1.8, duration=0.05),
        hover_flash("Renew Tick Glow", height=1.1, size=0.9, colour=LIFE_GOLD, alpha=0.35,
                    lifetime=0.45, delay=0.1),
    ])


def resurrection_channel():
    """Resurrection cast phase, looping for the 10 s channel: a large rune circle that keeps
    re-drawing itself under the caster (one long-lived sigil every 1.2 s, overlapping), a
    smaller counter-turning inner circle, and slow motes and faint streaks rising."""
    sigil = Emitter(
        name="Res Channel Sigil",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.2,
        spawn_rate=0.0, max_particles=4,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=2.4, max_lifetime=2.4,
        min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
        min_start_size=2.7, max_start_size=2.7,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=0.30, max_angular_velocity=0.30,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=SIGIL,
        size_over_life=float_curve((0.0, 0.92), (0.5, 1.0), (1.0, 1.08)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_GOLD, 0.0)),
            (0.35, rgba(SOFT_GOLD, 0.45)),
            (0.65, rgba(HOLY_GOLD, 0.40)),
            (1.00, rgba(HOLY_GOLD, 0.0))),
    )
    inner = Emitter(
        name="Res Channel Inner Sigil",
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.6,
        spawn_rate=0.0, max_particles=4,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=3.2, max_lifetime=3.2,
        min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
        min_start_size=1.5, max_start_size=1.5,
        min_start_rotation=0.0, max_start_rotation=6.28,
        min_angular_velocity=-0.5, max_angular_velocity=-0.5,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=SIGIL,
        size_over_life=float_curve((0.0, 0.9), (1.0, 1.05)),
        color_over_lifetime=color_curve(
            (0.00, rgba(HOLY_WHITE, 0.0)),
            (0.40, rgba(HOLY_WHITE, 0.35)),
            (0.70, rgba(SOFT_GOLD, 0.30)),
            (1.00, rgba(SOFT_GOLD, 0.0))),
    )
    return ParticleSystem(emitters=[
        sigil,
        inner,
        ring_motes("Res Channel Motes", HOLY_GOLD, radius=1.1, out_speed=0.1, rise=0.7,
                   buoyancy=0.35, drag=0.5, size=0.24, lifetime=2.4, alpha=0.6,
                   orbital=0.5, duration=1.0, rate=9.0, loop=True),
        ring_motes("Res Channel Streaks", SOFT_GOLD, radius=1.2, out_speed=0.0, rise=1.6,
                   buoyancy=0.4, drag=0.3, size=0.14, lifetime=1.4, alpha=0.32,
                   orbital=0.3, duration=1.0, rate=5.0, loop=True, material=BEAM,
                   render=hpar.RENDER_STRETCHED, length=6.0),
    ])


def resurrection_impact():
    """The grandest cleric effect: a tall bright shaft descends on the revived player, a big
    sigil flares, feathers drift down, a sunburst blooms and motes rise and spread. ~2.6 s."""
    ring = cc.ground_ring("Res Ring", start_size=1.2, end_size=5.5, colour=HOLY_GOLD,
                          alpha=0.6, lifetime=1.2, delay=0.35)
    _fast_ring(ring)
    return ParticleSystem(emitters=[
        cc.ground_sigil("Res Sigil", size=3.0, colour=HOLY_GOLD, alpha=0.75,
                        lifetime=2.2, spin=0.4, grow=1.15, delay=0.1),
        soft_shaft("Res Shaft", SOFT_GOLD, height=7.0, radius=0.8, rate=110.0,
                   duration=0.9, speed=10.0, size=0.34, lifetime=0.75, alpha=0.30,
                   length=7.0),
        soft_shaft("Res Shaft Core", HOLY_WHITE, height=7.0, radius=0.3, rate=60.0,
                   duration=0.8, speed=12.0, size=0.28, lifetime=0.65, alpha=0.45,
                   length=11.0),
        ring,
        hover_flash("Res Sunburst", height=1.2, size=3.6, colour=HOLY_WHITE,
                    material=RAYS, alpha=0.7, lifetime=0.85, spin=0.9, delay=0.4),
        falling_feathers("Res Feathers", HOLY_GOLD, count=24, height=4.0, radius=1.6,
                         fall=0.8, lift=0.0, size=0.38, lifetime=1.9, duration=0.6,
                         delay=0.2),
        ring_motes("Res Motes", HOLY_GOLD, count=50, radius=0.5, out_speed=1.5,
                   rise=2.2, buoyancy=0.7, drag=0.9, size=0.28, lifetime=1.6,
                   alpha=0.75, orbital=1.2, duration=0.6, delay=0.4),
    ])


EFFECTS = [
    ("HealingLightImpact.hpar", healing_light_impact),
    ("DivineVitalityImpact.hpar", divine_vitality_impact),
    ("RenewingLightImpact.hpar", renewing_light_impact),
    ("RenewingLightIdle.hpar", renewing_light_idle),
    ("RenewingLightTick.hpar", renewing_light_tick),
    ("ResurrectionChannel.hpar", resurrection_channel),
    ("ResurrectionImpact.hpar", resurrection_impact),
]


if __name__ == "__main__":
    only = set(sys.argv[1:])
    for filename, build in EFFECTS:
        if only and filename not in only and os.path.splitext(filename)[0] not in only:
            continue
        cc.write(build(), filename)
