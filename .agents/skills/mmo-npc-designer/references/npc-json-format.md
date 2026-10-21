<overview>
The NPC JSON wrapper is designed for safe iteration across multiple linked data files while keeping one creature-centric draft.
</overview>

<wrapper>
Use this wrapper:

```json
{
  "format": "mmo-npc",
  "version": 1,
  "unit": {},
  "loot_entry": {},
  "vendor_entry": {},
  "trainer_entry": {},
  "gossip_menus": [],
  "spawns": []
}
```

Only `unit` is required. Other sections are optional and should be omitted when not needed.
</wrapper>

<section_rules>
- `unit`
  Plain `UnitEntry` fields using protobuf field names.

- `loot_entry`
  Plain `LootEntry` object. Include it only when the unit should own or update a loot table.

- `vendor_entry`
  Plain `VendorEntry` object. Include it only when the unit should own or update a vendor inventory.

- `trainer_entry`
  Plain `TrainerEntry` object. Include it only when the unit should own or update a trainer inventory.

- `gossip_menus`
  Array of plain `GossipMenuEntry` objects for every menu that should be created or updated together with the unit.

- `spawns`
  Array of wrapper objects, not raw `UnitSpawnEntry` rows:

```json
{
  "map_id": 0,
  "replace_mode": "by_name",
  "spawn": {
    "name": "oakshire_warrior_trainer",
    "unitentry": 10,
    "movement": 0,
    "locations": [
      {
        "positionx": 0.0,
        "positiony": 0.0,
        "positionz": 0.0,
        "rotation": 0.0
      }
    ]
  }
}
```

`replace_mode` currently supports:

- `append`
- `by_name`
- `replace_first_for_unit`
- `replace_by_map_and_index`

For `replace_by_map_and_index`, also include `match_index` with the spawn's current zero-based index inside that map's `unitspawns` array.

Mixed spawn (one spawn point, several possible units, re-rolled on every spawn and respawn):
add `unitentries`, a list of `{unitentry, weight}`. The chance of an entry is
weight / sum of weights. Author it in the document of the most common unit:

```json
{
  "map_id": 0,
  "replace_mode": "by_name",
  "spawn": {
    "name": "Oakenshire - Bandit Camp 03",
    "unitentry": 120,
    "unitentries": [
      { "unitentry": 120, "weight": 3 },
      { "unitentry": 121, "weight": 1 }
    ],
    "respawn": true,
    "respawndelay": 90000,
    "locations": [ { "positionx": 0.0, "positiony": 0.0, "positionz": 0.0, "rotation": 0.0 } ]
  }
}
```

Here 120 (Bandit) spawns 75% and 121 (Bandit Mage) 25% of the time. Rules the validator enforces:
`unitentries[0].unitentry` is the document's unit and equals `unitentry` (the server's fallback and
the editor's preview); every other entry must already exist in project data (apply its own
document first); every `weight` is an integer > 0 (omitted means 1). Leave `unitentries` out for a
plain single-unit spawn.
</section_rules>

<authoring_rules>
- Keep IDs numeric and explicit.
- Prefer omitting unused optional sections over filling them with placeholder rows.
- Use one JSON file per creature or NPC concept.
- Keep spawn names stable when you want idempotent re-application.
- If a linked entry ID is new, include the linked section in the same JSON so validation can resolve it.
</authoring_rules>
