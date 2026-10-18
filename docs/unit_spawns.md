# Unit spawn points

A unit spawn point (`UnitSpawnEntry` in `src/shared/proto_data/maps.proto`, stored per map in
`maps.data`) spawns up to `maxcount` creatures at its position and respawns them `respawndelay`
ms after they die. Movement, waypoints, stand state, health and extra triggers belong to the spawn
point; level, loot, kill credit, faction and AI belong to the spawned `UnitEntry`.

## Mixed spawns

A spawn point can spawn one of several units, for example a bandit camp where most points spawn a
Bandit and sometimes a Bandit Mage. List the candidates in `unitentries`:

| Field | Meaning |
|---|---|
| `unitentries[].unitentry` | A unit the point may spawn. |
| `unitentries[].weight` | Relative chance (default 1). Chance = weight / sum of weights. 0 = never. |
| `unitentry` | Still required: the fallback and the unit the editor previews. Keep it equal to the first entry. |

The world server rolls the entry in `CreatureSpawner::SpawnOne` (`RollUnitEntryId`) on **every**
spawn: the initial spawn, every respawn, and when a trigger re-activates the spawn. Each creature
of a `maxcount > 1` point rolls on its own. Without an entry of weight > 0 the point spawns
`unitentry` exactly as a plain spawn does; an unknown entry id falls back to `unitentry` (and logs a
warning). Clients only ever see the spawned creature, so mixed spawns are not a wire change.

Authoring:

- **Editor (mmo_edit):** select the spawn, open *Unit Spawn > Mixed Spawn*, press *Make Mixed
  Spawn* (seeds the list with the current unit), then *Add Entry* and set weights; the table shows
  each entry's chance. Remove entries until the list is empty to make it a plain spawn again.
  Spawn lists mark mixed spawns with `+N` (N other units).
- **npc-designer skill:** add `unitentries` to a spawn in the most common unit's JSON document;
  see `.agents/skills/mmo-npc-designer/references/npc-json-format.md` ("Mixed spawn"). Its validator
  rejects unknown entries and weights <= 0.

Tooling that asks "which units live here" (`tools/world` report, level-band lint, canon extract,
bot grind spots) counts every entry with weight > 0.
