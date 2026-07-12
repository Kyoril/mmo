# Footstep Surface Sounds — Design

**Date:** 2026-07-12
**Status:** Approved

## Goal

Replace the hardcoded footstep WAV list in `GamePlayerC::OnFootstep` with data-driven
sound entries, selected by the **surface type** of the floor the player is standing on.
Surface types are a new, reusable engine concept resolved from materials / material
instances, including per-splat-layer resolution on terrain.

## Current state

- `GamePlayerC::OnFootstep` (src/shared/game_client/game_player_c.cpp) hardcodes six
  `Sound/Character/Footsteps/ground_*.WAV` paths, picks one at random and applies a
  random pitch (0.7–1.3) and fixed volume — duplicating by hand what `SoundEntryPlayer`
  already provides (shuffle-bag file picking, pitch range, base volume, 3D attenuation,
  category volume).
- Footstep notifies fire from animation states (`AnimationNotifyType::Footstep`),
  registered in `GamePlayerC::RegisterFootstepHandlers`, gated on animation weight
  ≥ 0.35. Only players (`GamePlayerC`) register footstep handlers; creatures do not.
- Collision hits (`CollisionResult`, `CollisionHitResult`) carry no identity of what was
  hit — no movable, no triangle, no material.
- Entity collision tests run against the mesh's serialized `AABBTree` (`COLL` chunk).
  `AABBTree::Build` permutes face order for spatial partitioning and discards the
  original order, so a hit face index cannot currently be mapped back to a submesh.
- Terrain tiles each render with one `MaterialInstance` of a splatting material; the
  four layers are blended inside that material via the coverage ("Splatting") texture.
  Per-position layer dominance is already queryable from page layer data
  (`GetLayerValueAt`).

## Decisions (made during brainstorming)

1. **Surface type lives on the material asset** (HMAT), overridable per material
   instance (HMI). Terrain splatting materials additionally carry four per-layer
   surface types because one splatting material renders several visual surfaces.
2. **The set of surface types is data-driven proto data** (`surface_types.proto`), not
   a C++ enum.
3. **Scope: players only** (as today). Moving footstep handling to `GameUnitC` for
   creatures is a follow-up.
4. **Per-triangle submesh resolution now**: the `AABBTree` is extended with a
   per-face submesh-index array (serialized, version-guarded). Old meshes fall back to
   the entity's first material until their collision is rebuilt in the mesh editor.
5. **Fallback**: if no surface type or no footstep sound resolves, keep playing the
   legacy hardcoded WAV list so footsteps never go silent while content is authored.
   The legacy path is deleted once content exists.

## 1. Surface types (proto data)

New `src/shared/proto_data/surface_types.proto`, mirrored byte-compatibly in
`src/shared/client_data/surface_types.proto` (same field numbers — ClientDB invariant):

```proto
message SurfaceType
{
    required uint32 id = 1;
    required string name = 2;            // "Grass", "Stone", "Wood", ...
    optional uint32 footstep_sound = 3;  // SoundEntry id; 0 = none
    // future consumers hang more refs here: impact decals, particles, ...
}

message SurfaceTypes
{
    repeated SurfaceType entry = 1;
}
```

Registered in both `proto_data/project.h` and `client_data/project.h`; exported with
the rest of the ClientDB data.

**Editor:** a new surface type editor window (pattern: `sound_editor_window`) with
CRUD list, name field, and a footstep sound entry picker (reuse the existing
`sound_entry_combo`).

## 2. Surface type on materials

- `MaterialInterface` gains:
  - `uint32 GetSurfaceTypeId() const` — base surface type (0 = unset).
  - `uint32 GetLayerSurfaceTypeId(uint8 layer) const` — per-splat-layer surface type
    for terrain splatting materials; a value of 0 falls back to the base surface type.
- `Material` (HMAT) serializes the base id + 4 layer ids. Version-guarded addition to
  material serialization; old files load unchanged with all ids = 0.
- `MaterialInstance` (HMI) can override base and layer ids (usual override-flag
  pattern); otherwise inherits from the parent material.
- **Material editor UI:** surface type dropdown (base) and a collapsible "Layer Surface
  Types" section with four dropdowns, fed from the proto project's surface type list.
  Material instance editor gets the standard override checkboxes.

## 3. Floor type query (reusable engine concept)

### Collision identity plumbing

