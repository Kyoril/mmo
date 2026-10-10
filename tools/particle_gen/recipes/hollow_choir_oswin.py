# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Brother Oswin, Keeper of the Vigil (Hollow Choir, boss 1). Run from the repo root::

    py -3 tools/particle_gen/recipes/hollow_choir_oswin.py

Writes seven systems into ``data/client/Particles/HollowChoir/``.

Palette: warm candle amber and gold flame, grey-violet incense smoke, bone dust -- plus a
pale blue-green for the souls Last Vigil raises. Same rules as the mage effects (see
``warrior_common``'s docstring): no additive blending and no bloom, so volumetric layers
stay at peak alpha 0.2-0.45, sparks and single onset flashes may run bright, and motion
(orbit, sweep, stretched streaks, rings) does what glow cannot.

==============================  ===================  =========  ============================
File                            Attach               Lifetime   Notes
==============================  ===================  =========  ============================
Oswin_GraveStrikeWindup.hpar    caster node (feet)   ~1.5 s     CASTING; cone on local +X
Oswin_GraveStrikeImpact.hpar    caster node (feet)   one-shot   CAST_SUCCEEDED; cone on +X
Oswin_LastVigilCast.hpar        caster node (feet)   ~2 s       CASTING
Oswin_LastVigilRelease.hpar     caster node (feet)   one-shot   CAST_SUCCEEDED
Oswin_CandleGround.hpar         ground zone node     2.0 s      GROUND_ACTIVE, radius 3.5
Oswin_CandleFlare.hpar          ground zone node     one-shot   GROUND_EXPIRED, radius 3.5
Oswin_CandleBurn.hpar           player node (feet)   one-shot   IMPACT
==============================  ===================  =========  ============================

Every emitter is ``loop=False``: the cast kits are torn down at cast end anyway, and the
ground zone stops its emitters when the zone ends.

Engine facts the shapes here depend on (``particle_emitter.cpp``):

* **Node space.** Both simulation spaces go through the node's full transform: world-space
  particles get ``systemWorld.TransformAffine(localPos)`` and
  ``TransformDirectionAffine(localVel)`` at spawn, local-space particles are baked with the
  same matrix every frame. A unit's scene node is oriented ``Quaternion(facing, UnitY)`` and
  ``FacingToDirection`` maps (1,0,0) to (cos, 0, -sin), so **local +X is the unit's forward**
  -- the Grave Strike cone is authored along +X. The node also carries the unit's
  ``object_fields::Scale`` (``GameUnitC::OnScaleChanged``): positions and velocities scale
  with it, sprite sizes do not. The 8-unit cone is authored for scale 1.0.
* **No per-emitter offset, and every shape is centred on the origin.** Particles are put
  where they belong by velocity: a launch speed plus drag parks a particle at
  ``v0 * (1 - k*dt) / k`` (``_park`` corrects for 60 fps; at 30 fps it lands ~8 % short, at
  144 fps ~4 % long), with the colour curve hidden while it travels.
* **The velocity box is componentwise random**, so a launch "along a diagonal ray" with a
  speed range is a rectangle, not a ray. Only rays along an axis are exact (``vz = 0``), or
  a single fixed velocity (``min == max``).
* **Orbital swirl rotates position, not velocity**, around the emitter's Y axis, at a fixed
  rate for every particle. So a particle's angle is a function of its *age*: launched along
  +X and swept by ``orbital_speed``, every particle of the same age lies on the same ray, and
  a continuous stream fans out into a wedge (``_solve_fan``). Launched at the wedge edge and
  parked at the rim, it sweeps along the arc (``_solve_arc``). That is how a ``100 deg x 8``
  cone gets drawn with shapes that are all rotationally symmetric.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import (BEAM, GLOW, RING, STAR, Burst, Emitter, ParticleSystem,
                            color_curve, float_curve, hpar, rgba)

FLAME = "Particles/Particle_Flame.hmi"   # teardrop flame tongue, tip up

# --- Palette -------------------------------------------------------------------------------
CANDLE_HOT = (1.00, 0.95, 0.74)    # white-gold candle core
CANDLE_GOLD = (1.00, 0.80, 0.36)   # flame body
AMBER = (1.00, 0.58, 0.16)         # embers, the danger colour
EMBER = (1.00, 0.72, 0.30)         # sparks, hotter than the body so they pop
INCENSE = (0.60, 0.54, 0.70)       # grey-violet incense smoke, lifted so it reads in the dark
ASH = (0.62, 0.57, 0.54)           # ash and dust
BONE = (0.92, 0.88, 0.76)          # bone shards and bone dust
SOUL_HOT = (0.88, 1.00, 0.96)      # pale soul core
SOUL = (0.55, 0.95, 0.82)          # pale blue-green soul body
SOUL_DEEP = (0.40, 0.78, 0.86)     # soul fade

ROOT = wc.ROOT
OUT_DIR = os.path.join(ROOT, "data", "client", "Particles", "HollowChoir")

# Grave Strike cone: 100 degrees wide, 8 units forward along local +X.
CONE_HALF = math.radians(50.0)
CONE_REACH = 8.0
# Guttering Candle zone radius.
CANDLE_RADIUS = 3.5
# Ring sprite: the bright band sits at ~62 % of the quad half-size.
RING_BAND = 0.62

FPS = 60.0


# =========================================================================================
# Helpers
# =========================================================================================

def _e(**kw):
    """An emitter with the defaults every effect here wants: one-shot, no gravity, no
    implicit velocity, a random sprite roll, and a particle cap derived from the bursts and
    the spawn rate. Override anything by keyword."""
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


def _park(distance, drag):
    """Launch speed that comes to rest ``distance`` away under ``drag`` (60 fps Euler)."""
    return distance * drag / (1.0 - drag / FPS)


def _fade(colour, alpha, peak=0.15, hold=None, end=None, hidden=0.0):
    """Invisible for the first ``hidden`` of life, fade in to ``alpha`` at ``peak``, hold
    (optionally to ``hold``), then fade to zero shifting toward ``end``."""
    end = end or colour
    keys = [(0.0, rgba(colour, 0.0))]
    if hidden > 0.0:
        keys.append((hidden, rgba(colour, 0.0)))
    keys.append((peak, rgba(colour, alpha)))
    if hold is not None:
        keys.append((hold, rgba(end, alpha * 0.85)))
    keys.append((1.0, rgba(end, 0.0)))
    return color_curve(*keys)


def _flash(name, size, colour, alpha=0.7, lifetime=0.16, lift=0.0, forward=0.0, delay=0.0):
    """One soft onset blob, parked ``lift`` units up and ``forward`` units along +X."""
    drag = 14.0 if (lift > 0.0 or forward > 0.0) else 0.0
    v = (_park(forward, drag) if drag else 0.0, _park(lift, drag) if drag else 0.0, 0.0)
    return _e(name=name, start_delay=delay, duration=0.05, bursts=[Burst(0.0, 1)],
              max_particles=2,
              min_lifetime=lifetime, max_lifetime=lifetime,
              min_velocity=v, max_velocity=v, drag=drag,
              min_start_size=size, max_start_size=size,
              size_over_life=float_curve((0.0, 0.55), (0.3, 1.0), (1.0, 1.3)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(colour, alpha)),
                  (0.35, rgba(colour, alpha * 0.65)),
                  (1.00, rgba(colour, 0.0))))


