# Door obstacles in the nav mesh + per-map shared static map data

Date: 2026-10-13
Branch: `feature/nav-door-obstacles`

## Problem

Closed doors and magic barriers (Door-type world objects) block line of sight on the server,
but not pathfinding. The nav mesh is baked from static geometry only and knows nothing about
doors, and `UnitMover` follows a path without any collision check. In the Hollow Choir dungeon,
a player can drag a trash mob to the G2 barrier in front of the final boss. The mob paths
through the barrier and ends up next to the boss, the boss assists it, and the player can fight
and kill the boss without ever clearing the west wing or Sister Mereth.

On top of that, every `WorldInstance` loads its own copy of the nav mesh, the static collision
geometry and the water map. Dozens of dungeon instances of the same map mean dozens of
identical copies of read-only data in memory.

## Goals

- While a door is closed, no server-side nav query (path, random point, nav raycast) crosses
  it. Where another route exists, pathfinding finds it. Where none exists, the result is a
  partial path that ends in front of the door, and the existing unreachable/evade handling
  takes over.
- No nav mesh rebuild, copy or mutation at runtime. Per-instance door state is a few bytes.
- Nav mesh, static collision geometry and water map are loaded once per map and shared by all
  instances of that map. Only dynamic LOS blockers (doors) stay per instance.
- The design applies to every Door-type object on every map, not just Hollow Choir.

## Non-goals

- Server-side validation of player movement against door collision. Player walk blocking stays
  on the client, as it is today.
- Doors spawned at runtime without a map spawn entry (dev command `CreateTemporaryObject`).
  They get LOS blocking as before but no nav blocking, and a warning is logged.
- Changing the nav file format version. Old `.nav` files keep loading, they just contain no
  door polys.
- `bot_nav_service`, `nav_query` and the editor keep their own `nav::Map` instances.

## Design

### 1. Baking door footprints (nav_builder)

- `nav_build` gets read access to the editor project data (`maps.data`, `objects.data`,
  `object_displays.data` under the output dir's `data/`). For the world being built, it finds
  the `MapEntry` whose `directory()` matches the world name and collects every object spawn
  whose object entry has type `Door`.
- For each door spawn it loads the display mesh's COLL tree through the asset registry and
  builds the same local→world transform the server uses in `WorldInstance::RegisterDoorCollision`:
  spawn position, uniform scale, and spawn rotation × 90° yaw around Y. The transform is factored
  into one shared helper (`MakeDoorTransform`) that both the builder and the server call, so the
  two cannot drift apart.
- The transformed collision vertices are projected onto XZ. Their minimal oriented box (aligned
  with the door's yaw) is inflated by the walkable agent radius on every side, because barriers
  are single planes with zero thickness. The Y range is the mesh's world Y range ± walkableClimb.
- In `MeshBuilder`, after `rcErodeWalkableArea` and before `rcBuildDistanceField`, each door box
  that overlaps the tile is applied with `rcMarkConvexPolyArea(..., poly_flags::Door)`, where
  `poly_flags::Door = 32` is a new bit in `nav_build/common.h`. Recast then cuts a separate thin
  poly strip at the doorway. The existing area→flags copy turns this into a poly flag, so every
  door poly carries `Door` alongside its other flags (normally `Ground`).
- A door with no COLL tree, or with an unknown object/display entry, is skipped with a warning.
- Maps with doors have their nav rebuilt, at least `Test` (Hollow Choir). The new `.nav` files
  are committed to the `data/editor` submodule.

### 2. Per-instance query filter

- New class `nav::InstanceQueryFilter : public dtQueryFilter` in `src/shared/nav_mesh/`. Detour
  is compiled with `DT_VIRTUAL_QUERYFILTER` so that `passFilter` can be overridden. The define
  must be set PUBLIC on the Detour target so every consumer sees the same class layout.
- `passFilter(ref, tile, poly)` runs the base flag check first. Only when the poly has the
  `Door` flag does it binary-search a sorted `std::vector<dtPolyRef>` of blocked refs. An
  instance without closed doors therefore pays one extra bit test per expanded poly.
- Blocked refs are reference-counted, because two doors could share a poly. API:
  `Block(span<const dtPolyRef>)` / `Unblock(span<const dtPolyRef>)`.
- `nav::Map::FindPath`, `FindRandomPointAroundCircle` and `LineOfSight(Ex)` get an optional
  `const dtQueryFilter* filter = nullptr` parameter. `nullptr` means the default filter, so
  existing callers (bots, tools) are unchanged.
- The same-poly shortcut in `FindPath` keeps working, because `findNearestPoly` now uses the
  instance filter and never returns a blocked poly as start or end.

### 3. Door registration on the server

- `WorldInstance::RegisterDoorCollision` keeps registering the dynamic LOS collision. In
  addition it:
  1. calls `EnsureNavLoaded` for the door's inflated world AABB,
  2. calls `queryPolygons` over that AABB with a filter that only accepts `Door`-flag polys, then
     keeps the polys whose center lies inside the door's oriented box (the same geometry the
     builder used, from `MakeDoorTransform`),
  3. stores these refs with the door registration, and blocks them in the instance filter if
     the door is closed.
- The existing `stateChanged` slot toggles the nav block together with the LOS collision
  (closed → `Block`, open → `Unblock`). `UnregisterDoorCollision` unblocks before it removes the
  registration. Despawn and respawn go through the same unregister/register pair.
- If a door finds no door polys (map not rebuilt, or a runtime-spawned door), the server logs
  one `WLOG` per door entry and map, and the door blocks LOS only.
- Poly refs stay valid because shared tiles are never removed while any instance of the map
  exists (see section 4: no page unloading while a map is in use).

### 4. Sharing static map data

- New `MapStaticData` (in `game_server/world/`). It is created once per map directory and
  immutable from the instances' point of view. It holds:
  - `std::shared_ptr<nav::Map>` plus its `nav::PageStreamer` (streaming state is per map),
  - `StaticCollisionMap`, the static part of today's `ServerCollisionMap` (the instance vector
    plus the load code),
  - `ServerWaterMap`.
