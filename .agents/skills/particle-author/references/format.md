# The `.hpar` format and every emitter field

Authoritative source: `src/shared/scene_graph/particle_emitter.h` (fields),
`particle_emitter.cpp` (simulation), `particle_emitter_serializer.cpp` (layout).
`tools/particle_gen/hpar.py` mirrors all three.

## Binary layout (v2.0)

Flat chunk sequence, little endian. Chunk framing is `<4-byte magic><uint32 payload size>`.
The magics on disk are the **reverse** of the four-character codes in the C++ source
(`MakeChunkMagic('EMTR')` writes `RTME`).

```
VERS  uint32 version           0x0200
PSYS  uint32 emitterCount      informational only; emitters come from the RTME chunks
RTME  <emitter blob>           one per emitter, in order
```

Strings are `uint16` length-prefixed. Curves are written inline (count, then keys) — not as
their own chunks, because the chunked curve readers consume to end-of-stream.

**v1.0 legacy files** (`FierySmoke.hpar`, `IceSmoke.hpar`) use `PARM` + `COLR` chunks with
`uint8`-prefixed strings and a single emitter. `hpar.py` reads them and always writes v2.0.

**Trailing-field rule.** `mesh_name` was appended after v2.0 shipped. The reader only reads
it when bytes remain before the chunk end, so older files load fine and re-saving them just
grows the file by the 2 empty-string bytes. `FrostImpact.hpar` is such a file: it round-trips
to 402 bytes instead of 400. That is expected, not corruption. If you add another field,
append it the same way and gate the read on `reader.pos < chunk_end`.

## Emitter fields

All distances are world units, all times seconds, all angles radians.

### Emitter module

| Field | Meaning |
|---|---|
| `name` | display name in the editor's emitter stack |
| `enabled` | disabled emitters simulate nothing and clear their particles |
| `simulation_space` | `SIM_WORLD`: particles are left behind when the owner moves (trails, smoke, debris). `SIM_LOCAL`: particles are stored emitter-relative and follow the owner (auras, columns on a moving character). |
| `loop` | `True` = restarts its cycle forever. **`False` is required for anything that must self-terminate.** |
| `duration` | length of one emission cycle. For a one-shot this is how long it *spawns*, not how long it lives. |
| `start_delay` | delay before this emitter starts. The only way to stagger emitters within a system. |
| `warmup_time` | pre-simulated seconds on first update, so a loop looks "already running". Simulated at a fixed 30 Hz, capped at 600 steps. Leave at 0 for one-shots. |
| `inherit_velocity` | fraction of the owner's own velocity added to new particles. World space only. |

### Emission module

| Field | Meaning |
|---|---|
| `spawn_rate` | continuous spawns per second, accumulated fractionally |
| `max_particles` | hard cap on live particles. When hit, both continuous spawns and bursts silently stop. |
| `bursts` | `[Burst(time, count)]` — instantaneous spawns at `time` within the cycle. On a looping emitter they re-arm every cycle. |

### Shape module

`shape_extents` is overloaded per shape:

| Shape | `shape_extents` | Spawns |
|---|---|---|
| `SHAPE_POINT` | ignored | all at the origin; outward direction is +Y |
| `SHAPE_SPHERE` | `(radius, _, _)` | uniformly inside the sphere; outward direction is radial |
| `SHAPE_BOX` | `(x, y, z)` full extents | uniformly inside the box; outward direction is +Y |
| `SHAPE_CONE` | `(angle, height, baseRadius)` | on a lerp `t ∈ [0,1)`: height `height*t`, radius `baseRadius*t`. **`angle` is stored but unused by the simulation.** Outward direction is radial-from-origin. |

The cone is the workhorse for columns and plumes: it fills a cone-shaped volume with
density concentrated near the tip (the origin), which reads as a shaft that widens upward.

### Spawn module

`min_*` / `max_*` pairs are all uniform random per particle.