def _sim(vx, vz, drag, omega, ages, fps=FPS):
    """Mirror of the engine's per-step update for one particle in the XZ plane (drag ->
    integrate -> orbital rotation). Returns ``(x, z, unwrapped_angle)`` at each requested
    age."""
    dt = 1.0 / fps
    x = z = 0.0
    out = []
    age = 0.0
    targets = sorted(ages)
    i = 0
    ca, sa = math.cos(omega * dt), math.sin(omega * dt)
    unwrapped = None
    while i < len(targets):
        age += dt
        damp = max(0.0, 1.0 - drag * dt)
        vx *= damp
        vz *= damp
        x += vx * dt
        z += vz * dt
        x, z = x * ca - z * sa, x * sa + z * ca
        a = math.atan2(z, x)
        if unwrapped is None:
            unwrapped = a
        else:
            d = a - math.atan2(math.sin(unwrapped), math.cos(unwrapped))
            d = (d + math.pi) % (2.0 * math.pi) - math.pi
            unwrapped += d
        while i < len(targets) and age >= targets[i] - 1e-9:
            out.append((x, z, unwrapped))
            i += 1
    return out


def _angle(p):
    return p[2]


def _solve_fan(sweep, lifetime, drag):
    """Orbital speed that turns a particle launched along +X by ``sweep`` radians over
    ``lifetime``. Angle depends only on age (the motion is linear in the launch speed), so
    this one number fans a continuous stream into a wedge from 0 to ``sweep``."""
    lo, hi = 0.0, 6.0
    for _ in range(50):
        mid = 0.5 * (lo + hi)
        (p,) = _sim(1.0, 0.0, drag, mid, [lifetime])
        if _angle(p) < sweep:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def _solve_arc(radius, half_angle, drag, lifetime, visible_from):
    """Launch for a particle that parks on the arc at ``radius`` and sweeps from
    ``-half_angle`` (at ``visible_from`` of its life) to ``+half_angle`` (at death) under
    positive orbital speed. Returns ``(omega, launch_angle, speed)``."""
    a0, a1 = visible_from * lifetime, lifetime
    lo, hi = 0.0, 6.0
    for _ in range(50):
        mid = 0.5 * (lo + hi)
        p0, p1 = _sim(1.0, 0.0, drag, mid, [a0, a1])
        sweep = _angle(p1) - _angle(p0)
        if sweep < 2.0 * half_angle:
            lo = mid
        else:
            hi = mid
    omega = 0.5 * (lo + hi)
    p0, p_mid = _sim(1.0, 0.0, drag, omega, [a0, 0.5 * (a0 + a1)])
    launch = -half_angle - _angle(p0)
    speed = radius / math.hypot(p_mid[0], p_mid[1])
    return omega, launch, speed


def _mirror_z(emitter, name):
    """A copy of ``emitter`` mirrored across the XZ forward axis (z -> -z, orbit reversed)."""
    import copy
    m = copy.deepcopy(emitter)
    m.name = name
    lo, hi = emitter.min_velocity, emitter.max_velocity
    m.min_velocity = (lo[0], lo[1], -hi[2])
    m.max_velocity = (hi[0], hi[1], -lo[2])
    m.orbital_speed = -emitter.orbital_speed
    return m


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)" % (path, len(system.emitters), os.path.getsize(path)))


# =========================================================================================
# 1. Grave Strike wind-up -- 1.5 s CASTING telegraph of a 100 deg x 8 cone on local +X
# =========================================================================================

def _edge_ray(name, side, speed, lifetime, rate, size, alpha, delay=0.0, duration=1.5,
              colour=EMBER, length=4.5, lift=0.06):
    """Embers racing out along one cone edge at constant speed -- exact and frame-rate
    independent (no drag): a fixed velocity (min == max) on the edge direction."""
    vx = speed * math.cos(CONE_HALF)
    vz = side * speed * math.sin(CONE_HALF)
    return _e(name=name, start_delay=delay, duration=duration,
              spawn_rate=rate,
              min_lifetime=lifetime * 0.93, max_lifetime=lifetime,
              min_velocity=(vx, lift, vz), max_velocity=(vx, lift * 2.0, vz),
              min_start_size=size * 0.7, max_start_size=size,
              render_mode=hpar.RENDER_STRETCHED, length_scale=length,
              material_name=BEAM,
              size_over_life=float_curve((0.0, 0.6), (0.15, 1.0), (1.0, 0.7)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(CANDLE_HOT, 0.0)),
                  (0.08, rgba(CANDLE_HOT, alpha)),
                  (0.55, rgba(colour, alpha * 0.95)),
                  (0.90, rgba(AMBER, alpha * 0.7)),
                  (1.00, rgba(AMBER, 0.0))))


