# World-model prop kits — design

Date: 2026-10-16. Status: draft for review.

## Intent

The same building model (`.hwmo`) is placed many times: three copies of the small one-room house
in Oakenshire today, and the inn and town hall will be reused in other towns later. Furniture
and other props baked into the model make every copy look identical inside. A **prop kit** is a
separate asset that holds the interior (or exterior) dressing for a building. Each placed building
picks its kits, so copies of one model can look different.

## What the user decided

- **Kits are separate asset files.** They are not stored inside the `.hwmo`. One kit can be used
  in any building it fits, and new towns can add kits without touching the building model.
- **A placement can stack several kits**, as an ordered list. For example, "Inn - Base Furniture"
  plus "Harvest Festival Decor".
- **No kit means an empty interior.** The model does not name a default kit.
- **The small house's baked furniture moves into a kit.** That furniture is the table, two chairs,
  candle, wine casket, bed, bookcase, and the candle light. It is removed from
  `HouseTest.hwmo` and saved as a kit such as "Small House - Default". The three Oakenshire
  placements get that kit, so they look exactly as they do today until someone assigns a
  different kit.
- **Authoring happens in two places:**
  - **Kit editor.** Opening a kit asset shows it inside a preview building, where props and
    lights can be placed, moved and deleted.
  - **World editor.** Kits are assigned per placement in the details panel, as an ordered list
    with add, remove and reorder. A placed building can be furnished in place, then saved as a
    new kit or written back into one of its kits.

## Goals

- Copies of one building model can differ in props and lights.
- Kits render as efficiently as the model's own geometry. That means instanced batching and
  per-room portal culling, the same treatment doodads get today.
- Props in kits behave like the building's own geometry for player collision and for the
  server navmesh, so NPCs path around tables.
- Existing worlds keep loading without changes. Old placements simply have no kits.

## Non-goals

- The WMO-internal doodad sets (`MODS`/`MODN`/`MODD`, `SetActiveDoodadSet`, the doodads panel)
  are left alone. They are not removed and not migrated.
- Kits do not nest other kits or other world models. They have no animated, particle or sound
  emitters in this version.
- No randomized or procedural kit selection.

## Anti-goals

These would count as failure even if the feature works:
- Turning the WMO into a "prefab" format that bakes kits back in.
- Editing a kit silently changing placements that use a different kit.
- Copying furniture through placements by hand, so that kits drift apart.
- A furnished interior that blocks doorways or stairs because collision was ignored during
  authoring.

## Constraints

### Data

- **Kit content** is a list of props and a list of lights, all in the building's local space:
  - A prop: mesh path, name, position, rotation, scale, optional material override, visible flag.
    These are the same fields as `WorldModelMeshRef`.
  - A light: the same fields as `WorldModelLight`.
  - Editor-only metadata: the preview building path used by the kit editor, and a display name.
- **Kit asset format:** a new chunked binary asset, like the other engine assets. Use a versioned
  chunk layout with byte-reversed magics, the convention of the `.hwmo` and `.wobj` writers. The
  extension and magics are the builder's call. `.hkit` is suggested.
- **Placement file.** The placement file (`.wobj`) keeps version 3. The ordered list of kit asset
  paths for a world-model placement goes into a new optional chunk after the `WWMO` chunk.
  `WorldEntityLoader` already ignores chunks it doesn't know, so an older client still loads the
  building shell and simply shows no kits. A file without the chunk means "no kits". *(My call.
  Bumping to version 4 would make older readers drop the whole building, because the loader
  rejects versions above 3.)*
- **Readers of placements that must learn the kit chunk:**
  - `src/shared/game_common/world_entity_loader.*`, used by the client, `nav_build` and
    `server_collision_map`
  - the world editor's own entity reader and writer in `world_editor_instance.cpp`
  - the Python world tooling: `tools/world/worldkit/formats/wobj.py`, used by lint, dressing and
    renders. It requires exactly two chunks today, so it must accept and return the optional kit
    chunk. Its `wobj_bytes` writer may keep writing files without one.

### Rendering