| Field | Meaning |
|---|---|
| `min/max_lifetime` | seconds. Clamped to a minimum of 0.0001. |
| `min/max_velocity` | per-axis initial velocity, componentwise random |
| `min/max_start_speed` | extra speed **along the shape's outward direction**, added to the velocity above. This is what makes a sphere burst outward. |
| `min/max_start_size` | quad size in world units (the whole quad, not a half-extent) |
| `min/max_start_rotation` | initial roll of the sprite |
| `min/max_angular_velocity` | roll speed. No effect in stretched/velocity-aligned modes. |

### Update / forces module

Applied in this order every step: gravity → radial → attractor → noise → drag →
integrate → orbital → rotation.

| Field | Meaning |
|---|---|
| `gravity` | constant acceleration. **Set to `(0,0,0)` for anything rising** — the default is real gravity and will pull your column back down. |
| `drag` | `velocity *= (1 - drag*dt)`, clamped at 0. ~1.5 kills a spark burst in about a second. |
| `orbital_speed` | rad/s swirl around the emitter's Y axis, applied to *position* after integration. 0.6-1.5 gives a pleasant twist; above ~3 it reads as a blender. |
| `radial_acceleration` | outward (+) / inward (-) from the emitter centre |
| `attractor_position` / `attractor_strength` | pull toward a point (emitter-local in Local space, emitter-relative in World space) |
| `noise_amplitude` / `noise_frequency` | curl-noise turbulence. Amplitude is an acceleration; 1-4 for drifting smoke, higher for chaos. |

### Over-life curves

`size_over_life` is a **multiplier** on the randomized `base_size`; `color_over_lifetime` is
absolute RGBA. Both are evaluated on normalized age `t = age / lifetime`.

Both are **Hermite** curves, and keys with `tangent_mode == 0` (the default) get their
tangents **recomputed on load** from neighbouring keys. So the tangent values written to
disk are ignored for auto keys — you shape the curve entirely with key times and values.
Values before the first key and after the last are clamped, and the interpolator snaps to
the endpoint values within 0.1% of a segment.

Build them with `float_curve((t, v), ...)` and `color_curve((t, (r,g,b,a)), ...)`.

### Sprite sheet