def grave_strike_windup():
    duration = 1.5

    # --- Edges: two lines of embers drawn outward along +-50 deg, reaching 8 at death.
    edge_life = 0.75
    edge_speed = CONE_REACH / edge_life
    edge_l = _edge_ray("Edge Embers L", +1, edge_speed, edge_life, rate=34, size=0.13,
                       alpha=0.95)
    edge_r = _edge_ray("Edge Embers R", -1, edge_speed, edge_life, rate=34, size=0.13,
                       alpha=0.95)
    # Second, bigger and brighter pass on the edges for the last 0.6 s: the edge intensifies
    # right before the strike lands.
    surge_l = _edge_ray("Edge Surge L", +1, edge_speed * 1.25, edge_life / 1.25, rate=40,
                        size=0.20, alpha=1.0, delay=0.9, duration=0.6, colour=CANDLE_GOLD,
                        length=5.5)
    surge_r = _edge_ray("Edge Surge R", -1, edge_speed * 1.25, edge_life / 1.25, rate=40,
                        size=0.20, alpha=1.0, delay=0.9, duration=0.6, colour=CANDLE_GOLD,
                        length=5.5)

    # --- Front arc: motes parked on the 8-unit rim, sweeping across the 100 deg arc.
    arc_drag, arc_life, arc_vis = 5.0, 1.15, 0.42
    omega, launch, speed = _solve_arc(CONE_REACH, CONE_HALF, arc_drag, arc_life, arc_vis)
    vx, vz = speed * math.cos(launch), speed * math.sin(launch)
    arc = _e(name="Front Arc", duration=duration,
             spawn_rate=62.0,
             min_lifetime=arc_life, max_lifetime=arc_life,
             min_velocity=(vx, 0.25, vz), max_velocity=(vx, 0.45, vz),
             min_start_size=0.55, max_start_size=0.85,
             min_angular_velocity=-1.5, max_angular_velocity=1.5,
             drag=arc_drag, orbital_speed=omega,
             material_name=GLOW,
             size_over_life=float_curve((0.0, 1.0), (arc_vis, 1.0), (0.7, 1.15), (1.0, 0.8)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(CANDLE_HOT, 0.0)),
                 (arc_vis, rgba(CANDLE_HOT, 0.0)),
                 (arc_vis + 0.06, rgba(CANDLE_GOLD, 0.62)),
                 (0.80, rgba(AMBER, 0.55)),
                 (1.00, rgba(AMBER, 0.0))))
    arc_back = _mirror_z(arc, "Front Arc Back")

    # --- Fill: a stream launched along +X and swept by orbit fans into the wedge.
    fan_life, fan_drag = 1.1, 2.6
    fan_omega = _solve_fan(CONE_HALF, fan_life, fan_drag)
    (unit,) = _sim(1.0, 0.0, fan_drag, fan_omega, [fan_life * 0.8])
    fan_reach = math.hypot(unit[0], unit[1])   # rest distance per unit launch speed

    def fan_speed(distance):
        return distance / fan_reach

    near, far = 0.7, CONE_REACH * 0.97
    fan_embers = _e(name="Fan Embers L", duration=duration,
                    spawn_rate=30.0,
                    min_lifetime=fan_life * 0.97, max_lifetime=fan_life,
                    min_velocity=(fan_speed(near), 0.10, 0.0),
                    max_velocity=(fan_speed(far), 0.55, 0.0),
                    min_start_size=0.06, max_start_size=0.11,
                    gravity=(0.0, 0.0, 0.0),
                    drag=fan_drag, orbital_speed=fan_omega,
                    render_mode=hpar.RENDER_STRETCHED, length_scale=3.5,
                    material_name=BEAM,
                    size_over_life=float_curve((0.0, 1.0), (1.0, 0.6)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(CANDLE_HOT, 0.0)),
                        (0.06, rgba(CANDLE_HOT, 1.0)),
                        (0.45, rgba(EMBER, 0.95)),
                        (0.85, rgba(AMBER, 0.7)),
                        (1.00, rgba(AMBER, 0.0))))
    fan_embers_r = _mirror_z(fan_embers, "Fan Embers R")

    fan_glow = _e(name="Fan Floor Glow L", duration=duration,
                  spawn_rate=30.0,
                  min_lifetime=fan_life * 0.97, max_lifetime=fan_life,
                  min_velocity=(fan_speed(0.9), 0.02, 0.0),
                  max_velocity=(fan_speed(CONE_REACH * 0.86), 0.04, 0.0),
                  min_start_size=1.7, max_start_size=2.6,
                  drag=fan_drag, orbital_speed=fan_omega,
                  render_mode=hpar.RENDER_HORIZONTAL,
                  size_over_life=float_curve((0.0, 0.8), (0.2, 1.0), (1.0, 1.15)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(CANDLE_GOLD, 0.0)),
                      (0.06, rgba(CANDLE_GOLD, 0.20)),
                      (0.88, rgba(AMBER, 0.18)),
                      (1.00, rgba(AMBER, 0.0))))
    fan_glow_r = _mirror_z(fan_glow, "Fan Floor Glow R")

    # Far fill, starting later: the outer half of the cone fills in as the cast goes on,
    # so the whole wedge reads at full strength by the time the strike lands.
    far_glow = _e(name="Fan Far Glow L", duration=duration - 0.45, start_delay=0.45,
                  spawn_rate=24.0,
                  min_lifetime=fan_life * 0.97, max_lifetime=fan_life,
                  min_velocity=(fan_speed(CONE_REACH * 0.5), 0.02, 0.0),
                  max_velocity=(fan_speed(CONE_REACH * 0.9), 0.04, 0.0),
                  min_start_size=2.2, max_start_size=3.2,
                  drag=fan_drag, orbital_speed=fan_omega,
                  render_mode=hpar.RENDER_HORIZONTAL,
                  size_over_life=float_curve((0.0, 0.8), (0.2, 1.0), (1.0, 1.1)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(CANDLE_GOLD, 0.0)),
                      (0.06, rgba(CANDLE_GOLD, 0.22)),
                      (0.88, rgba(AMBER, 0.20)),
                      (1.00, rgba(AMBER, 0.0))))
    far_glow_r = _mirror_z(far_glow, "Fan Far Glow R")

    # --- At the caster: incense smoke curling up and a slow ember swirl.
    incense = _e(name="Incense Smoke", duration=duration,
                 spawn_rate=16.0,
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.6, 0.7),
                 min_lifetime=1.0, max_lifetime=1.5,
                 min_velocity=(-0.15, 0.35, -0.15), max_velocity=(0.15, 0.8, 0.15),
                 min_start_size=0.9, max_start_size=1.4,
                 min_angular_velocity=-0.6, max_angular_velocity=0.6,
                 gravity=(0.0, 0.2, 0.0), drag=0.5, orbital_speed=1.1,
                 noise_amplitude=1.2, noise_frequency=0.8,
                 size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.6)),
                 color_over_lifetime=_fade(INCENSE, 0.32, peak=0.3))
    swirl = _e(name="Caster Ember Swirl", duration=duration,
               spawn_rate=30.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.6, 0.75),
               min_lifetime=0.6, max_lifetime=0.95,
               min_velocity=(-0.2, 0.4, -0.2), max_velocity=(0.2, 1.1, 0.2),
               min_start_size=0.05, max_start_size=0.09,
               gravity=(0.0, 0.3, 0.0), drag=0.4, orbital_speed=2.6,
               render_mode=hpar.RENDER_STRETCHED, length_scale=3.0,
               material_name=BEAM,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(CANDLE_HOT, 0.0)),
                   (0.12, rgba(EMBER, 1.0)),
                   (0.65, rgba(AMBER, 0.8)),
                   (1.00, rgba(AMBER, 0.0))))

    return ParticleSystem(emitters=[
        fan_glow, fan_glow_r, far_glow, far_glow_r, incense,
        arc, arc_back, fan_embers, fan_embers_r,
        edge_l, edge_r, surge_l, surge_r, swirl,
    ])


# =========================================================================================
# 2. Grave Strike impact -- forward dust / ash shockwave over the cone
# =========================================================================================

def _wedge_burst(name, count, centre, spread, drag, **kw):
    """Burst whose velocities fill the disc ``centre +- spread`` around (centre, 0) on +X:
    a constant +X velocity plus a horizontal radial kick from a zero-height cone. Every
    direction from the origin through that disc lies inside the wedge when
    ``spread <= centre * sin(50 deg)``, so the particles fan out inside the cone."""
    params = dict(name=name, simulation_space=hpar.SIM_WORLD,
                  bursts=[Burst(0.0, count)],
                  shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.05),
                  drag=drag,
                  min_velocity=(centre, 0.0, 0.0), max_velocity=(centre, 0.0, 0.0),
                  min_start_speed=0.0, max_start_speed=spread)
    params.update(kw)
    return _e(**params)


