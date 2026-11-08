# World map

The world map (`M`) shows the player's whole map as one continuous canvas. You can zoom it from
the full continent down to single buildings. Zone and sub zone names appear as their areas
grow on screen. When you zoom in closely, the painted-style map art crossfades into the
minimap tiles. The design follows the WoW addon OpenMap.

## Controls

| Input | Action |
|---|---|
| Mouse wheel | Zoom towards the cursor (eased) |
| Left drag | Pan |
| Left click | Glide into the zone under the cursor, then its sub zone, then closer |
| Right click | Glide out to the zone, then to the whole map |
| `+` / `-` buttons | Zoom about the centre |
| Find Me / Whole Map | Centre on the player / fit the whole map |
| `Escape`, `M` | Close |

`Shift-M` now toggles the minimap; it used to be `M`.

## Data: baked per map

`mmo_edit` bakes `data/client/Textures/WorldMap/<map directory>/`:

- `WorldMap.wmap`: the page rectangle and page presence, plus the zone id of every terrain tile
  (33 m cells, run-length encoded). It also lists each zone's bounds and label position.
  The format is in `src/shared/game/world_map.h` (`world_map::WorldMapInfo`).
- `L<level>_<x>_<y>.htex`: the map art pyramid. Level `L` tiles are 256 px DXT1 and cover
  2^L x 2^L pages, for levels 1 (about 4 m per pixel) to 6 (the whole 64 x 64 page grid).
  The tiles are built from the minimap tiles' mip levels. They get a printed-map grade
  (softer colour, parchment tint, paper grain) and ink lines along zone borders. Sub zone
  borders are fainter.

Bake it with `mmo_edit --bake-worldmap Worlds/<n>/<n>.hwld`. Every minimap bake also re-bakes
it when it finishes (the editor's Generate Minimaps button and `--bake-minimaps`). So bake the
minimaps first, or the art has holes where minimap tiles are missing. Pages that have terrain
but no minimap tile are drawn as open sea.

Zones come from the pages' `.tile` files. The bake reads them with `terrain_io`, which rejects
legacy v1 pages. Re-save such pages in the editor, or their tiles have no zone on the map.

A map without a `WorldMap.wmap` shows "There is no map of this place."

## Client

- `WorldMap` (`src/mmo_client/ui/world_map.h`) draws into its own render texture. It draws the
  coarsest art level as a base under everything, then the art level that matches the zoom, and
  over both the minimap tiles with `world_map::MinimapBlend` opacity. Texture loads are
  synchronous, so it loads at most four tiles per frame, nearest to the view centre first, and
  each tile fades in. Hovering a zone tints it, using a mask built from the zone grid. Labels
  are drawn into the texture with the `WorldMapTitleFont`, `WorldMapZoneFont` and
  `WorldMapSubZoneFont` font maps (`WorldMap.xml`). The most important labels are placed first,
  and a label that would overlap one already placed is skipped.
- `WorldMapFrame` is the `WorldMap` frame type: it forwards mouse input and shows the texture.
- UI: `data/client/Interface/GameUI/WorldMap.xml` / `.lua`. Lua API: `WorldMapHasMap`,
  `WorldMapGetMapName`, `WorldMapGetHoverInfo`, `WorldMapGetPlayerInfo`, `WorldMapCenterOnPlayer`,
  `WorldMapShowWholeMap`, `WorldMapZoomIn` and `WorldMapZoomOut`.
- Coordinates in the footer are percent of the root zone's bounding box, as players quote them.

## Party and guild members

Both maps show party members (blue) and online guild mates (green).

- **Party:** the world node reports each grouped *or guilded* character's stats and position to
  the realm every 3 s (`Player::RefreshMemberReports`). The realm forwards `PartyMemberStats`
  to the party members who are not near the character. Since game protocol 39, its `Map` field
  carries the map id. The client keeps the last position (`PartyMember::position`/`mapId`), so
  members stay on the minimap and world map after they despawn, as long as they are on the
  player's map.
- **Guild:** the client polls `GuildMemberPositions` every 3 s while in a guild
  (`GuildClient::UpdateMemberPositions`). The realm answers with the online guild mates in the
  same world instance, so someone in another copy of a dungeon never shows up. It drops
  requests that come closer together than 1 s.
- A spawned unit's live position always wins over a reported one. A guild mate who is also in
  the party is drawn once, as a party member.
- **Minimap:** members beyond the minimap's circle are drawn on its edge, in their direction
  (`Minimap::s_edgeRadiusFactor`, `world_map::ClampToCircle`).