- **Dependency on the Haven session.** A parallel session is working on `feature/haven-town-wmo`
  and makes child world-model refs (`MCWR`) real at runtime. It reworks `WorldModelInstance`
  into one "gather" step that yields mesh entries for batching and collision. Kits are added as
  another source to that step, so kit implementation starts after that branch is merged.

- Kit props join the world model instance's doodad-style path: batching, collision proxies, and
  culling by derived room (`WorldModelInstance::CreateDoodads` and the code around it). Kit
  lights are added to the instance's lights.
- Changing a placement's kits in the editor rebuilds only the kit content. The shell stays as it
  is, just as switching a doodad set does today.

### Collision and navigation

- **Client.** Kit props with a collision tree collide like the building's mesh refs.
- **Navmesh.** `nav_build` (`src/shared/nav_build/map.cpp`) includes the kit props of every
  world-model placement, so the server navmesh routes around furniture. The navmesh still has to
  be rebuilt and published separately, as today.
- **Server line of sight.** Kit props are ignored here, as WoW treats doodads.
  `server_collision_map` keeps reading only the building shell. *(My call: a candle or chair
  should not block spells. Easy to change if you disagree.)*

### Tooling

- The project rule: engine and content tooling lives in C++ `mmo_edit`, with an unattended
  `--flag` for agent use. Python is only for temporary scratch work.
- Provide an unattended job that applies a kit or creates one from a JSON description. This lets
  an agent furnish buildings without driving the GUI. The kit editor and world editor UI are the
  primary path for the user.
- Every new and changed reader and writer gets Catch2 tests, including round-trip and old-version
  tests.

### Shipping

- Anything merged reaches players the next morning through the nightly deploy, and old clients
  must not crash on new data.
- Kits and version-4 placements live in `data/client`. A client build that cannot read version 4
  must fail soft on such a placement: skip the kits, or skip the entity, and log it. It must not
  crash. *(My call; the builder should check what the current loader does with an unknown
  version.)*
- No network protocol change.

### Editing kits in place in the world editor (my calls)

- **Where props live.** Every prop belongs to exactly one kit. Placements store only their kit
  list. There are no per-placement overrides, because those would let kits drift apart (see the
  anti-goals).
- **Furnishing in place.** The details panel of a selected building has an **active kit**,
  picked from the building's kit list. New props go into the active kit. Props from the other
  kits stay visible and can be selected, and moving one edits the kit it came from.
- **Writing kits.** Edited kits are written when the world is saved. If an edited kit is also
  used by other placements, a confirmation names how many placements it affects.
- **Save as new kit.** This writes the active kit's current props and lights to a new file. The
  new kit replaces the active kit in this building's list. The original kit file is unchanged.

### Keeping doorways and stairs free (my calls)

- Kit editor and world editor can show the kit props' collision, using the existing collision
  debug display.
- The unattended kit job warns when a prop's collision box intersects any portal of the building
  model, or comes within 0.6 m of one. Portals mark the building's openings.
- Navmesh reachability is still checked as today, with `tools/world/dress.py check --nav`-style
  nav queries after a navmesh rebuild.

### Migration note

The small house's furniture is mesh refs in `HouseTest.hwmo`'s inner group. Today
`server_collision_map` reads those mesh refs, so the furniture blocks line of sight, and it also
feeds the navmesh. Moving the furniture into a kit removes it from line of sight, which is
intended. `nav_build` must read kits before that migration ships, so the next navmesh rebuild
does not lose it.

## Left to the builder

- Kit file extension, chunk layout and version numbering.
- Kit editor UI details: gizmo reuse, prop picker, light editing. Reuse the world model editor's
  mesh-ref and light panels where possible.
- Where the kit loading cache lives, for example a `PropKitManager` like `WorldModelManager`.
- How "save placement furnishing as kit" handles a name clash: ask, or overwrite.

## Follow-on work that depends on this

The Oakenshire dressing pass has these steps:
1. Fix the house and inn materials.
2. Move the old vendors.
3. Add outdoor props.

Steps 1-3 do not need kits and proceed now. Furnishing the inn, the town hall and the three
houses waits for kits, so each house can get a different interior: a farmhouse, the healer's
house, and a weaver's house.