def grave_strike_impact():
    sin_half = math.sin(CONE_HALF)
    # Shock ring: the inscribed circle of the cone, rolling forward as it grows. Velocity is
    # a constant +X push plus a radial kick of exactly sin(50) of it, so every particle lies
    # on a circle tangent to both cone edges at all times.
    ring_drag = 3.0
    ring_centre = _park(CONE_REACH / (1.0 + sin_half), ring_drag) * 0.98
    shock = _wedge_burst("Dust Shock Front", 60, ring_centre, ring_centre * sin_half, ring_drag,
                         min_start_speed=ring_centre * sin_half * 0.93,
                         max_start_speed=ring_centre * sin_half,
                         min_velocity=(ring_centre, 0.4, 0.0),
                         max_velocity=(ring_centre, 2.4, 0.0),
                         min_lifetime=0.65, max_lifetime=1.0,
                         min_start_size=1.0, max_start_size=1.7,
                         min_angular_velocity=-1.2, max_angular_velocity=1.2,
                         gravity=(0.0, -0.5, 0.0),
                         size_over_life=float_curve((0.0, 0.4), (0.35, 1.0), (1.0, 1.5)),
                         color_over_lifetime=color_curve(
                             (0.00, rgba(BONE, 0.0)),
                             (0.08, rgba(BONE, 0.45)),
                             (0.45, rgba(ASH, 0.32)),
                             (1.00, rgba(ASH, 0.0))))
    body = _wedge_burst("Ash Body", 34, ring_centre * 0.8, ring_centre * 0.8 * sin_half,
                        ring_drag,
                        min_velocity=(ring_centre * 0.8, 0.1, 0.0),
                        max_velocity=(ring_centre * 0.8, 1.4, 0.0),
                        min_lifetime=0.8, max_lifetime=1.25,
                        min_start_size=1.0, max_start_size=1.7,
                        min_angular_velocity=-0.8, max_angular_velocity=0.8,
                        gravity=(0.0, 0.25, 0.0),
                        noise_amplitude=1.0, noise_frequency=0.7,
                        size_over_life=float_curve((0.0, 0.4), (0.35, 1.0), (1.0, 1.6)),
                        color_over_lifetime=color_curve(
                            (0.00, rgba(ASH, 0.0)),
                            (0.15, rgba(ASH, 0.30)),
                            (0.60, rgba(INCENSE, 0.20)),
                            (1.00, rgba(INCENSE, 0.0))))

    # Dust racing out along the two edges (constant velocity, exact rays) to the 8 rim.
    edge = []
    for side, label in ((+1, "L"), (-1, "R")):
        s = 12.0
        vx, vz = s * math.cos(CONE_HALF), side * s * math.sin(CONE_HALF)
        edge.append(_e(name="Edge Dust " + label, simulation_space=hpar.SIM_WORLD,
                       duration=0.30, spawn_rate=55.0,
                       min_lifetime=CONE_REACH / s * 0.9, max_lifetime=CONE_REACH / s,
                       min_velocity=(vx, 0.3, vz), max_velocity=(vx, 1.2, vz),
                       min_start_size=0.8, max_start_size=1.3,
                       min_angular_velocity=-1.5, max_angular_velocity=1.5,
                       size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.4)),
                       color_over_lifetime=color_curve(
                           (0.00, rgba(BONE, 0.0)),
                           (0.10, rgba(BONE, 0.32)),
                           (0.70, rgba(ASH, 0.22)),
                           (1.00, rgba(ASH, 0.0)))))

    # Shock ring on the floor: one ring particle carried forward to the inscribed centre
    # while it grows to the inscribed radius. The size curve follows the drag decay of the
    # centre, so the ring stays tangent to both cone edges and never reaches behind Oswin.
    ring_life = 0.8
    ring_end = 2.0 * (CONE_REACH * sin_half / (1.0 + sin_half)) / RING_BAND
    ring_start = 0.6
    reach_end = 1.0 - math.exp(-ring_drag * ring_life)
    size_keys = []
    for u in (0.0, 0.08, 0.2, 0.35, 0.55, 0.75, 1.0):
        frac = (1.0 - math.exp(-ring_drag * ring_life * u)) / reach_end
        size_keys.append((u, max(ring_start, ring_end * frac * reach_end) / ring_start))
    floor_ring = _e(name="Floor Shock Ring", duration=0.05, bursts=[Burst(0.0, 1)],
                    max_particles=2,
                    min_lifetime=ring_life, max_lifetime=ring_life,
                    min_velocity=(ring_centre, 0.04, 0.0), max_velocity=(ring_centre, 0.04, 0.0),
                    drag=ring_drag,
                    min_start_size=ring_start, max_start_size=ring_start,
                    render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                    size_over_life=float_curve(*size_keys),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(CANDLE_HOT, 0.0)),
                        (0.06, rgba(CANDLE_GOLD, 0.70)),
                        (0.45, rgba(AMBER, 0.50)),
                        (1.00, rgba(AMBER, 0.0))))

    # Bone shards: bright stretched splinters thrown forward and up, falling back.
    shards = _wedge_burst("Bone Shards", 34, 9.0, 9.0 * sin_half * 0.95, 1.2,
                          min_velocity=(9.0, 2.0, 0.0), max_velocity=(9.0, 6.0, 0.0),
                          min_lifetime=0.45, max_lifetime=0.8,
                          min_start_size=0.08, max_start_size=0.15,
                          gravity=(0.0, -12.0, 0.0),
                          render_mode=hpar.RENDER_STRETCHED, length_scale=4.5,
                          material_name=BEAM,
                          size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
                          color_over_lifetime=color_curve(
                              (0.00, rgba(BONE, 1.0)),
                              (0.60, rgba(BONE, 0.9)),
                              (1.00, rgba(ASH, 0.0))))
    sparks = _wedge_burst("Ember Sparks", 30, 11.0, 11.0 * sin_half * 0.95, 1.6,
                          min_velocity=(11.0, 1.0, 0.0), max_velocity=(11.0, 4.5, 0.0),
                          min_lifetime=0.35, max_lifetime=0.7,
                          min_start_size=0.07, max_start_size=0.12,
                          gravity=(0.0, -8.0, 0.0),
                          render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
                          material_name=BEAM,
                          size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                          color_over_lifetime=color_curve(
                              (0.00, rgba(CANDLE_HOT, 1.0)),
                              (0.40, rgba(EMBER, 1.0)),
                              (1.00, rgba(AMBER, 0.0))))
    # Scorch on the floor where the blade meets stone, just ahead of Oswin.
    scorch = _flash("Strike Scorch", 3.2, CANDLE_GOLD, alpha=0.55, lifetime=0.45, forward=1.6)
    scorch.render_mode = hpar.RENDER_HORIZONTAL
    scorch.min_velocity = (scorch.min_velocity[0], 0.3, 0.0)
    scorch.max_velocity = scorch.min_velocity
    # Ash smoke rolling up out of the cone after everything else (the tail).
    smoke = _wedge_burst("Ash Smoke Tail", 24, 3.6, 3.6 * sin_half * 0.9, 1.6,
                         start_delay=0.18,
                         min_velocity=(3.6, 0.5, 0.0), max_velocity=(3.6, 1.2, 0.0),
                         min_lifetime=1.0, max_lifetime=1.45,
                         min_start_size=1.2, max_start_size=1.9,
                         min_angular_velocity=-0.6, max_angular_velocity=0.6,
                         gravity=(0.0, 0.35, 0.0),
                         noise_amplitude=1.0, noise_frequency=0.6,
                         size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.6)),
                         color_over_lifetime=_fade(INCENSE, 0.30, peak=0.3))
    return ParticleSystem(emitters=[
        scorch, floor_ring, smoke, body, shock, edge[0], edge[1],
        _flash("Strike Flash", 2.6, CANDLE_HOT, alpha=0.8, lifetime=0.16, lift=0.9, forward=1.4),
        sparks, shards,
    ])


