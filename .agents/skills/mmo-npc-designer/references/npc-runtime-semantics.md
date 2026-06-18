<overview>
Creature behavior in this repository is not defined by data shape alone. Several runtime systems reinterpret or derive behavior from the raw fields.
</overview>

<interaction_semantics>
`GameCreatureS::SetEntry()` recomputes runtime NPC flags from linked data:

- trainer link present -> trainer flag
- vendor link present -> vendor flag
- gossip menus present -> gossip flag
- quests or end quests present -> questgiver flag

Because of this, setting `npcflags` by hand without the linked rows does not produce a functional NPC.
</interaction_semantics>

<faction_semantics>
Friendliness and hostility are resolved from `FactionTemplateEntry` plus its linked `FactionEntry`:

- Same faction template or same underlying faction -> friendly
- Explicit `enemies` list match -> hostile
- Explicit `friends` list match -> friendly
- `enemymask` vs `selfmask` -> hostile
- `friendmask` vs `selfmask` -> friendly

Choose faction templates deliberately. A neutral-looking NPC can still be non-interactable if its faction template makes it hostile to the player.
</faction_semantics>

<combat_semantics>
Creature combat AI reads `UnitEntry.creaturespells` and classifies behavior from the spell list and power type:

- Passive spells are skipped for active combat spell selection.
- Spell ranges come first from the creature-spell override, then from the spell's range type, then from melee fallback.
- More ranged spells than melee spells tends to push the AI into caster behavior.
- Rage users default to melee behavior.

`script_name` is optional. Current built-in registered names are discovered from `CreatureCombatScriptRegistry` and include `training_dummy` and `example_dungeon_boss`.
</combat_semantics>

<stat_system_semantics>
For modern units, `useStatBasedSystem = true` causes runtime stat calculation from `unitClassId` and the scaling fields on `UnitEntry`. Existing project data is already biased toward this path.

If `useStatBasedSystem = false`, the runtime falls back to legacy fields such as fixed health, mana, armor, and melee damage.
</stat_system_semantics>

<loot_semantics>
`LootInstance` evaluates loot one group at a time:

- Conditional drops are skipped if no eligible loot recipient satisfies the condition.
- Non-zero `dropchance` entries compete inside their group.
- `dropchance == 0` acts as an equal-chance pool fallback when no non-equal-chance entry won.
- Gold is rolled separately from `minmoney` and `maxmoney`.

This means loot groups matter. Flattening every possible item into one big group changes behavior.
</loot_semantics>

<gossip_semantics>
Gossip handling is condition-first and order-sensitive:

- The first gossip menu on the unit whose menu condition passes becomes the active menu.
- Each option can have its own condition.
- `Vendor` and `Trainer` gossip actions use the unit's linked vendor or trainer rows.
- `Trigger` gossip actions raise `OnGossipAction` on the unit; trigger event data must match the active menu ID and option ID.
</gossip_semantics>
