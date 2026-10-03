# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Cleric party-aura and Faithward effects. Run from the repo root::

    py -3 tools/particle_gen/recipes/cleric_auras.py

Writes into ``data/client/Particles/Cleric/``.

Art direction
-------------
Radiant stylized holy light with the shared cleric gold/ivory core. **Healing Aura** is gold
with a soft HEAL_GREEN accent; **Protective Aura** and **Faithward** lean cool
WARD_SILVER/WARD_AZURE around a gold core, so protection never reads as a heal.

The two auras are party-wide (40 yd) toggles, so there are two very different audiences:

* the **activation** (caster ROOT, one-shot) is the one big consecration moment -- a large
  turning sigil, a ground ring racing outward, motes lifting all around, a column flash;
* the **apply** and **idle** effects play on *every* party member in range, potentially five
  at once for hours. Apply is a small flash at the feet; idle is close to invisible on
  purpose -- a mote or two per second, alpha <= 0.3.

Faithward is attached to ``spine_03`` (chest), so its origin is the torso centre. The bone's
orientation is applied to SIM_LOCAL particles, so everything there is kept close to
spherically symmetric (shell + orbit) and does not depend on which way the bone's Y points.

Tuning lessons (from rendered contact sheets)
---------------------------------------------
* ``ground_ring``'s size and alpha curves run independently; a ring with a big size ratio
  has faded before it travels. ``fast_ring`` front-loads the growth (same fix as
  ``warrior_abilities._fast_ring``).
* "Spawn on a ring" has no shape of its own. A zero-height cone spawns on circles of radius
  ``baseRadius * t``; giving those particles an outward ``start_speed`` and strong ``drag``
  makes them coast out by ``speed / drag`` and settle in a band, which is how the Protective
  ward wall and the Faithward shell are built. Upward ``gravity`` then sets the terminal
  rise speed (``gravity / drag``) so the streaks turn from outward to vertical.
* Even with drag 10 the outward coast is visible for ~0.2 s, and stretched streaks follow
  velocity, so the first ward-wall sheet was a fan of radial spokes. The wall's alpha stays
  at zero for the first ~24% of life and only fades in once the streaks stand vertical.
* ``orbital_speed`` rotates *position* after integration and never touches velocity, so a
  stretched particle parked on a shell by drag has a meaningless stretch axis (the first
  Faithward sheet was a pile of randomly angled sticks). Orbiting layers are billboard stars.
* A flat cone piles spawns at the centre (radius ``baseRadius * t``, uniform ``t``), which
  made the activation motes a clump around the caster; a thin box spreads them evenly.
* ``inspect_hpar --check`` budgets ``spawn_rate`` as ``rate * max_lifetime``; one-shot
  windows shorter than the lifetime use ``staggered_bursts`` so ``max_particles`` stays
  exact instead of being padded to satisfy the check.
* Idle loops: star sprites at ~0.1 size vanished entirely in the preview; 0.2 at alpha 0.3
  with ~1.8/s is the "barely there but present" point. ``preview.py`` ignores
  ``warmup_time``, so the first idle tile is empty in the sheet but not in game.
* The preview draws the figure at the emitter origin; the chest-attached Faithward sheets
  were rendered with a wrapper that offsets particles to spine_03 height (~1.3).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cleric_common as cc  # noqa: E402
from cleric_common import (  # noqa: E402
    BEAM, GLOW, RING, STAR, SIGIL, RAYS, Burst, Emitter, ParticleSystem, color_curve,
    float_curve, hpar, rgba, HOLY_WHITE, HOLY_GOLD, SOFT_GOLD, HEAL_GREEN, WARD_SILVER,
    WARD_AZURE)


# --- helpers ---------------------------------------------------------------------------