# =========================================================================================
# 3. Last Vigil cast -- 2 s CASTING, souls rising from the floor around Oswin
# =========================================================================================

def last_vigil_cast():
    duration = 1.9
    wisps = _e(name="Soul Wisps", duration=duration,
               spawn_rate=34.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.4),
               min_lifetime=0.8, max_lifetime=1.2,
               min_velocity=(-0.1, 1.4, -0.1), max_velocity=(0.1, 2.6, 0.1),
               min_start_size=0.14, max_start_size=0.24,
               gravity=(0.0, 0.4, 0.0), drag=0.3, orbital_speed=1.4,
               noise_amplitude=3.0, noise_frequency=1.6,
               render_mode=hpar.RENDER_STRETCHED, length_scale=3.5,
               material_name=BEAM,
               size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 0.5)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(SOUL_HOT, 0.0)),
                   (0.15, rgba(SOUL_HOT, 0.55)),
                   (0.55, rgba(SOUL, 0.45)),
                   (1.00, rgba(SOUL_DEEP, 0.0))))
    heads = _e(name="Soul Heads", duration=duration,
               spawn_rate=12.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.2),
               min_lifetime=1.0, max_lifetime=1.4,
               min_velocity=(-0.1, 0.9, -0.1), max_velocity=(0.1, 1.7, 0.1),
               min_start_size=0.40, max_start_size=0.62,
               gravity=(0.0, 0.3, 0.0), drag=0.3, orbital_speed=1.4,
               noise_amplitude=3.0, noise_frequency=1.6,
               size_over_life=float_curve((0.0, 0.3), (0.25, 1.0), (1.0, 0.6)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(SOUL_HOT, 0.0)),
                   (0.20, rgba(SOUL_HOT, 0.60)),
                   (0.60, rgba(SOUL, 0.42)),
                   (1.00, rgba(SOUL_DEEP, 0.0))))
    floor_mist = _e(name="Grave Mist", duration=duration,
                    spawn_rate=9.0,
                    shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 1.8),
                    min_lifetime=1.0, max_lifetime=1.4,
                    min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.05, 0.0),
                    min_start_size=2.0, max_start_size=3.0,
                    min_angular_velocity=-0.5, max_angular_velocity=0.5,
                    render_mode=hpar.RENDER_HORIZONTAL,
                    size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.2)),
                    color_over_lifetime=_fade(SOUL, 0.30, peak=0.3, end=SOUL_DEEP))
    ring_size = 2.0 * 2.4 / RING_BAND
    grave_ring = _e(name="Grave Ring", duration=1.5,
                    bursts=[Burst(0.0, 1), Burst(0.7, 1), Burst(1.4, 1)],
                    min_lifetime=0.7, max_lifetime=0.7,
                    min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
                    min_start_size=ring_size * 1.25, max_start_size=ring_size * 1.25,
                    min_angular_velocity=-0.4, max_angular_velocity=0.4,
                    render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                    size_over_life=float_curve((0.0, 1.0), (1.0, 0.72)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(SOUL, 0.0)),
                        (0.35, rgba(SOUL_HOT, 0.45)),
                        (1.00, rgba(SOUL, 0.0))))
    candles = _e(name="Candle Motes", duration=duration,
                 spawn_rate=7.0,
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.8, 1.4),
                 min_lifetime=0.9, max_lifetime=1.3,
                 min_velocity=(-0.1, 0.2, -0.1), max_velocity=(0.1, 0.5, 0.1),
                 min_start_size=0.16, max_start_size=0.26,
                 min_angular_velocity=-2.0, max_angular_velocity=2.0,
                 orbital_speed=0.8,
                 material_name=STAR,
                 size_over_life=float_curve((0.0, 0.3), (0.3, 1.0), (1.0, 0.4)),
                 color_over_lifetime=_fade(CANDLE_GOLD, 0.9, peak=0.2, hold=0.6, end=AMBER))
    return ParticleSystem(emitters=[floor_mist, grave_ring, wisps, heads, candles])


# =========================================================================================
# 4. Last Vigil release -- ghost burst around the caster
# =========================================================================================

def last_vigil_release():
    ghosts = _e(name="Ghost Burst", simulation_space=hpar.SIM_WORLD,
                bursts=[Burst(0.0, 44)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.3),
                min_lifetime=0.7, max_lifetime=1.1,
                min_velocity=(0.0, 2.0, 0.0), max_velocity=(0.0, 6.0, 0.0),
                min_start_speed=4.0, max_start_speed=9.0,
                min_start_size=0.16, max_start_size=0.28,
                drag=2.4, gravity=(0.0, 0.8, 0.0), orbital_speed=1.2,
                noise_amplitude=2.0, noise_frequency=1.2,
                render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(SOUL_HOT, 0.9)),
                    (0.35, rgba(SOUL, 0.7)),
                    (1.00, rgba(SOUL_DEEP, 0.0))))
    veil = _e(name="Ghost Veil", simulation_space=hpar.SIM_WORLD,
              bursts=[Burst(0.0, 24)],
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 0.4),
              min_lifetime=0.8, max_lifetime=1.3,
              min_velocity=(0.0, 1.5, 0.0), max_velocity=(0.0, 4.0, 0.0),
              min_start_speed=3.0, max_start_speed=6.0,
              min_start_size=0.9, max_start_size=1.5,
              min_angular_velocity=-1.0, max_angular_velocity=1.0,
              drag=2.0, gravity=(0.0, 0.5, 0.0),
              size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 1.5)),
              color_over_lifetime=_fade(SOUL, 0.32, peak=0.12, end=SOUL_DEEP))
    shock = _e(name="Ghost Ring", duration=0.05, bursts=[Burst(0.0, 1)], max_particles=2,
               min_lifetime=0.6, max_lifetime=0.6,
               min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.04, 0.0),
               min_start_size=1.5, max_start_size=1.5,
               render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
               size_over_life=float_curve((0.0, 1.0), (0.35, 9.0), (1.0, 12.0)),
               color_over_lifetime=color_curve(
                   (0.00, rgba(SOUL_HOT, 0.0)),
                   (0.08, rgba(SOUL_HOT, 0.7)),
                   (0.40, rgba(SOUL, 0.45)),
                   (1.00, rgba(SOUL, 0.0))))
    air_ring = _e(name="Ghost Shock", duration=0.05, bursts=[Burst(0.0, 1)], max_particles=2,
                  min_lifetime=0.4, max_lifetime=0.4,
                  min_velocity=(0.0, _park(1.1, 14.0), 0.0),
                  max_velocity=(0.0, _park(1.1, 14.0), 0.0), drag=14.0,
                  min_start_size=1.0, max_start_size=1.0,
                  material_name=RING,
                  size_over_life=float_curve((0.0, 1.0), (0.35, 3.6), (1.0, 4.6)),
                  color_over_lifetime=color_curve(
                      (0.00, rgba(SOUL_HOT, 0.0)),
                      (0.10, rgba(SOUL_HOT, 0.6)),
                      (1.00, rgba(SOUL, 0.0))))
    motes = _e(name="Soul Motes", simulation_space=hpar.SIM_WORLD,
               duration=0.6, spawn_rate=26.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.3, 2.4),
               min_lifetime=1.1, max_lifetime=1.6,
               min_velocity=(-0.2, 0.8, -0.2), max_velocity=(0.2, 1.8, 0.2),
               min_start_size=0.18, max_start_size=0.32,
               min_angular_velocity=-2.0, max_angular_velocity=2.0,
               gravity=(0.0, 0.3, 0.0), drag=0.4,
               material_name=STAR,
               size_over_life=float_curve((0.0, 0.3), (0.25, 1.0), (1.0, 0.3)),
               color_over_lifetime=_fade(SOUL_HOT, 0.85, peak=0.15, hold=0.6, end=SOUL))
    embers = wc.spark_burst("Candle Embers", 16, 4.0, EMBER, size=0.08, lifetime=0.8,
                            gravity=-3.0, drag=1.5)
    return ParticleSystem(emitters=[
        shock, veil, ghosts, air_ring,
        _flash("Vigil Flash", 2.4, SOUL_HOT, alpha=0.7, lifetime=0.18, lift=1.1),
        embers, motes,
    ])


