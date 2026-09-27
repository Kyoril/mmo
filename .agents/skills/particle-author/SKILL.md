---
name: particle-author
description: Author, tune, and inspect .hpar particle effects for this MMO's client — spell impacts, auras, level-up and other one-shot bursts, ambient loops, projectile trails. Use when creating a new visual effect, retuning an existing one, diagnosing a particle system that looks wrong or never disappears, or wiring an effect up so it actually plays in game.
---

# Particle Author

Particle effects live in `data/client/Particles/*.hpar`. A `.hpar` file is a **particle
system**: an ordered list of one or more **emitters**, each with its own shape, spawn rate,
forces, over-life curves and material. Layering emitters is how effects get their look —
almost nothing convincing is a single emitter.

The loop is: **write a Python recipe → build the .hpar → render a contact sheet → look at
it with the Read tool → retune → repeat**, then hand it to the engine and verify in the
real client or the editor's particle editor.

Never hand-write `.hpar` bytes. `tools/particle_gen/hpar.py` mirrors
`src/shared/scene_graph/particle_emitter_serializer.cpp` and round-trips every existing
v2.0 file byte-for-byte; use it.

## Workflow

1. **Look at what already exists first.** `python tools/particle_gen/inspect_hpar.py
   data/client/Particles/ResetTalents.hpar` prints an effect in readable form. If an
   existing effect is close to what you want, `--as-recipe` prints an editable Python
   script that rebuilds it exactly — start there instead of from defaults.
2. **Write a recipe** in `tools/particle_gen/recipes/<effect>.py`. Start from
   [references/recipes.md](references/recipes.md), which has building blocks (column of
   light, impact burst, ground flare, rising motes, trail, smoke) with real numbers taken
   from shipped effects. Keep the recipe checked in next to the `.hpar` it produces — the
   binary is the artifact, the recipe is the source you retune next time.
3. **Build and preview:**
   ```
   python tools/particle_gen/recipes/<effect>.py
   python tools/particle_gen/preview.py data/client/Particles/<Effect>.hpar --figure
   ```
   Then **Read the resulting `_preview.png`**. This is not optional — particle parameters
   are wildly unintuitive, and every effect in this repo needed 2-4 rounds of looking.
4. **Judge the sheet against the checklist** in
   [references/pitfalls.md](references/pitfalls.md): scale versus the `--figure`
   silhouette, whether it reads as the shape you intended, whether the base stays anchored,
   whether it fades out or cuts out, and whether it ends at all. Then run
   `inspect_hpar.py <file> --check [--one-shot]` for the mistakes a preview cannot show —
   chunk-size corruption, particle budgets blown past `max_particles`, colour curves that
   do not fade to zero, looping emitters in a one-shot effect.
5. **Wire it up.** An effect that nothing plays is dead data. See
   [references/wiring.md](references/wiring.md) for the four ways an effect reaches the
   screen (spell visualization kits, one-shot on a unit, projectile trail/impact, and
   direct engine code) and which one to pick.
6. **Verify in the real renderer.** The preview reads each material's real blend mode and
   prints it per emitter, but it stamps a generic soft blob rather than the actual sprite
   and has no scene or geometry. Confirm in `mmo_edit`'s particle system editor (opens
   `.hpar` directly, with pause / sim-speed / scrubbing), or in the client if you added a
   trigger.

## Command reference

```
# inspect an existing effect
python tools/particle_gen/inspect_hpar.py <file.hpar>
python tools/particle_gen/inspect_hpar.py <file.hpar> --as-recipe > tools/particle_gen/recipes/x.py

# validate (exit 1 on problems); --one-shot also rejects looping emitters
python tools/particle_gen/inspect_hpar.py <file.hpar> --check --one-shot

# build from a recipe (writes data/client/Particles/<Effect>.hpar by default)
python tools/particle_gen/recipes/<effect>.py [output.hpar]

# preview
python tools/particle_gen/preview.py <file.hpar>
    --figure                 draw a 1.8-unit character silhouette for scale
    --camera side|front|top|threequarter
    --view-height 6.0        world units covered by the tile height
    --frames 12 --columns 6  contact-sheet layout
    --duration 3.0           seconds simulated (default: auto)
    --exposure 1.0           plain output gain; the engine has no bloom, so 1.0 is the truth
    --out sheet.png
```

## The shape of an effect

Everything is in **world units and seconds**. A player capsule is ~1.8 units tall and
~0.5 wide, so scale intuition is: a `0.2` particle is a fist, a `1.5` ground disc covers
the character's footprint plus a margin, a column reaching `y = 5` is roughly three
characters tall.

A system is a list of emitters that all start together and are all driven by the same
scene node. There is no per-emitter transform — only `start_delay` staggers them in time.
Layer them by role:

| Role | Typically |
|---|---|
| **Core** | narrow, fast, bright, short-lived, heavily stretched — the hot centre |
| **Body** | wider, slower, coloured, longer-lived — the silhouette from a distance |
| **Onset** | a single `Burst` at t=0 — gives the effect a start instead of a fade-in |
| **Ground contact** | `HorizontalBillboard` discs — anchors the effect to the world |
| **Tail** | slow textured motes that outlive everything else — stops it cutting out |

Full field reference, units, and the binary layout:
[references/format.md](references/format.md).

## Rules that are not negotiable

- **One-shot effects must have `loop=False` on every emitter.** The auto-destroy path
  (`ParticleSystem::IsFinished`) returns false forever if any emitter loops, and the effect
  leaks a scene node and a particle system for the lifetime of whatever owns it.
- **`duration` is emission time, not effect time.** The effect lives for
  `start_delay + duration + max_lifetime`. A 1.35 s emitter with 1.3 s particles is a
  2.65 s effect. Budget the whole thing.
- **Check the material, not just the emitter.** `Particles/Additive.hmat` is typed `Unlit`,
  which makes the engine render it with **opaque** blending — hard, occluding rectangles.
  Use the `Particles/Particle_Beam.hmi` / `Particle_Glow.hmi` / `Particle_Ring.hmi` /
  `Particle_Star.hmi` instances instead; they are translucent, textured with soft DXT5
  sprites, and have depth-write off. Full table in
  [references/format.md](references/format.md).
- **There is no additive blending and no bloom in this engine** — only opaque and standard
  alpha. Keep volumetric layers at peak alpha 0.2-0.4 and let density carry the brightness;
  a stack of high-alpha particles composites to a solid wall, not to a glow.
- **Colour alpha does the fading, not the size curve.** Every emitter's
  `color_over_lifetime` must start and end at `a=0` unless you specifically want particles
  to pop in or clip out.
- **Keep `max_particles` honest.** It is a hard cap: if `spawn_rate * max_lifetime` exceeds
  it the emitter silently stops spawning and the effect thins out mid-life. Either raise
  the cap or lower the rate.
