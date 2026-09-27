# Building blocks

Numbers here are taken from effects that shipped in this repo, at this game's scale
(player ≈ 1.8 units tall). Copy a block, then retune with the preview loop.

Each block is one emitter. Real effects layer 3-5 of them; see the "Composition" section
at the bottom.

## Column of light (the level-up / talent-reset shaft)

The core trick is `RENDER_STRETCHED` with a large `length_scale`: each particle draws as a
long streak along its own velocity, so a handful of upward-moving particles read as a solid
beam rather than as dots.

```python
Emitter(
    name="Column Glow",
    simulation_space=hpar.SIM_LOCAL,      # follows the character if they move
    loop=False, duration=1.35,
    spawn_rate=150.0, max_particles=340,
    shape=hpar.SHAPE_CONE,
    shape_extents=(0.05, 0.15, 0.80),     # (unused angle, height, base radius)
    min_lifetime=0.85, max_lifetime=1.30,
    min_velocity=(0.0, 1.1, 0.0), max_velocity=(0.0, 3.4, 0.0),
    min_start_size=0.26, max_start_size=0.40,
    gravity=(0.0, 0.0, 0.0),              # MUST be zeroed or the column falls back down
    orbital_speed=1.4,                    # slow twist; reads as swirling energy
    render_mode=hpar.RENDER_STRETCHED, length_scale=8.5,
    material_name="Particles/Particle_Beam.hmi",   # soft streak sprite, translucent
    size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 0.85)),
    color_over_lifetime=color_curve(
        (0.00, (1.0, 0.94, 0.50, 0.0)),
        (0.20, (1.0, 0.94, 0.50, 0.34)),
        (0.80, (1.0, 0.94, 0.50, 0.20)),
        (1.00, (1.0, 0.86, 0.34, 0.0))),
)
```

**Height** is `max_velocity.y * max_lifetime` ≈ 4.4 units here — about two and a half
characters. **Width** is `2 * base_radius`.

**Watch the green channel.** For a warm effect, `G` is what separates yellow from fire:
`(1.0, 0.94, 0.50)` reads as gold, `(1.0, 0.60, 0.12)` reads as flame. Because blending is
alpha, there is no white accumulation to pull a low-`G` ramp back toward neutral, so a
colour that looks like a reasonable "deep" tone in isolation turns the whole column orange.
Keep the darkest key above `G ≈ 0.85`, and reach it only in the last ~20% of the life.

**Keep the peak alpha low** (0.2-0.4 on a sheath like this). Blending is alpha, not
additive, so twenty layered particles at alpha 0.95 composite to a solid gold wall that
hides the character standing inside the column. Density carries the brightness.

**Widen the velocity spread** (`min` well below `max`, as above) or the column detaches from
the ground as it dies: with a narrow spread every particle rises at the same rate and the
whole shaft lifts off in one piece, leaving a gap at the feet.

Pair it with a narrower, faster, brighter, shorter-lived twin (`base_radius` ~0.22,
`length_scale` ~13, white-hot colour) as a core. The core reads as the hot centre; the body
gives the silhouette.

The original of this recipe is `ResetTalents.hpar` ("Holy Column"), in cold blue-white:
cone `(0.05, 0.2, 0.6)`, velocity `y 3..5`, lifetime `0.8..1.4`, `length_scale=11.9`,
`orbital_speed=1.5`, size `0.1`. Note that it still points at the broken
`Particles/Additive.hmat`, so in game it renders as opaque blocks — copy its motion, not its
material.

## Impact / onset burst

One burst at t=0, thrown outward by `start_speed` along the sphere's radial direction,
killed by gravity and drag. Its whole job is to give an effect a beginning.

