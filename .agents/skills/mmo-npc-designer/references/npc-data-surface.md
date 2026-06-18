<overview>
The creature authoring surface in this repository spans multiple editor data files. `units.data` stores the base creature or NPC row, but most meaningful behavior comes from linked tables and map placement data.
</overview>

<data_files>
- `units.data` from `units.proto`
  Base creature row: name, levels, faction template, model IDs, combat stats, stat-system fields, linked loot or service IDs, creature spells, quest links, triggers, and gossip menu links.

- `unit_loot.data` from `unit_loot.proto`
  Unit drop tables. Each `LootEntry` owns grouped drop definitions and optional gold ranges.

- `vendors.data` from `vendors.proto`
  Vendor inventories. A unit becomes a vendor in practice when `unit.vendorentry` points at a live row.

- `trainers.data` from `trainers.proto`
  Trainer inventories. A unit becomes a trainer in practice when `unit.trainerentry` points at a live row.

- `gossip_menus.data` from `gossip_menus.proto`
  Dialog text and interaction options. `unit.gossip_menus` is an ordered list of possible menus. The first menu whose condition passes is used for the player.

- `conditions.data` from `conditions.proto`
  Condition rows referenced by gossip menus, gossip options, and conditional loot definitions.

- `triggers.data` from `triggers.proto`
  Trigger rows referenced by `unit.triggers`. Gossip trigger actions raise unit trigger events; matching depends on menu and option IDs.

- `maps.data` from `maps.proto`
  Actual placement. Units do not appear in the world unless they are spawned on a map.

- `factions.data` and `faction_templates.data`
  Hostility and friendliness configuration.

- `model_data.data`
  Display and model IDs. `unit.maleModel` and `unit.femaleModel` point here.
</data_files>

<live_defaults>
Useful current fallback model IDs from live data:

- Human female NPC default: `3`
- Human male NPC default: `4`
- Orc male player model: `12`
- Orc female player model: `13`
- Undead human male player model: `17`
- Undead human female player model: `18`

These are practical temporary defaults when no bespoke creature display is available yet.
</live_defaults>

<important_fields>
Important `UnitEntry` fields for this project:

- `factionTemplate`
  The actual friendliness or hostility anchor used at runtime.

- `vendorentry`, `trainerentry`, `gossip_menus`, `quests`, `end_quests`
  The real interaction links. These matter more than stored `npcflags`.

- `unitlootentry`
  Linked unit loot table. Existing content also uses `4294967295` as an explicit no-loot sentinel for NPCs that should not drop anything.

- `creaturespells`
  The combat or passive spell list. Non-passive spells are loaded into combat AI. Passive spells are learned on the unit and persist through death in the spell system.

- `useStatBasedSystem`, `unitClassId`, `baseDamageVariance`, `damagePerLevel`, `baseArmor`, `armorPerLevel`, `eliteStatMultiplier`
  The modern creature stat authoring surface.

- `script_name`
  Optional combat-script hook. Must match a script actually registered in C++.

- `auto_attack_spell`
  Optional spell-driven auto attack.
</important_fields>

<spawn_notes>
`MapEntry.unitspawns` controls creature placement and movement:

- `unitentry`
  Points at the base `UnitEntry`.

- `movement`
  `0 = STATIONARY`, `1 = RANDOM`, `2 = PATROL`.

- `locations`
  Preferred modern placement container.

- `waypoints`
  Patrol path used when movement is patrol.

- `name`
  Optional but strongly recommended for idempotent updates.
</spawn_notes>
