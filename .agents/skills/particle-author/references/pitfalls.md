# Review checklist and traps

## Read the contact sheet against this list

Every one of these has actually gone wrong in this repo.

1. **Scale.** Render with `--figure`. Is the effect the size you meant *relative to a
   character*, not relative to the tile? An effect tuned without the silhouette is almost
   always 2-3x too big or too small.
2. **Does it read as the shape you intended?** A cone emitter with a fast narrow core and
   nothing else reads as a *flame*, not a *column*. Widening the base and slowing the rise
   is what turns one into the other. Judge the silhouette, not the parameters.
3. **Is the base still lit at the end?** If the effect visibly lifts off the ground while
   dying, the vertical velocity spread is too narrow — lower `min_velocity.y` so some
   particles linger near the emitter.
4. **Does it fade or cut?** Check the last two tiles. If particles are still bright in the
   second-to-last tile and gone in the last, some emitter's colour curve does not end at
   `a=0`, or a short-lived emitter is the longest-lived thing in the system.
5. **Does it end at all?** `loop=False` on every emitter of a one-shot. Confirm by running
   the preview with an explicit `--duration` past the expected end and checking the tile is
   empty.
6. **Density.** Uniform brightness across the whole volume means too many particles or too
   little size variance; visible individual dots means too few. Vary `min/max_start_size`
   by at least 1.5x.
7. **Colour and opacity.** Blending is alpha, so what you see is what ships — there is no
   bloom to rescue it. If the effect reads as a solid wall and hides the `--figure`
   silhouette behind it, the peak alphas are too high; volumetric layers want 0.2-0.4.
8. **Blend modes.** Read the per-emitter blend report `preview.py` prints under the output
   path. Any emitter reported as `opaque` is using a mis-typed material and will render as
   hard rectangles.

## Traps

**A material typed `Unlit` renders OPAQUE.** `MaterialCompiler` only produces
`MaterialType::Translucent` when the graph's Material node has `Translucent = true`; an
unlit-but-not-translucent material falls through to `Unlit`, and `Material::Apply` then
picks `BlendMode::Opaque`. Particles come out as hard, mutually-occluding rectangles with no
soft edge. This is the state `Particles/Additive.hmat` is in today. **Always check the
material before blaming the emitter** — `preview.py` prints each emitter's resolved blend
mode and warns about this, and
`material_tool.py inspect <mat>` shows the type directly.

**There is no additive blending in this engine, and no bloom.** Only `BlendMode::Opaque`
and `BlendMode::Alpha` exist. Effects cannot glow by accumulation; keep volumetric layers at
alpha 0.2-0.4 and let density do the work. A design copied from an additive reference
(most WoW spell effects) will not reproduce directly.

**An untextured particle material draws a hard quad.** Softness comes entirely from the
sprite's alpha channel. And a DXT1 sprite has no usable alpha, so it renders as an opaque
black square — particle sprites must be DXT5.

**`depth_write` on a translucent particle material** makes particles cull each other and
anything transparent behind them. The `Particles/Particle_*.hmi` instances turn it off;
their parent `Particle_Alpha_Tex.hmat` does not.

**`gravity` defaults to real gravity.** `EmitterParameters` starts at `(0, -9.81, 0)`. Any
emitter meant to rise, hover, or hold a shape must set it explicitly. This is the single
most common "why does my effect immediately collapse" cause.

**`duration` is emission time.** The effect lives for
`start_delay + duration + max_lifetime`. Emitters whose emission windows all end at the
same time still finish at wildly different moments if their lifetimes differ.

**`max_particles` silently truncates.** When the cap is hit, both continuous spawning and
bursts stop — no warning, the effect just thins out partway through. A burst of 55 into a
cap of 40 spawns 40. Sanity check: `spawn_rate * max_lifetime + sum(burst counts) <= max_particles`.

**`length_scale` is clamped to a minimum of 1** and does nothing outside
`RENDER_STRETCHED`. Setting it on a billboard emitter has no effect at all.

**The cone's `angle` extent is dead.** `shape_extents.x` is serialized but the simulation
never reads it — only `height` (`.y`) and `baseRadius` (`.z`) matter.

**`SIM_LOCAL` particles follow the owner.** For an effect on a moving character this is
usually what you want for a column or aura, and wrong for a trail or debris. Note that
local-space particles are also *rotated* by the owner's yaw — fine for radially symmetric
effects, visibly wrong for anything with a facing.

**Curve tangents on disk are ignored** for keys with `tangent_mode == 0`. The engine
recomputes them from neighbouring keys at load. Shape curves with key times and values.

**Auto tangents overshoot.** A three-key curve like `(0, 0.35) (0.35, 1.0) (1.0, 1.9)` is
smooth, but sharp value changes over short time spans can push the Hermite segment past
both endpoints. If a size curve looks like it pops, add an intermediate key rather than
fighting the tangents.

**Sorting is per-emitter, not global.** Particles are sorted back-to-front within an
emitter, but emitters are drawn as whole renderables ordered by their own depth. Two
emitters that interpenetrate can therefore pop as the camera moves. Keep interpenetrating
layers in one emitter where it matters, and keep depth-write off so the artifact stays a
blend-order issue rather than a hard cull.

**Re-saving an old file grows it by 2 bytes.** Files written before `mesh_name` existed
(e.g. `FrostImpact.hpar`, 400 bytes) come back as 402 after a round-trip. That is the empty
trailing string, not corruption.

**Legacy v1.0 files do not round-trip byte-identically.** `FierySmoke.hpar` and
`IceSmoke.hpar` are v1.0; `hpar.py` reads them and writes v2.0. That is an upgrade, and the
engine reads both — but do not use a byte comparison to validate a change to those two.

**The preview is not the renderer.** It reads each material's real ATTR chunk, so opaque
versus alpha is honest, but it stamps a generic soft blob instead of the actual sprite, has
no scene, no depth sorting against geometry, and a faked stretch projection (top-down views
of stretched emitters look elongated as an artifact). Use it for timing, extent, density,
opacity and shape. Confirm the sprite itself in `mmo_edit` or the client.

## Verifying in the editor

`mmo_edit` has a particle system editor that opens `.hpar` files directly, with an emitter
stack, pause / sim-speed / scrub controls, and a live particle count. If the user has the
editor open, an already-open particle file will not hot-reload — they need to reopen it.

Presets live in `Editor/ParticlePresets/` and are ordinary `.hpar` files, so anything
authored here can be dropped in as a preset.