```python
Emitter(
    name="Burst Sparks",
    simulation_space=hpar.SIM_WORLD,      # sparks stay where they were thrown
    loop=False, duration=0.15,
    spawn_rate=0.0, max_particles=70,
    bursts=[Burst(0.0, 55)],
    shape=hpar.SHAPE_SPHERE, shape_extents=(0.22, 0.0, 0.0),
    min_lifetime=0.40, max_lifetime=0.85,
    min_velocity=(-1.2, 1.5, -1.2), max_velocity=(1.2, 4.0, 1.2),  # biased upward
    min_start_speed=2.5, max_start_speed=6.5,                       # radial kick
    min_start_size=0.09, max_start_size=0.15,
    gravity=(0.0, -7.0, 0.0), drag=1.6,
    render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
    material_name="Particles/Particle_Beam.hmi",
    size_over_life=float_curve((0.0, 1.0), (1.0, 0.25)),
    color_over_lifetime=color_curve(
        (0.00, (1.0, 0.98, 0.86, 1.0)),
        (0.45, (1.0, 0.82, 0.30, 0.9)),
        (1.00, (1.0, 0.60, 0.12, 0.0))),
)
```

`duration` only needs to cover the last burst time. Shipped variants: `FrostImpact.hpar`
(60 particles, `start_speed 2..6`, `drag 1.0`, cold blue) and `FireBlast_Impact.hpar`.

For an omnidirectional burst leave `min/max_velocity` symmetric and let `start_speed` do
all the work; for a directional spray bias the velocity range.

## Ground flare

Flat expanding discs at the feet. This is what makes a vertical effect look like it is
*coming out of the ground* instead of hovering.

```python
Emitter(
    name="Ground Flare",
    simulation_space=hpar.SIM_LOCAL,
    loop=False, duration=0.95,
    spawn_rate=0.0, max_particles=12,
    bursts=[Burst(0.0, 2), Burst(0.30, 2), Burst(0.65, 2)],   # staggered pulses
    shape=hpar.SHAPE_POINT,
    min_lifetime=0.55, max_lifetime=0.75,
    min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.12, 0.0),  # barely drifts up
    min_start_size=1.5, max_start_size=2.0,
    min_start_rotation=0.0, max_start_rotation=3.14,
    min_angular_velocity=-0.9, max_angular_velocity=0.9,
    gravity=(0.0, 0.0, 0.0),
    render_mode=hpar.RENDER_HORIZONTAL,
    material_name="Particles/Particle_Glow.hmi",
    size_over_life=float_curve((0.0, 0.35), (0.35, 1.0), (1.0, 1.9)),  # expands as it fades
    color_over_lifetime=color_curve(
        (0.00, (1.0, 0.98, 0.86, 0.55)),
        (0.30, (1.0, 0.82, 0.30, 0.35)),
        (1.00, (1.0, 0.60, 0.12, 0.0))),
)
```

Give the disc a random start rotation and a slow spin, or repeated pulses visibly stamp the
same image. Lift it a hair off the ground (`min_velocity.y > 0`) so it does not z-fight the
terrain.

## Rising motes (the tail)

Slow textured sprites that outlive everything else, so the effect trails off instead of
cutting out. These are the one layer that wants a high peak alpha: they are discrete shapes
that should stay readable, not a volume to see through.

```python
Emitter(
    name="Rising Motes",
    simulation_space=hpar.SIM_LOCAL,
    loop=False, duration=1.60,
    spawn_rate=30.0, max_particles=70,
    shape=hpar.SHAPE_SPHERE, shape_extents=(0.75, 0.0, 0.0),
    min_lifetime=1.10, max_lifetime=1.90,
    min_velocity=(-0.15, 0.7, -0.15), max_velocity=(0.15, 1.5, 0.15),
    min_start_size=0.16, max_start_size=0.30,
    min_start_rotation=0.0, max_start_rotation=3.14,
    min_angular_velocity=-1.6, max_angular_velocity=1.6,   # twinkle
    gravity=(0.0, 0.35, 0.0),                              # gentle upward buoyancy
    drag=0.4, orbital_speed=0.7,
    render_mode=hpar.RENDER_BILLBOARD,
    material_name="Particles/Particle_Star.hmi",           # star sprite
    size_over_life=float_curve((0.0, 0.2), (0.25, 1.0), (1.0, 0.0)),
    color_over_lifetime=color_curve(
        (0.00, (1.0, 0.98, 0.86, 0.0)),
        (0.18, (1.0, 0.98, 0.86, 0.95)),
        (0.70, (1.0, 0.82, 0.30, 0.8)),
        (1.00, (1.0, 0.60, 0.12, 0.0))),
)
```

