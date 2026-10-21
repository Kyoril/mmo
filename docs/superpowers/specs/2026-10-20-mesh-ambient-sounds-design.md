# Mesh ambient sounds — design

Date: 2026-10-20 · Branch: `feature/ambient-sound-emitters`

## Goal

Meshes can carry looping positional sounds: a torch crackles, a campfire burns, the dungeon portal
hums. Authored once on the mesh asset, every placement of that mesh plays it — a plain world
placement, a world model's group mesh, a doodad, a prop-kit prop, a nested child world model.

## Decisions

- **Mesh-level** (user decision). No per-placement override in v1.
- **Client-only, cosmetic.** No server involvement, no packets, no protocol bump.
- Sounds reference the existing `SoundEntry` catalog (category, 3D, loop, volume, pitch range,
  min/max distance, fades). Emitters only add *where* and *how loud relative to the entry*.

## Data: `SNDS` mesh chunk

`MeshSoundEmitter { uint32 soundEntryId; Vector3 offset; float volume = 1; float maxDistance = 0; }`
held by `Mesh` (`GetSoundEmitters()` / `SetSoundEmitters()`), so every existing save path
(mesh editor, collision builder, stairs-to-ramp, set-submesh-material) keeps it.

Chunk payload: `uint8 version (1)`, `uint16 count`, then per emitter `uint32 id, float3 offset,
float volume, float maxDistance`. `maxDistance` 0 = use the entry's `max_distance`.
Written only when the mesh has emitters and only at version ≥ 0.3.2.

Compatibility: from mesh version 0.3.2 readers ignore unknown chunks, and the writer always writes
0.3.2, so older clients, servers and nav_build simply skip the chunk. No mesh version bump.

Offsets are mesh-local and follow the placement's rotation and scale; distances do not scale.

## Runtime

**`AmbientSoundEmitters`** (`src/shared/game_client/ambient_sound_emitters.{h,cpp}`), owned by
`WorldState`, updated each frame with the listener (player) position.

- `Add(owner, emitters)` / `Remove(owner)` / `Clear()`. `owner` = (kind, placement unique id) — mesh
  placements and world models are kept apart because their id spaces are separate maps.
- Re-selects every 0.25 s: candidates are emitters within their audible range (override or entry
  `max_distance`). Nearest first, at most **12 voices total and 4 per sound entry** — a hall of 40
  torches plays the four nearest, which is indistinguishable and keeps FMOD's 64 channels free.
  Playing voices keep their slot until 10 % beyond range (hysteresis, no edge flicker).
- Voices start through `SoundEntryPlayer::PlayEntry(id, position)` at a **random point in the
  loop** (new `IChannelInstance::SetPlaybackPosition`) so identical torches never phase-lock, and
  fade in/out over the entry's fades with a 0.3 s floor (swaps due to the cap happen within
  earshot). Volume = entry volume × emitter volume × fade.
- Non-looped or 2D entries are rejected with one warning per entry id (this is a loop system).
- Selection is a pure function (`SelectAmbientVoices`) covered by Catch2 tests; the manager is
  tested against `FakeAudio`.

**Registration** (`ClientWorldInstance`, given the manager by `WorldState`):
- `CreateMapEntity`: mesh emitters transformed by the placement node.
- `CreateWorldModelEntity`: `WorldModelInstance::CollectSoundEmitters(out)` walks the group mesh
  refs, the active doodad sets, the kit props and the child instances recursively, in world space.
  Pure data walk — works the same with batching/merging on, where no per-mesh entity exists.
- Page unload removes the page's owners; `UnloadAllEntities` clears.

Not covered in v1: creature/game-object display meshes, instanced foliage, editor viewport playback.

## Authoring

- **Mesh editor**: "Ambient Sounds" section — per emitter a `SoundEntry` combo, offset, volume,
  max-distance override, preview, remove; "Add". Offsets are shown in the viewport as markers.
- **Unattended**: `mmo_edit --set-mesh-sounds <mesh> <spec>` where spec is
  `id:x,y,z[:volume[:maxDistance]]` entries separated by `;` (empty spec clears). Writes the mesh
  with its collision recipe preserved.

## Content (first pass)

Generated loops (sfx_gen, `loop: true`), `SoundEntry` with category Ambience, `is_3d`, `looped`:
- Torch crackle → `Models/Dungeon/Props/Torch_01.hmsh` (Hollow Choir monastery), max ~10 m.
- Campfire → `SM_hc_CampFire_A.hmsh`, braziers `SM_hc_Brazier_A/B`, max ~18 m.
- Portal hum → `Models/Dungeon/DungeonPortal_01.hmsh`, max ~20 m.

The user auditions the takes before they are wired.