`sprite_sheet_columns` × `sprite_sheet_rows` split the material's texture into frames.
`SPRITE_ANIMATE_OVER_LIFE` plays the sheet across the particle's life (`sprite_animation_fps
= 0` means exactly one full sheet per lifetime); `SPRITE_RANDOM_STATIC` picks one frame at
spawn. Only meaningful on a material whose texture actually is a sheet.

### Render module

| Mode | Quad orientation |
|---|---|
| `RENDER_BILLBOARD` | faces the camera; `rotation` rolls it |
| `RENDER_VELOCITY_ALIGNED` | faces the camera, up axis along velocity, square |
| `RENDER_STRETCHED` | as above, but the length half-extent is multiplied by `max(1, length_scale)` — **this is how you draw beams, streaks and sparks** |
| `RENDER_HORIZONTAL` | lies flat on the XZ plane; `rotation` spins it in-plane. Ground discs, shockwaves. |
| `RENDER_MESH` | renders `mesh_name` once per particle via GPU instancing, tinted by the colour curve |

`length_scale` only does anything in `RENDER_STRETCHED`, and values below 1 are clamped to
1. A stretched particle of size `s` with `length_scale = L` draws `s` wide by `s*L` long, so
a `0.3` particle at `L = 8.5` is a 2.5-unit streak.

`material_name` is an asset path. `mesh_name` is only read in `RENDER_MESH`.

## Blending: there is no additive mode

The engine has exactly two blend states (`graphics_device_d3d11.cpp`): **opaque**
(`ONE`/`ZERO`) and **standard alpha** (`SRC_ALPHA`/`INV_SRC_ALPHA`). `BlendMode` has no
additive entry, and there is no bloom or HDR pass. Everything a particle does visually has
to come out of alpha blending and the sprite's own alpha channel.

Consequences you must design around:

- **Overlapping particles do not accumulate toward white.** Each particle carries its own
  brightness; stacking twenty of them just composites toward opaque.
- **Peak alpha has to stay low on volumetric layers** (~0.2-0.4 for a column sheath), or the
  effect becomes a solid wall that hides whatever is inside it. Density builds the look.
- **Softness lives in the texture's alpha channel.** An untextured particle material draws a
  hard-edged quad, full stop.
- **DXT1 sprites are unusable** for particles: no alpha, so the black background renders as
  an opaque black square. Particle sprites must be DXT5.

`material_name` accepts a `.hmat` **or a `.hmi`** — `MaterialManager::Load` dispatches on the
extension. Prefer an `.hmi` instance: it can swap the texture and turn depth-write off
without any shader recompilation.

## Available particle materials

| Path | Type | Sprite | Notes |
|---|---|---|---|
| `Particles/Particle_Beam.hmi` | Translucent, depth-write off | `T_Particle_Beam.htex` | vertical soft streak — use for `RENDER_STRETCHED` |
| `Particles/Particle_Glow.hmi` | Translucent, depth-write off | `T_Particle_Glow.htex` | soft radial blob — the workhorse |
| `Particles/Particle_Ring.hmi` | Translucent, depth-write off | `T_Particle_Ring.htex` | soft annulus — expanding shockwaves |
| `Particles/Particle_Star.hmi` | Translucent, depth-write off | `star.htex` | four-point star |
| `Particles/Particle_Alpha_Tex.hmat` | Translucent, **depth-write on** | `star.htex` | the parent of all four instances |
| `Particles/IceSmoke.hmat` | Translucent | `simpleSmoke3.htex` | smoke puff |
| `Particles/Additive.hmat` | **Unlit — BROKEN** | none | see below |
| `Particles/Alpha.hmat` | Unlit — same defect | none | |

**`Particles/Additive.hmat` does not work and its name is a lie.** Its graph has
`Translucent = false`, so `MaterialCompiler` types it `Unlit`, so `Material::Apply` selects
`BlendMode::Opaque` — and it has depth-write on and no texture. Particles using it render as
hard, opaque, mutually-occluding rectangles. `material_compiler.cpp` even carries a comment
warning about exactly this mapping. `ResetTalents.hpar`, `FrostImpact.hpar` and
`FireBlast_Impact.hpar` all still reference it and all still look like blocks.

Fixing it means setting `Translucent = true` and `Depth Write = false` on its Material node
and giving it a texture — an HMAT graph edit that must be re-saved in `mmo_edit` to
recompile the shaders. Until someone does that, **do not use it**.

Making new material instances (no editor needed):

```python
# export any .hmi, change name/parent/attributes/texture_parameters, apply to a new path
python .claude/skills/mmo-material-editor/scripts/material_tool.py apply-json x.json     --output data/client/Particles/Particle_Foo.hmi --overwrite
```

Making new sprites: `python tools/particle_gen/make_sprites.py` builds the soft glow, beam
and ring sprites procedurally and imports them as DXT5 `.htex` via `texconv`.

Legacy textures in `data/client/Particles/`: `star.htex` (DXT5, usable),
`fireshockwave.htex` (DXT5, fiery ring), `simpleSmoke3.htex` (DXT5, smoke),
`flare.htex` and `basic_droplet.htex` (**DXT1 — no alpha, unusable as particle sprites**).

## Lifetime and destruction

`EmitterInstance::IsFinished()` is `!loop && age >= start_delay + duration && particles.empty()`.
`ParticleSystem::IsFinished()` requires that of *every* emitter. Callers that auto-destroy
one-shot effects (`GameUnitC::UpdateOneShotEffects`) poll it — so a single looping emitter
in an otherwise one-shot system means the effect never goes away.