The looping ambient version of this is `Sparkles.hpar` (the lootable-corpse sparkle):
`loop=True`, sphere radius 1.2, rate 15, gravity `(0, 0.29, 0)`.

## Ambient loop (smoke, fog, fire)

```python
Emitter(
    loop=True, duration=1.0,
    warmup_time=2.0,                 # already running when it becomes visible
    spawn_rate=20.0, max_particles=100,
    shape=hpar.SHAPE_SPHERE, shape_extents=(0.4, 0.0, 0.0),
    min_lifetime=1.5, max_lifetime=3.0,
    min_velocity=(-0.2, 0.5, -0.2), max_velocity=(0.2, 1.2, 0.2),
    min_start_size=0.8, max_start_size=1.4,
    min_angular_velocity=-0.4, max_angular_velocity=0.4,
    gravity=(0.0, 0.1, 0.0),
    noise_amplitude=1.5, noise_frequency=0.6,   # drift, so it never looks like a fountain
    render_mode=hpar.RENDER_BILLBOARD,
    material_name="Particles/IceSmoke.hmat",
    size_over_life=float_curve((0.0, 0.4), (1.0, 1.6)),   # smoke expands as it dissipates
)
```

Keep `spawn_rate * max_lifetime` under `max_particles` (here 20 × 3 = 60 < 100) or the
plume visibly thins when it hits the cap.

## Projectile trail

`SIM_WORLD` is mandatory — that is what leaves particles behind along the flight path.

```python
Emitter(
    loop=True, duration=1.0,
    simulation_space=hpar.SIM_WORLD,
    spawn_rate=60.0, max_particles=120,
    shape=hpar.SHAPE_POINT,
    min_lifetime=0.25, max_lifetime=0.5,
    min_velocity=(-0.3, -0.3, -0.3), max_velocity=(0.3, 0.3, 0.3),
    min_start_size=0.12, max_start_size=0.2,
    gravity=(0.0, 0.0, 0.0), drag=2.0,
    render_mode=hpar.RENDER_BILLBOARD,
    material_name="Particles/Particle_Glow.hmi",
    size_over_life=float_curve((0.0, 1.0), (1.0, 0.0)),
)
```

Trails loop because the projectile manager destroys the whole emitter on impact. Do not
use `RENDER_STRETCHED` here: the particles are near-stationary, so the stretch direction
is noise.

## Composition

`LevelUp.hpar` is a worked example of the full stack —
`tools/particle_gen/recipes/level_up.py`:

| Emitter | Role | Life window |
|---|---|---|
| Burst Sparks | onset | 0 – 0.85 s |
| Ground Flare | contact | 0 – 1.7 s |
| Column Core | hot centre | 0 – 2.1 s |
| Column Glow | body | 0 – 2.65 s |
| Rising Motes | tail | 0 – 3.5 s |

Note how the windows are nested, shortest to longest: the effect resolves inward-out, so
there is never a frame where it visibly stops. Reversing that — a long-lived core and a
short-lived tail — is the single most common reason an effect "just ends".

Every emitter is alpha-blended, so draw order is visible. Emitters are sorted as whole
renderables by depth, not particle-by-particle across emitters, so keep the layers that must
interpenetrate cleanly (core and sheath) at similar extents and low alpha.