def staggered_bursts(count, duration, steps=6):
    """Spread ``count`` spawns over ``duration`` as evenly spaced bursts.

    Used instead of ``spawn_rate`` for one-shot emission windows shorter than the particle
    lifetime: ``inspect_hpar --check`` budgets a rate as ``rate * max_lifetime`` (the
    steady-state population), which over-counts a short window; bursts are counted exactly,
    so ``max_particles`` can stay honest.
    """
    per = [count // steps + (1 if i < count % steps else 0) for i in range(steps)]
    return [Burst(duration * i / steps, n) for i, n in enumerate(per) if n > 0]


def fast_ring(name, start_size, end_size, colour, alpha=0.45, lifetime=0.8, delay=0.0,
              growth_frac=0.35, growth_hold=0.8, hold_alpha_frac=0.65):
    """A ground ring whose expansion happens while it is still bright.

    ``cc.ground_ring`` grows linearly over the whole life while alpha peaks at 15%, so a big
    ring is a dim smudge by the time it is wide. Here the size reaches ``growth_hold`` of its
    travel by ``growth_frac`` of the life, while alpha holds ``hold_alpha_frac`` of its peak.
    """
    ring = cc.ground_ring(name, start_size=start_size, end_size=end_size, colour=colour,
                          alpha=alpha, lifetime=lifetime, delay=delay)
    ratio = end_size / start_size
    ring.size_over_life = float_curve(
        (0.0, 1.0),
        (growth_frac, 1.0 + (ratio - 1.0) * growth_hold),
        (1.0, ratio))
    ring.color_over_lifetime = color_curve(
        (0.00, rgba(colour, 0.0)),
        (0.10, rgba(colour, alpha)),
        (growth_frac, rgba(colour, alpha * hold_alpha_frac)),
        (1.00, rgba(colour, 0.0)))
    return ring


def bloom_sigil(name, size, colour, alpha=0.6, lifetime=1.6, spin=0.5, delay=0.0):
    """A big ground sigil that *blooms*: opens fast from small, then settles and turns.

    ``cc.ground_sigil`` starts at 85% size; for the activation moment the circle should
    visibly unfold out of the caster's feet instead.
    """
    sigil = cc.ground_sigil(name, size=size, colour=colour, alpha=alpha, lifetime=lifetime,
                            spin=spin, delay=delay)
    sigil.size_over_life = float_curve((0.0, 0.25), (0.18, 0.92), (0.45, 1.0), (1.0, 1.06))
    sigil.color_over_lifetime = color_curve(
        (0.00, rgba(HOLY_WHITE, 0.0)),
        (0.10, rgba(HOLY_WHITE, alpha)),
        (0.35, rgba(colour, alpha)),
        (0.75, rgba(colour, alpha * 0.7)),
        (1.00, rgba(colour, 0.0)))
    return sigil


def ground_motes(name, colour, hot=HOLY_WHITE, count=48, radius=2.4, rise=2.2, size=0.34,
                 lifetime=1.2, alpha=0.75, orbital=0.5, duration=0.5, delay=0.0):
    """Star motes lifting off the whole consecrated area, not just around the body.

    A sphere would put half of them under the floor, and a flat cone spawns on circles of
    radius ``radius * t`` with uniform ``t`` -- that piles them up at the centre (the first
    contact sheet was a clump around the caster). A thin box spreads them evenly over the
    consecrated square; with star sprites and this count the square outline never reads.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=duration, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=staggered_bursts(count, duration),
        shape=hpar.SHAPE_BOX, shape_extents=(radius * 2.0, 0.2, radius * 2.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-0.08, rise * 0.45, -0.08), max_velocity=(0.08, rise, 0.08),
        min_start_size=size * 0.5, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.8, max_angular_velocity=1.8,
        gravity=(0.0, 0.3, 0.0), drag=0.3, orbital_speed=orbital,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.2, 1.0), (1.0, 0.35)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.18, rgba(hot, alpha)),
            (0.55, rgba(colour, alpha * 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def column_flash(name, colour, hot=HOLY_WHITE, count=26, radius=0.45, speed=4.5, size=0.30,
                 lifetime=0.70, alpha=0.32, length=7.0, delay=0.0):
    """A soft upward shaft out of the caster -- the 'light rising' half of consecration."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.20, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count // 2), Burst(0.10, count - count // 2)],
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.3, radius),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(0.0, speed * 0.4, 0.0), max_velocity=(0.0, speed, 0.0),
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, 0.0, 0.0), orbital_speed=1.2,
        render_mode=hpar.RENDER_STRETCHED, length_scale=length,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.8)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.15, rgba(hot, alpha)),
            (0.60, rgba(colour, alpha * 0.75)),
            (1.00, rgba(colour, 0.0))),
    )