# =========================================================================================
# 5. Guttering Candle ground -- 2.0 s GROUND_ACTIVE, rim 3.5 on an unrotated node
# =========================================================================================

def _rim_park(emitter, radius, drag, lift):
    """Throw particles horizontally from the centre so they come to rest on the rim at
    ``radius`` (zero-height cone: the start-speed direction is horizontal and radial) and
    ``lift`` up. Pair with a colour curve that is hidden while they travel."""
    s = _park(radius, drag)
    vy = _park(lift, drag)
    emitter.shape = hpar.SHAPE_CONE
    emitter.shape_extents = (0.0, 0.0, 0.02)
    emitter.min_start_speed = s * 0.985
    emitter.max_start_speed = s * 1.015
    emitter.min_velocity = (0.0, vy * 0.9, 0.0)
    emitter.max_velocity = (0.0, vy * 1.1, 0.0)
    emitter.drag = drag
    return emitter


def candle_ground():
    life = 2.0
    ring_size = 2.0 * CANDLE_RADIUS / RING_BAND
    rim = _e(name="Rim", duration=0.05, bursts=[Burst(0.0, 1)], max_particles=2,
             min_lifetime=life + 0.15, max_lifetime=life + 0.15,
             min_velocity=(0.0, 0.03, 0.0), max_velocity=(0.0, 0.03, 0.0),
             min_start_size=ring_size, max_start_size=ring_size,
             min_angular_velocity=0.25, max_angular_velocity=0.25,
             render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
             size_over_life=float_curve((0.0, 0.92), (0.06, 1.0), (1.0, 1.0)),
             color_over_lifetime=color_curve(
                 (0.00, rgba(CANDLE_HOT, 0.0)),
                 (0.04, rgba(CANDLE_HOT, 0.75)),
                 (0.22, rgba(CANDLE_GOLD, 0.62)),
                 (0.85, rgba(AMBER, 0.80)),
                 (1.00, rgba(AMBER, 0.0))))
    # Warning pulses closing in on the rim -- tension.
    pulses = _e(name="Rim Pulses", duration=1.5,
                bursts=[Burst(0.0, 1), Burst(0.55, 1), Burst(1.0, 1), Burst(1.35, 1)],
                min_lifetime=0.5, max_lifetime=0.5,
                min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.04, 0.0),
                min_start_size=ring_size * 1.15, max_start_size=ring_size * 1.15,
                render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                size_over_life=float_curve((0.0, 1.0), (0.6, 1.03 / 1.15), (1.0, 1.0 / 1.15)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(AMBER, 0.0)),
                    (0.75, rgba(CANDLE_GOLD, 0.42)),
                    (1.00, rgba(CANDLE_HOT, 0.0))))
    # Floor glow: soft discs accumulating over the 2 s, each getting brighter with age.
    fill = _e(name="Floor Glow", duration=1.7, spawn_rate=20.0,
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, 2.3),
              min_lifetime=life * 0.75, max_lifetime=life * 0.85,
              min_velocity=(0.0, 0.02, 0.0), max_velocity=(0.0, 0.04, 0.0),
              min_start_size=2.6, max_start_size=3.6,
              min_angular_velocity=-0.3, max_angular_velocity=0.3,
              render_mode=hpar.RENDER_HORIZONTAL,
              size_over_life=float_curve((0.0, 0.7), (1.0, 1.1)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(AMBER, 0.0)),
                  (0.25, rgba(AMBER, 0.12)),
                  (0.85, rgba(CANDLE_GOLD, 0.26)),
                  (1.00, rgba(CANDLE_GOLD, 0.0))))
    # The candles: flame tongues standing on the rim, flickering out of sync.
    candle_drag = 5.0
    hidden = 0.22
    candles = _rim_park(_e(
        name="Rim Candles", duration=0.05, bursts=[Burst(0.0, 22)],
        min_lifetime=life + 0.05, max_lifetime=life + 0.35,
        min_start_size=0.45, max_start_size=0.62,
        min_start_rotation=-0.12, max_start_rotation=0.12,
        material_name=FLAME,
        size_over_life=float_curve((0.0, 0.2), (hidden, 0.4), (0.3, 1.0), (0.38, 0.85),
                                   (0.47, 1.05), (0.55, 0.8), (0.64, 1.0), (0.72, 0.85),
                                   (0.82, 1.05), (0.9, 0.9), (1.0, 0.5)),
        color_over_lifetime=color_curve(
            (0.00, rgba(CANDLE_HOT, 0.0)),
            (hidden, rgba(CANDLE_HOT, 0.0)),
            (0.30, rgba(CANDLE_HOT, 0.95)),
            (0.60, rgba(CANDLE_GOLD, 0.9)),
            (0.92, rgba(AMBER, 0.85)),
            (1.00, rgba(AMBER, 0.0)))),
        CANDLE_RADIUS, candle_drag, lift=0.26)
    # Guttering tongues: short-lived flames popping up along the rim between the candles.
    tongues = _rim_park(_e(
        name="Guttering Tongues", duration=1.45, spawn_rate=36.0,
        min_lifetime=0.85, max_lifetime=1.05,
        min_start_size=0.30, max_start_size=0.50,
        min_start_rotation=-0.25, max_start_rotation=0.25,
        material_name=FLAME,
        size_over_life=float_curve((0.0, 0.3), (0.55, 0.4), (0.7, 1.0), (1.0, 0.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(CANDLE_HOT, 0.0)),
            (0.55, rgba(CANDLE_HOT, 0.0)),
            (0.68, rgba(CANDLE_GOLD, 0.85)),
            (1.00, rgba(AMBER, 0.0)))),
        CANDLE_RADIUS, candle_drag, lift=0.22)
    # Halo glow at the foot of each candle so the rim glows on the floor.
    halos = _rim_park(_e(
        name="Candle Halos", duration=0.05, bursts=[Burst(0.0, 16)],
        min_lifetime=life, max_lifetime=life + 0.2,
        min_start_size=0.9, max_start_size=1.3,
        min_angular_velocity=-0.5, max_angular_velocity=0.5,
        size_over_life=float_curve((0.0, 0.5), (hidden, 0.6), (0.4, 1.0), (1.0, 1.1)),
        color_over_lifetime=color_curve(
            (0.00, rgba(CANDLE_GOLD, 0.0)),
            (hidden, rgba(CANDLE_GOLD, 0.0)),
            (0.35, rgba(CANDLE_GOLD, 0.30)),
            (0.90, rgba(AMBER, 0.32)),
            (1.00, rgba(AMBER, 0.0)))),
        CANDLE_RADIUS, candle_drag, lift=0.12)
    # Wax and ember motes drifting up out of the marked area.
    motes = _e(name="Wax Embers", duration=1.8, spawn_rate=16.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, CANDLE_RADIUS),
               min_lifetime=0.8, max_lifetime=1.2,
               min_velocity=(-0.15, 0.6, -0.15), max_velocity=(0.15, 1.4, 0.15),
               min_start_size=0.05, max_start_size=0.09,
               gravity=(0.0, 0.3, 0.0), noise_amplitude=1.2, noise_frequency=1.5,
               render_mode=hpar.RENDER_STRETCHED, length_scale=2.5,
               material_name=BEAM,
               size_over_life=float_curve((0.0, 1.0), (1.0, 0.5)),
               color_over_lifetime=_fade(EMBER, 0.95, peak=0.12, hold=0.6, end=AMBER))
    # Thin smoke curling off the candles.
    smoke = _rim_park(_e(
        name="Candle Smoke", duration=1.6, spawn_rate=8.0,
        min_lifetime=1.2, max_lifetime=1.5,
        min_start_size=0.4, max_start_size=0.7,
        min_angular_velocity=-0.6, max_angular_velocity=0.6,
        gravity=(0.0, 1.4, 0.0),
        noise_amplitude=0.8, noise_frequency=1.0,
        size_over_life=float_curve((0.0, 0.4), (1.0, 1.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(INCENSE, 0.0)),
            (0.30, rgba(INCENSE, 0.0)),
            (0.50, rgba(INCENSE, 0.16)),
            (1.00, rgba(INCENSE, 0.0)))),
        CANDLE_RADIUS, candle_drag, lift=0.5)
    return ParticleSystem(emitters=[fill, rim, pulses, halos, smoke, candles, tongues, motes])