- `CollisionResult` (scene_graph/movable_object.h) gains an optional hit face index
  (`int32 faceIndex = -1`). `Entity::TestRayCollision` fills it (its traversal already
  walks tree faces).
- `AABBTree` gains `std::vector<uint16> m_faceSubMeshes` — one submesh index per face:
  - Filled by `Build()` via a new optional input (per-input-face submesh ids), permuted
    together with the face reorder.
  - Serialized with the tree, version-guarded: old `COLL` payloads deserialize with an
    empty array (fallback behavior below).
  - The mesh editor's "Build Complex" gathers faces submesh-by-submesh and already
    knows the submesh of every face; it passes the ids. Excluded submeshes are simply
    not present.
- `IntersectRay`'s existing `faceIndex` output is the index into the reordered faces —
  exactly what `m_faceSubMeshes` is indexed by.

### Surface type resolution

New virtual on `ICollidable`:

```cpp
/// Resolves the surface type id at a collision hit on this object (0 = unknown).
virtual uint32 GetSurfaceTypeAt(const CollisionResult& hit) const { return 0; }
```

- **`Entity`**: hit face index → submesh index via the tree's `m_faceSubMeshes`
  (fallback: submesh 0 when array empty or index invalid) → that submesh's material →
  `GetSurfaceTypeId()`.
- **Terrain `Tile`**: ignore the face; sample the dominant splat layer at
  `hit.contactPoint` via the page layer data (`GetLayerValueAt`), then return the tile
  material's `GetLayerSurfaceTypeId(dominantLayer)` (0 → base surface type).
- Other collidables (e.g. instanced foliage) keep the default 0 for now.

### Query entry point

A small helper (game_client) — `ResolveFloorSurfaceType(Scene&, const Vector3&
position)`: short downward ray from the unit's position (capsule bottom + small up
epsilon, down ~1.5 m), scene AABB query over collidable movables (same pattern as
`UnitMovement::SweepMultiCast`), closest blocking hit wins, returns
`hit collidable → GetSurfaceTypeAt(...)`. Returns 0 when nothing is hit.

## 4. Footstep playback

`GamePlayerC::OnFootstep` becomes:

1. Resolve surface type id under the player via the floor query.
2. Look up the surface type entry in the client data project; take its
   `footstep_sound` sound entry id.
3. If a sound entry id resolved: `SoundEntryPlayer::PlayEntry(id, GetPosition())`.
   Variation, pitch range, volume, 3D attenuation and category all come from the
   sound entry.
4. Otherwise: legacy fallback — the existing hardcoded `ground_*.WAV` random playback,
   unchanged, so footsteps keep working before content is authored.

Wiring: `GamePlayerC` receives the `SoundEntryPlayer` and client data project access
the same way the audio interface is currently injected (world state owns the
`SoundEntryPlayer`). Existing gates stay: animation weight ≥ 0.35, players only,
no footsteps while swimming (animation-driven, unchanged).

## 5. Content workflow

1. Create surface types (Grass, Dirt, Stone, Wood, ...) in the new editor window and
   assign footstep sound entries.
2. Set base surface types on floor-relevant materials; set the four layer surface
   types on terrain splatting materials.
3. Multi-material floor meshes: rebuild collision in the mesh editor (gains the
   per-face submesh array on save).

## Error handling

- Every resolution step treats "missing" as 0/unknown and falls through to the legacy
  fallback — no asserts on unauthored content.
- Out-of-range face/submesh indices (stale collision data) clamp to fallback
  (first material), never crash.
- Sound entry ids referencing missing entries behave like today's `SoundEntryPlayer`
  (no-op with a debug log).

## Testing

- **Unit-testable pieces**: `AABBTree` build/serialize round-trip with face-submesh
  ids (permutation correctness: face hit by ray reports the right submesh before and
  after save/load); dominant-layer selection logic if factored as a free function.
- **Manual/editor verification**: assign distinct footstep sounds to two terrain
  layers and one mesh material; walk across a splat boundary and onto the mesh floor;
  verify the sound switches. Verify an old (not rebuilt) mesh falls back to first
  material, and unauthored materials fall back to legacy WAVs.

## Explicit non-goals (this round)

- Creature footsteps (`GameUnitC`) — easy follow-up.
- Surface-type consumers beyond footstep sounds (decals, particles).
- Per-triangle resolution for instanced foliage collision.
- Water/swimming sounds.