def ward_wall(name, colour, hot=HOLY_WHITE, count=60, radius=2.0, rise=3.2, size=0.16,
              lifetime=0.9, alpha=0.42, duration=0.35, delay=0.0):
    """Streaks rising in a ring: a ward going up around the caster.

    Zero-height cone + outward start speed + strong drag parks the particles in a band at
    ~``radius``; upward gravity sets their terminal rise (``gravity / drag``), so each
    streak flicks outward and then stands up vertically -- a shimmering palisade.
    """
    drag = 10.0
    out_speed = radius * drag            # coast distance ~= speed / drag
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=duration, start_delay=delay,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=staggered_bursts(count, duration),
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.15),
        min_lifetime=lifetime * 0.65, max_lifetime=lifetime,
        min_velocity=(0.0, 0.2, 0.0), max_velocity=(0.0, 0.8, 0.0),
        min_start_speed=out_speed * 0.92, max_start_speed=out_speed * 1.05,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, rise * drag, 0.0), drag=drag, orbital_speed=0.4,
        render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.6)),
        # Invisible while coasting outward (the radial phase reads as spokes, not a wall);
        # the streak appears only once drag has parked it and it is rising vertically.
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.24, rgba(hot, 0.0)),
            (0.38, rgba(hot, alpha)),
            (0.68, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def idle_motes(name, colour, hot=HOLY_WHITE, rate=1.8, radius=0.4, rise=0.45, size=0.20,
               lifetime=1.5, alpha=0.30):
    """The barely-there loop worn by every party member for as long as the aura lasts.

    Warmup so the loop is already 'running' when it appears instead of popping in empty.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=True, duration=1.0, warmup_time=lifetime,
        spawn_rate=rate, max_particles=int(rate * lifetime) + 4,
        shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.15, radius),
        min_lifetime=lifetime * 0.7, max_lifetime=lifetime,
        min_velocity=(-0.04, rise * 0.6, -0.04), max_velocity=(0.04, rise, 0.04),
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-1.0, max_angular_velocity=1.0,
        gravity=(0.0, 0.0, 0.0), drag=0.2, orbital_speed=0.4,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.4)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.30, rgba(hot, alpha)),
            (0.65, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


def orbit_shell(name, colour, hot=HOLY_WHITE, count=30, radius=0.6, orbital=7.0, size=0.12,
                lifetime=0.8, alpha=0.6, loop=False, rate=0.0, duration=0.15):
    """Star motes pushed onto a sphere shell around the torso and whipped around it.

    One-shot by default (two bursts); ``loop=True`` with a ``rate`` makes the idle variant.

    Spawn in a small core, kick outward along the sphere's radial direction and let drag
    park them at ~``radius``; ``orbital_speed`` then spins them around the bone's Y axis.
    """
    drag = 4.0
    core = radius * 0.25
    out_speed = (radius - core) * drag
    bursts = [] if loop else [Burst(0.0, count // 2), Burst(duration * 0.5, count - count // 2)]
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=loop, duration=1.0 if loop else duration,
        warmup_time=lifetime if loop else 0.0,
        spawn_rate=rate, max_particles=int(rate * lifetime) + 4 if loop else count + 6,
        bursts=bursts,
        shape=hpar.SHAPE_SPHERE, shape_extents=(core, 0.0, 0.0),
        min_lifetime=lifetime * 0.65, max_lifetime=lifetime,
        min_velocity=(0.0, -0.1, 0.0), max_velocity=(0.0, 0.25, 0.0),
        min_start_speed=out_speed * 0.85, max_start_speed=out_speed * 1.1,
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-2.0, max_angular_velocity=2.0,
        gravity=(0.0, 0.0, 0.0), drag=drag, orbital_speed=orbital,
        # Billboard stars, never stretched: orbital_speed moves the *position* after
        # integration and never touches velocity, so once drag has parked a particle on the
        # shell its stretch axis is leftover noise and the streaks point every which way.
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=STAR,
        size_over_life=float_curve((0.0, 0.4), (0.25, 1.0), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(hot, 0.0)),
            (0.15, rgba(hot, alpha)),
            (0.60, rgba(colour, alpha * 0.8)),
            (1.00, rgba(colour, 0.0))),
    )


# --- effects ---------------------------------------------------------------------------

def healing_aura_activate():
    """Caster ROOT. Consecration: gold sigil unfolding, gold-green rings racing out, motes
    lifting off the whole area, a soft column of light rising out of the cleric."""
    return ParticleSystem(emitters=[
        bloom_sigil("Consecration Sigil", size=4.6, colour=HOLY_GOLD, alpha=0.62,
                    lifetime=1.6, spin=0.45),
        bloom_sigil("Inner Sigil", size=2.0, colour=HEAL_GREEN, alpha=0.45,
                    lifetime=1.3, spin=-0.9, delay=0.08),
        fast_ring("Gold Ring", start_size=0.8, end_size=9.5, colour=HOLY_GOLD,
                  alpha=0.6, lifetime=0.9),
        fast_ring("Green Ring", start_size=0.6, end_size=6.0, colour=HEAL_GREEN,
                  alpha=0.45, lifetime=0.75, delay=0.15),
        column_flash("Rising Light", colour=SOFT_GOLD, count=26, alpha=0.32),
        ground_motes("Life Motes", colour=HEAL_GREEN, count=44, radius=2.4, alpha=0.75),
        ground_motes("Gold Motes", colour=SOFT_GOLD, count=24, radius=1.2, alpha=0.75,
                     rise=2.4, delay=0.1),
    ])


def protective_aura_activate():
    """Caster ROOT. Same consecration structure in silver-azure around a gold core, plus a
    ring of rising streaks -- the ward going up."""
    return ParticleSystem(emitters=[
        bloom_sigil("Ward Sigil", size=4.6, colour=WARD_SILVER, alpha=0.62,
                    lifetime=1.6, spin=-0.45),
        bloom_sigil("Gold Core Sigil", size=2.0, colour=HOLY_GOLD, alpha=0.5,
                    lifetime=1.3, spin=0.9, delay=0.08),
        fast_ring("Azure Ring", start_size=0.8, end_size=9.0, colour=WARD_AZURE,
                  alpha=0.5, lifetime=0.85),
        fast_ring("Silver Ring", start_size=0.6, end_size=6.0, colour=WARD_SILVER,
                  alpha=0.38, lifetime=0.75, delay=0.15),
        ward_wall("Ward Wall", colour=WARD_AZURE, count=96, radius=2.2, lifetime=1.05,
                  duration=0.45),
        column_flash("Rising Light", colour=HOLY_GOLD, count=22, alpha=0.30),
        ground_motes("Ward Motes", colour=WARD_AZURE, count=40, radius=2.4),
    ])


def aura_apply(sigil_colour, mote_colour, accent):
    """Recipient ROOT, every party member. A small sigil flash at the feet + a few motes."""
    return ParticleSystem(emitters=[
        bloom_sigil("Apply Sigil", size=1.2, colour=sigil_colour, alpha=0.68,
                    lifetime=0.75, spin=0.8),
        cc.rising_motes("Apply Motes", colour=mote_colour, count=12, radius=0.4, rise=1.6,
                        size=0.20, lifetime=0.7, alpha=0.7, orbital=1.2, duration=0.15),
        fast_ring("Apply Ring", start_size=0.4, end_size=1.8, colour=accent,
                  alpha=0.45, lifetime=0.45),
    ])


def healing_aura_idle():
    return ParticleSystem(emitters=[
        idle_motes("Aura Motes", colour=HEAL_GREEN),
    ])


def protective_aura_idle():
    return ParticleSystem(emitters=[
        idle_motes("Aura Motes", colour=WARD_AZURE, hot=WARD_SILVER),
    ])


def faithward_apply():
    """spine_03 (chest). A protective shell flaring around the torso."""
    return ParticleSystem(emitters=[
        orbit_shell("Ward Orbit", colour=WARD_AZURE, count=36, radius=0.65, orbital=7.0,
                    lifetime=0.85, alpha=0.75, size=0.16),
        orbit_shell("Gold Orbit", colour=HOLY_GOLD, count=16, radius=0.45, orbital=-6.0,
                    lifetime=0.7, alpha=0.7, size=0.13),
        cc.ray_flash("Ward Flash", size=1.7, colour=WARD_AZURE, alpha=0.8, lifetime=0.40),
        cc.ray_flash("Gold Flash", size=0.9, colour=HOLY_GOLD, alpha=0.8, lifetime=0.28,
                     spin=-2.0),
        cc.spark_burst("Ward Sparks", count=16, speed=3.0, colour=WARD_SILVER, size=0.08,
                       lifetime=0.45, gravity=-2.0, drag=2.5),
    ])


def faithward_idle():
    """spine_03 (chest), lives 15 s. A few motes slowly circling the torso."""
    return ParticleSystem(emitters=[
        orbit_shell("Ward Orbit", colour=WARD_AZURE, hot=HOLY_GOLD, loop=True, rate=5.0,
                    radius=0.6, orbital=1.6, size=0.15, lifetime=1.6, alpha=0.45),
    ])


if __name__ == "__main__":
    cc.write(healing_aura_activate(), "HealingAuraActivate.hpar")
    cc.write(protective_aura_activate(), "ProtectiveAuraActivate.hpar")
    cc.write(aura_apply(HOLY_GOLD, HEAL_GREEN, SOFT_GOLD), "HealingAuraApply.hpar")
    cc.write(aura_apply(WARD_SILVER, WARD_AZURE, HOLY_GOLD), "ProtectiveAuraApply.hpar")
    cc.write(healing_aura_idle(), "HealingAuraIdle.hpar")
    cc.write(protective_aura_idle(), "ProtectiveAuraIdle.hpar")
    cc.write(faithward_apply(), "FaithwardApply.hpar")
    cc.write(faithward_idle(), "FaithwardIdle.hpar")
