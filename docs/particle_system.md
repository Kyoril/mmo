# Particle System

A high-performance, Unreal-Niagara-inspired particle system for spell VFX and environment effects. It
integrates with the scene graph and is authored visually in the **Particle System Editor** (`mmo_edit`).

> Looking for ready-made recipes (slashes, blood, holy, healing, casting auras, projectiles)? See
> the [Spell Effects Cookbook](spell_effects_cookbook.md).

## Concepts (Niagara mapping)

| Niagara | Here | Notes |
|---|---|---|
| **System** | `ParticleSystem` (`MovableObject`) | The scene object. Owns one or more emitters. One `.hpar` file = one System. |
| **Emitter** | `EmitterInstance` + `EmitterParameters` | Each emitter has its own particle pool, material and renderer. |
| **Module stacks** | grouped fields: *Emitter / Emission / Spawn / Update / Render* | Fixed, toggleable feature blocks presented as a stack in the editor. |
| **Curves** | `ColorCurve`, `FloatCurve` | Color- and size-over-life, edited with curve widgets. |
| **Renderers** | `ParticleRenderMode` | Billboard / Velocity-Aligned / Stretched / Horizontal. |

A `Particle` is a compact POD (80 bytes). Systems are simulated once per frame with a single shared
deltaTime computed by the `Scene` (no per-emitter timing drift).

## The module stack

### Emitter
- **Simulation Space** — **World** (particles stay where spawned: trails, smoke) or **Local**
  (particles follow the emitter: auras on a moving hand). This is the key control for "does the effect
  follow the caster?".
- **Loop** / **Duration** — loop forever, or run a single cycle (one-shot). Non-looping systems report
  `IsFinished()` and are auto-destroyed by the spell visualization service.
- **Start Delay**, **Warmup** (pre-simulate so the effect looks "already running" on spawn).
- **Inherit Velocity** — fraction of the emitter's own motion added to new particles.

### Emission
- **Spawn Rate** — continuous particles/second.
- **Max Particles** — hard cap.
- **Bursts** — list of `(time, count)` instantaneous spawns within a cycle (impacts, slashes, level-ups).

### Spawn (initial particle attributes)
- **Shape** — Point / Sphere / Box / Cone (+ shape extents). Defines spawn position and the outward
  direction used by **Start Speed**.
- **Lifetime**, **Start Size**, **Start Rotation**, **Rotation Rate** — all min/max ranges.
- **Velocity** (min/max vector) and **Start Speed** (along the shape's outward direction).

### Update (over-life behaviour & forces)
- **Gravity**, **Drag** (linear damping).
- **Orbital / Swirl** — angular speed around the emitter's Y axis (spinning magic).
- **Radial Acceleration** — push out (+) / pull in (−) from the emitter center.
- **Point Attractor** — position + strength.
- **Noise** — cheap curl-like turbulence (amplitude, frequency) for organic motion.
- **Size Over Life** — `FloatCurve` multiplier on the spawned base size.
- **Color Over Life** — `ColorCurve` (multi-key RGBA). Multiplies the texture.

### Render
- **Render Mode**:
  - **Billboard (Face Camera)** — default.
  - **Velocity Aligned** — quad's long axis follows velocity (still camera-facing).
  - **Stretched** — velocity-aligned and elongated by **Length Scale** (slashes, spark streaks).
  - **Horizontal Billboard** — flat on the ground plane (shockwaves, decals).
- **Sprite Sheet** — Columns/Rows + **Sprite Animation** (None / Animate Over Life / Random Static)
  for flipbook textures (blood, explosions). `Sprite FPS = 0` fits one full sheet across the lifetime.
- **Material** — `.hmat` used to render this emitter (drag-drop from the Asset Browser, or the
  Additive / Alpha / Soft Additive quick buttons).

## Using the editor

Open a `.hpar` from the Asset Browser. Layout:

- **Viewport** (left) — live preview with a toolbar: Play/Pause, Restart, Loop, **Speed** slider,
  background preset, **Wireframe**, **Move Emitter** (animates the emitter so you can verify Local-space
  effects follow it), a **scrub** bar (fast-forwards a fresh simulation to a time), and live stats
  (emitter count, particle count, frame ms). Camera: LMB rotate, RMB pan, wheel/MMB zoom.
- **Emitters** (top-right) — add (empty or **From Template**), duplicate, remove, rename, enable/disable,
  and select emitters.
- **Properties** (bottom-right) — the module stack for the selected emitter, including the **Color Over
  Life** and **Size Over Life** curve editors (double-click to add a key, drag to move, right-click to
  delete).

Edits apply live; `Ctrl+S` saves.

## Runtime usage

```cpp
// One system can contain several emitters.
ParticleSystem* system = scene.CreateParticleEmitter("MyEffect"); // returns a ParticleSystem

ParticleSystemParameters params;
StreamSource source(/* .hpar file */);
io::Reader reader(source);
ParticleSystemSerializer serializer;
if (serializer.Deserialize(params, reader))
{
    system->SetSystemParameters(params); // materials load automatically from each emitter
}

scene.GetRootSceneNode().CreateChildSceneNode()->AttachObject(*system);
system->Play();
```

For a quick single-emitter setup in code you can still use the convenience API
(`system->SetParameters(EmitterParameters)`, `SetMaterial`, `Play/Stop/Reset`), which operates on the
first emitter. `ParticleEmitter`/`ParticleEmitterParameters` remain as aliases of `ParticleSystem`/
`EmitterParameters` for source compatibility.

Spells reference systems through `SpellKit.particles` (a list of `.hpar` paths); the
`spell_visualization_service` spawns them, attaches to `attach_bone` if set, and destroys non-looping
systems when finished.

## File format (`.hpar`)

Chunk-based, versioned:

- **v2.0** (current): `VERS` → `SYS` (emitter count) → one `EMIT` chunk per emitter (flat fields +
  inline size/color curves).
- **v1.0** (legacy single emitter): auto-detected on load and imported as a one-emitter system; the
  old start/end size maps to a base size + a 2-key size curve. Saving always writes v2.0.

Old `.hpar` files continue to load unchanged.

## Performance notes

- Keep `Max Particles` low; prefer bursts to high continuous rates for impacts.
- Additive blending needs no sorting; alpha blending is back-to-front sorted per frame.
- Shorter lifetimes and one-shot (non-looping) systems reduce live particle counts.
- Stretched/Velocity-Aligned quads are larger — mind overdraw.
- Orbital/Noise are cheap CPU forces but scale with particle count.

## Source map

| File | Responsibility |
|---|---|
| `src/shared/scene_graph/particle_emitter.h/.cpp` | `Particle`, enums, `EmitterParameters`, `EmitterInstance`, `ParticleRenderable`, `ParticleSystem` |
| `src/shared/scene_graph/particle_emitter_serializer.h/.cpp` | `ParticleSystemSerializer` (v2.0 + legacy v1.0), `ParticleEmitterSerializer` (single-emitter compat) |
| `src/shared/graphics/float_curve.h/.cpp` | scalar curve (size/alpha over life) |
| `src/shared/graphics/color_curve.h/.cpp` | RGBA curve (color over life) |
| `src/mmo_edit/editors/particle_system_editor/` | the editor (stack UI, preview, templates) |
| `src/mmo_edit/editors/color_curve_editor/` | `ColorCurveImGuiEditor`, `FloatCurveImGuiEditor` widgets |