- New `MapStaticDataRegistry`, owned by `WorldInstanceManager`, keyed by map directory. It
  stores `std::weak_ptr<MapStaticData>` and hands out `std::shared_ptr`. Creating an instance
  of a map that is already loaded reuses the data; destroying the last instance frees it. A
  dungeon that is reset (destroyed and recreated) reuses the data only while another instance
  of the same map is alive; when it was the last one, the data is freed and the new instance
  loads it again.
- `ServerCollisionMap` becomes the per-instance LOS view: a `shared_ptr<const StaticCollisionMap>`
  plus the existing dynamic-instance map, handles and mesh-tree cache. `LineOfSight(Ex)` tests
  static first, then dynamic, exactly as today. Its public API stays the same, so call sites do
  not change.
- `NavMapData` (per instance) holds `shared_ptr<MapStaticData>`, its `ServerCollisionMap`
  and its `nav::InstanceQueryFilter`. Every nav call passes that filter.
- Page installation (the 4 ms slices) moves from `NavMapData::Update` to
  `MapStaticDataRegistry::Update`, which `WorldInstanceManager` calls once per tick for every
  live map. `EnsureNavLoaded` and `CalculatePath`'s retry path call into the shared streamer.
- `SimpleMapData` (maps without nav) is unchanged.

### Threading

The world server has a single io thread. One shared `dtNavMesh`, one shared `dtNavMeshQuery`
(its node pool) and the per-map streamer are only touched from that thread; the streamer's
reader thread still only reads files. This adds one more reason why the world tier cannot go
multi-threaded without work (a query per thread, or a lock). `docs/threading.md` and the
"Server Threading" section of `CLAUDE.md` are updated to say so.

### Wire / DB impact

No protocol change and no DB migration. Data change: rebuilt `.nav` files in `data/editor`.

## Testing

- **`nav_mesh_tests`:** a synthetic tile built with the existing `BuildFlatTile` helper, with
  a door poly between two halves.
  - Path is blocked when the door ref is blocked, and a partial path ends in front of it.
  - Path is free again after `Unblock`.
  - With a second corridor, the path goes around.
  - Refcounting: two blocks plus one unblock means still blocked.
  - `FindRandomPointAroundCircle` never returns a point on a blocked poly.
- **`nav_build_tests`:** rasterizing a flat plane with one door box yields at least one
  `Door`-flag poly inside the box and none outside the inflated box. `MakeDoorTransform`
  matches the server's transform for a rotated spawn.
- **`game_server_tests`:**
  - Two instances of the same map share one `MapStaticData`, and destroying both frees it.
  - LOS: a static blocker is seen by both instances, but a door registered in instance A does
    not block LOS in instance B.
  - A closed door blocks the instance filter, and opening it unblocks.
- **E2E (`e2e/scenarios/`):** in Hollow Choir, pull a trash mob to the G2 barrier while
  Mereth is alive. The mob must never reach the far side, and the final boss must stay out of
  combat. After Mereth dies, the mob can cross.
- **Manual:** the original repro from the bug report.
- **Gate:** the fast gate, including the Linux build. gcc must see the
  `DT_VIRTUAL_QUERYFILTER` define consistently.
- **Shutdown:** `python tools/shutdown_check.py`, since the registry owns streamer threads.

## Risks

- **Door strip too thin, or removed by region filtering:** Recast's small-region filter counts
  connected regions across areas, so a strip joined to the floor survives. The test in
  `nav_build_tests` covers this.
- **Builder/server transform drift:** prevented by the shared `MakeDoorTransform` helper and
  its test.
- **Memory peak while streaming:** unchanged per map, now paid once per map instead of once per
  instance.
