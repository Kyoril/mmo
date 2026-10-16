# World-model prop kits

A prop kit is a separate asset (`.hkit`) holding the furniture and lights of a building interior.
A world-model placement (`.wobj`) lists the kits it uses, so copies of one `.hwmo` can look
different inside. A placement without kits has an empty interior; the model does not name a default.

## Format

`.hkit` is a chunked binary asset (`src/shared/scene_graph/prop_kit_serializer.{h,cpp}`), version 1.

| Chunk | Content |
|---|---|
| `KVER` | uint32 version (1) |
| `KNAM` | display name |
| `KPRV` | editor-only path of the preview world model the kit editor shows it in |
| `KPRP` | props: mesh path, name, material override, position, rotation (w,x,y,z), scale, visible |
| `KLIT` | lights, 60 byte entries, same layout as `MOLT` 2.1 in `world_model_serializer.cpp` |

Everything is in the building's local space. Kits do not nest and have no animated, particle or
sound content. `PropKitManager` caches loaded kits by path.

Placements store the ordered kit list in an optional `WKIT` chunk of the `.wobj` (uint8 count, then
uint16-length paths, at most 255; `src/shared/game_common/world_entity_loader.{h,cpp}`). The file
version stays 3. No chunk means no kits; old readers ignore it. The Python reader is
`tools/world/worldkit/formats/wobj.py`.

## Runtime

- **Client:** `world_deserializer.cpp` loads the kits of each placement and passes them to
  `WorldModelInstance::SetPropKits`. Kit props are batched and culled like the model's own mesh
  refs, and props with a collision tree collide like them.
- **Navmesh:** `nav_build` (`map.cpp`, `prop_kit_collision.cpp`) bakes the collision triangles of
  every visible kit prop that has a collision tree into the navmesh, per placement, so furniture
  blocks paths. Props without collision are skipped. Missing kit or mesh files are logged and skipped.
- **Server line of sight:** kit props are ignored, as doodads are in WoW.

Rebuild the navmesh after changing a kit that is placed in a world.

## Authoring

**World editor** (details panel of a selected world model): edit the placement's ordered kit list.
The `Active kit` combo chooses where meshes dropped into the viewport go while the building is
selected; clicking a prop of the building selects and moves it in the kit it came from. Saving
writes kits edited in place; a kit that other loaded buildings also use asks for confirmation first.
`Save as new kit...` writes the active kit's props and lights to a new file and uses it for this
building only.

**Kit editor** (`src/mmo_edit/editors/prop_kit_editor/`): open an `.hkit` to edit its props and
lights inside its preview building.

**Unattended job:** `mmo_edit --apply-prop-kit <job.json>` applies a job and exits (exit code 1 on
load or write failures, log in `prop_kit_job.log`). mmo_edit must not already be running. Committed
examples are in `data/world/kits/*.json`. The whole job is parsed and validated before any asset is
written. Top-level keys, all optional:

```json
{
  "kit": "Kits/Name.hkit",
  "name": "Display name",
  "preview": "Models/.../Building.hwmo",
  "props": [
    {"mesh": "Models/.../Chair.hmsh", "name": "Chair_01", "material": "",
     "position": [x, y, z], "yaw": 90, "rotation": [w, x, y, z],
     "scale": 1.0, "visible": true}
  ],
  "lights": [
    {"type": "omni", "color": "ffce8115", "position": [x, y, z], "intensity": 1.4, "range": 8.0,
     "rotation": [w, x, y, z], "innerCone": 0, "outerCone": 0, "fogScattering": 0}
  ],
  "assign": [
    {"world": "Development", "uniqueId": "4555430321098860313", "kits": ["Kits/Name.hkit"]}
  ],
  "removeMeshRefs": {
    "worldModel": "Models/.../Building.hwmo",
    "names": ["SM_hc_DinnerTable_B"],
    "removeLights": true
  }
}
```

- `kit` (required when writing a kit) is the output path. Giving it creates or overwrites the kit
  from `name`, `preview`, `props` and `lights`.
- Props: `mesh` is required. `rotation` (quaternion w,x,y,z) wins over `yaw` (degrees around Y).
  `scale` is a number or an array of 3. Defaults: identity rotation, scale 1, visible.
- Lights: `type` is `omni` (default), `spot`, `direct` or `ambient`; `color` is up to 8 hex digits
  (default `ffffffff`); `range` is the attenuation end.
- If `preview` is set, props whose collision bounds come within 0.6 m of a portal of that model are
  logged as warnings (doorway clearance). Warnings do not fail the job.
- `assign` replaces the kit list of the placement `uniqueId` (a decimal string, 64 bit ids do not
  survive JSON numbers) in the world's entity folders, `Worlds/<world>/<world>/Entities/`. A missing
  kit file only warns; a missing placement fails.
- `removeMeshRefs` strips mesh refs by name from every group of the world model, optionally
  clears all its lights (`removeLights`), recalculates the bounds and saves the `.hwmo`. Use it to
  move baked furniture out of a model and into a kit.