# =========================================================================================
# 6. Guttering Candle flare -- GROUND_EXPIRED detonation over the 3.5 radius
# =========================================================================================

def candle_flare():
    r = CANDLE_RADIUS
    pillar = _e(name="Flame Pillar", simulation_space=hpar.SIM_WORLD,
                duration=0.5, spawn_rate=120.0, bursts=[Burst(0.0, 30)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, r * 0.95),
                min_lifetime=0.55, max_lifetime=1.0,
                min_velocity=(-0.4, 2.5, -0.4), max_velocity=(0.4, 9.0, 0.4),
                min_start_size=1.0, max_start_size=1.7,
                min_angular_velocity=-2.0, max_angular_velocity=2.0,
                gravity=(0.0, 2.5, 0.0), drag=1.2,
                noise_amplitude=2.5, noise_frequency=0.8,
                size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(CANDLE_HOT, 0.0)),
                    (0.08, rgba(CANDLE_HOT, 0.42)),
                    (0.45, rgba(CANDLE_GOLD, 0.38)),
                    (0.80, rgba(AMBER, 0.22)),
                    (1.00, rgba(AMBER, 0.0))))
    tongues = _e(name="Flame Tongues", simulation_space=hpar.SIM_WORLD,
                 duration=0.45, spawn_rate=70.0, bursts=[Burst(0.0, 18)],
                 shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, r),
                 min_lifetime=0.35, max_lifetime=0.7,
                 min_velocity=(-0.2, 2.5, -0.2), max_velocity=(0.2, 7.5, 0.2),
                 min_start_size=0.7, max_start_size=1.3,
                 min_start_rotation=-0.2, max_start_rotation=0.2,
                 gravity=(0.0, 1.5, 0.0), drag=1.4,
                 material_name=FLAME,
                 size_over_life=float_curve((0.0, 0.4), (0.25, 1.0), (1.0, 0.6)),
                 color_over_lifetime=color_curve(
                     (0.00, rgba(CANDLE_HOT, 0.0)),
                     (0.10, rgba(CANDLE_HOT, 0.7)),
                     (0.50, rgba(CANDLE_GOLD, 0.55)),
                     (1.00, rgba(AMBER, 0.0))))
    core = _e(name="Pillar Core", simulation_space=hpar.SIM_WORLD,
              duration=0.4, spawn_rate=90.0,
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.2, 2.2),
              min_lifetime=0.35, max_lifetime=0.6,
              min_velocity=(0.0, 9.0, 0.0), max_velocity=(0.0, 16.0, 0.0),
              min_start_size=0.4, max_start_size=0.65,
              drag=1.8, orbital_speed=2.0,
              render_mode=hpar.RENDER_STRETCHED, length_scale=6.0,
              material_name=BEAM,
              size_over_life=float_curve((0.0, 0.7), (0.3, 1.0), (1.0, 0.6)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(CANDLE_HOT, 0.0)),
                  (0.10, rgba(CANDLE_HOT, 0.5)),
                  (0.60, rgba(CANDLE_GOLD, 0.35)),
                  (1.00, rgba(AMBER, 0.0))))
    ring_size = 2.0 * r / RING_BAND
    blast_ring = _e(name="Blast Ring", duration=0.05, bursts=[Burst(0.0, 1)], max_particles=2,
                    min_lifetime=0.55, max_lifetime=0.55,
                    min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.05, 0.0),
                    min_start_size=ring_size * 0.9, max_start_size=ring_size * 0.9,
                    render_mode=hpar.RENDER_HORIZONTAL, material_name=RING,
                    size_over_life=float_curve((0.0, 1.0), (0.35, 1.4), (1.0, 1.6)),
                    color_over_lifetime=color_curve(
                        (0.00, rgba(CANDLE_HOT, 0.0)),
                        (0.06, rgba(CANDLE_HOT, 0.8)),
                        (0.40, rgba(AMBER, 0.5)),
                        (1.00, rgba(AMBER, 0.0))))
    scorch = _e(name="Scorch", duration=0.05, bursts=[Burst(0.0, 1)], max_particles=2,
                min_lifetime=0.9, max_lifetime=0.9,
                min_velocity=(0.0, 0.04, 0.0), max_velocity=(0.0, 0.04, 0.0),
                min_start_size=r * 2.6, max_start_size=r * 2.6,
                render_mode=hpar.RENDER_HORIZONTAL,
                size_over_life=float_curve((0.0, 0.8), (0.2, 1.0), (1.0, 1.05)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(CANDLE_HOT, 0.65)),
                    (0.25, rgba(CANDLE_GOLD, 0.45)),
                    (1.00, rgba(AMBER, 0.0))))
    embers = _e(name="Flung Embers", simulation_space=hpar.SIM_WORLD,
                bursts=[Burst(0.0, 44), Burst(0.12, 20)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 0.0, r * 0.8),
                min_lifetime=0.7, max_lifetime=1.25,
                min_velocity=(-2.0, 5.0, -2.0), max_velocity=(2.0, 11.0, 2.0),
                min_start_speed=0.5, max_start_speed=2.5,
                min_start_size=0.07, max_start_size=0.13,
                gravity=(0.0, -8.0, 0.0), drag=0.9,
                render_mode=hpar.RENDER_STRETCHED, length_scale=4.5,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(CANDLE_HOT, 1.0)),
                    (0.35, rgba(EMBER, 1.0)),
                    (0.75, rgba(AMBER, 0.8)),
                    (1.00, rgba(AMBER, 0.0))))
    smoke = _e(name="Smoke Tail", simulation_space=hpar.SIM_WORLD,
               start_delay=0.3, duration=0.4, spawn_rate=40.0,
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 3.0, r * 0.8),
               min_lifetime=1.0, max_lifetime=1.4,
               min_velocity=(-0.3, 0.8, -0.3), max_velocity=(0.3, 1.8, 0.3),
               min_start_size=1.3, max_start_size=2.1,
               min_angular_velocity=-0.6, max_angular_velocity=0.6,
               gravity=(0.0, 0.4, 0.0), drag=0.6,
               noise_amplitude=1.2, noise_frequency=0.6,
               size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.6)),
               color_over_lifetime=_fade(INCENSE, 0.30, peak=0.3))
    return ParticleSystem(emitters=[
        scorch, blast_ring, smoke, pillar, core, tongues,
        _flash("Flare Flash", 5.0, CANDLE_HOT, alpha=0.7, lifetime=0.2, lift=1.4),
        embers,
    ])


# =========================================================================================
# 7. Candle burn -- IMPACT on a player hit by the flare
# =========================================================================================

def candle_burn():
    flames = _e(name="Burn Flames", simulation_space=hpar.SIM_LOCAL,
                duration=0.3, spawn_rate=50.0, bursts=[Burst(0.0, 6)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.0, 0.3),
                min_lifetime=0.3, max_lifetime=0.55,
                min_velocity=(-0.1, 0.5, -0.1), max_velocity=(0.1, 2.0, 0.1),
                min_start_size=0.35, max_start_size=0.6,
                min_start_rotation=-0.2, max_start_rotation=0.2,
                gravity=(0.0, 0.8, 0.0), drag=1.5, orbital_speed=1.5,
                material_name=FLAME,
                size_over_life=float_curve((0.0, 0.4), (0.3, 1.0), (1.0, 0.5)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(CANDLE_HOT, 0.0)),
                    (0.12, rgba(CANDLE_HOT, 0.75)),
                    (0.55, rgba(CANDLE_GOLD, 0.6)),
                    (1.00, rgba(AMBER, 0.0))))
    glow = _e(name="Burn Glow", simulation_space=hpar.SIM_LOCAL,
              duration=0.2, spawn_rate=50.0,
              shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.2, 0.3),
              min_lifetime=0.3, max_lifetime=0.5,
              min_velocity=(-0.2, 1.0, -0.2), max_velocity=(0.2, 2.0, 0.2),
              min_start_size=0.5, max_start_size=0.8,
              min_angular_velocity=-2.0, max_angular_velocity=2.0,
              gravity=(0.0, 0.8, 0.0), drag=1.5,
              size_over_life=float_curve((0.0, 0.5), (0.3, 1.0), (1.0, 1.3)),
              color_over_lifetime=color_curve(
                  (0.00, rgba(CANDLE_HOT, 0.0)),
                  (0.10, rgba(CANDLE_GOLD, 0.40)),
                  (0.60, rgba(AMBER, 0.28)),
                  (1.00, rgba(AMBER, 0.0))))
    embers = _e(name="Burn Embers", simulation_space=hpar.SIM_WORLD,
                bursts=[Burst(0.0, 16)],
                shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.2, 0.3),
                min_lifetime=0.45, max_lifetime=0.8,
                min_velocity=(-1.2, 1.5, -1.2), max_velocity=(1.2, 3.5, 1.2),
                min_start_size=0.05, max_start_size=0.08,
                gravity=(0.0, -4.0, 0.0), drag=1.2,
                render_mode=hpar.RENDER_STRETCHED, length_scale=3.5,
                material_name=BEAM,
                size_over_life=float_curve((0.0, 1.0), (1.0, 0.4)),
                color_over_lifetime=color_curve(
                    (0.00, rgba(CANDLE_HOT, 1.0)),
                    (0.40, rgba(EMBER, 1.0)),
                    (1.00, rgba(AMBER, 0.0))))
    smoke = _e(name="Burn Smoke", simulation_space=hpar.SIM_WORLD,
               start_delay=0.12, bursts=[Burst(0.0, 6)],
               shape=hpar.SHAPE_CONE, shape_extents=(0.0, 1.4, 0.25),
               min_lifetime=0.5, max_lifetime=0.68,
               min_velocity=(-0.2, 0.8, -0.2), max_velocity=(0.2, 1.4, 0.2),
               min_start_size=0.5, max_start_size=0.8,
               min_angular_velocity=-0.6, max_angular_velocity=0.6,
               size_over_life=float_curve((0.0, 0.5), (1.0, 1.5)),
               color_over_lifetime=_fade(INCENSE, 0.28, peak=0.3))
    scorch = _flash("Burn Scorch", 1.6, CANDLE_GOLD, alpha=0.55, lifetime=0.35)
    scorch.render_mode = hpar.RENDER_HORIZONTAL
    scorch.min_velocity = scorch.max_velocity = (0.0, 0.04, 0.0)
    return ParticleSystem(emitters=[
        scorch,
        _flash("Burn Flash", 1.3, CANDLE_HOT, alpha=0.7, lifetime=0.14, lift=1.0),
        glow, flames, smoke, embers,
    ])


EFFECTS = [
    (grave_strike_windup, "Oswin_GraveStrikeWindup.hpar"),
    (grave_strike_impact, "Oswin_GraveStrikeImpact.hpar"),
    (last_vigil_cast, "Oswin_LastVigilCast.hpar"),
    (last_vigil_release, "Oswin_LastVigilRelease.hpar"),
    (candle_ground, "Oswin_CandleGround.hpar"),
    (candle_flare, "Oswin_CandleFlare.hpar"),
    (candle_burn, "Oswin_CandleBurn.hpar"),
]


if __name__ == "__main__":
    only = set(sys.argv[1:])
    for build, filename in EFFECTS:
        if not only or filename in only or filename.split(".")[0] in only:
            write(build(), filename)
