# Multi-Class System

Status: **in development** (feature branch `feature/multi-class`, plus matching branches in the
`data/client` and `data/editor` submodules).

This document is the source of truth for the multi-class redesign. It is intentionally written up
front because the change spans all three server tiers, the client, the editor, the proto data and the
realm database, and will land in phases.

## Goal

A character is no longer permanently bound to the single class chosen at creation. Instead:

- A character has a **list of known classes**. Exactly one is **active** at any time.
- The character's `class` property always refers to the **active** class.
- Switching the active class is done by casting a **class-change spell** (a spell carrying the new
  `ChangeClass` effect). Learning that spell is what unlocks the ability to be that class.
- At creation the player still picks a class; that becomes the **initial** active class and the
  character is granted that class's class-change spell.
- Each known class carries its own **class level**, its own **talent points / talent ranks**, and its
  own **attribute-point spending profile**.

## Concepts and how they relate

| Concept | Scope | Drives |
|---|---|---|
| **Character level** | character-wide | base stats, HP/mana, attribute-point **total**, XP/leveling (cap 10) |
| **Class level** | per known class | that class's talent-point **pool** (cap 20+). Frozen at 1 for now. |
| **Talent points / ranks** | per known class | active class's talents apply; others are dormant |
| **Attribute total** | character-wide | how many attribute points exist to spend at the current character level |
| **Attribute spending** | per known class | how those points are allocated for the active class |
| **Spellbook** | class-bound + persistent | which spells are currently castable |

### Talent points

Talent points derive from **class level**, not character level. A freshly-acquired class starts at
class level 1 and therefore has a near-empty talent pool (the "0 warrior points" case), independent of
how high the character level is.

**Transitional note:** there is currently no mechanism to raise a class level — all class levels stay
at 1 until class-leveling is designed. Consequently, when this feature ships, existing characters keep
their character level but their (single) class drops to class level 1, so their talent points and
talents effectively reset. This is acceptable during internal development; we deliberately skip a
talent-preserving migration because the class-leveling concept itself is not yet designed.

### Attribute points

The **total** number of attribute points available is a function of **character level** and is the
same regardless of active class. The **allocation** of those points is stored **per class**:

- Switching class un-applies the old class's spending profile and applies the new class's profile.
- A class you have never allocated has its full pool unspent, giving a one-time spend opportunity; the
  allocation is remembered and restored whenever you return to that class.
- No manual resets are ever required when switching.

### Spellbook (class-bound, FFXIV-like)

Spells fall into two categories:

- **Persistent** — class-change spells, racials, professions, generic learned spells. Always known
  regardless of active class.
- **Class-bound** — class spells (`ClassEntry.spells`) and talent-granted spells. Only usable while
  their owning class is active.

A spell's owning class is **derived at runtime** (is it in some `ClassEntry.spells` list, or in a
talent tree of a class?) rather than stored as a tag, so no spell-table migration is needed and
existing data (racials etc.) is classified correctly. The active runtime spellbook is rebuilt on
login and on every class switch as: `persistent ∪ (active class's class spells up to its class level)
∪ (active class's talent-granted spells)`.

> Per-class **action bar** layouts are a likely follow-up but are **out of scope** for this pass.

## Class switching

A class-change spell carries the `ChangeClass` spell effect (`spell_effects::ChangeClass = 56`), with
`miscvaluea` = the target class id. `ClassEntry.class_change_spell` points back at the spell that
grants/activates the class (used to grant the initial class's spell at creation).

`ChangeClass` validates, server-side:

- caster is **not in combat**;
- the caster's **race may be that class** (reusing the same legality the character-creation screen
  uses: the race must have entries for the class id in its `initialSpells`/`initialItems` maps);
- if the class is not yet known, it is added at **class level 1**.

On success it swaps the active class: updates `object_fields::Class`, `PowerType`, `MaxLevel`,
re-derives talent points (from the new class level), swaps the attribute-spending profile, rebuilds
the spellbook, and refreshes stats.

## Data model

Per known class, persisted on the realm:

```
classId
classLevel   (uint8, default 1, frozen at 1 for now)
classXp      (uint32, reserved, 0 for now)
attributePointsSpent[5]
talentRanks  (talentId -> rank, scoped to this class)
```

### Realm database

New table `character_classes` (one row per class a character has known), and `character_talents`
gains a `class` column so talent ranks are scoped per class. `characters.class` keeps meaning the
active class. `characters.attr_0..attr_4` remain for now (still read by pre-Phase-3 code) and are
copied into the initial class row by the migration; they will be dropped in a later cleanup migration
once persistence is fully switched over to `character_classes`.

The realm bootstraps from the `data/realm/updates/*.sql` migrations only (the
`realm_db_schema_full.sql` snapshot is not referenced by the server), so the DB change is a single new
migration: `data/realm/updates/20260623_1_multi_class.sql`.

## Realm ↔ World transfer

`CharacterData` ([character_data.h](../src/shared/game_server/character_data.h)) replaces its flat
`talentRanks` with a `vector<CharacterClassData>` plus the active class id, serialized in both
`operator<<`/`operator>>`. Realm `database.h` / `mysql_database.cpp` load and save the new table set.

## Implementation phases

1. **Data spine** *(done)* — proto `ClassEntry.class_change_spell`, `ChangeClass` effect enum +
   editor name, realm DB migration (`character_classes`, `character_talents.class`).
2. **Transfer** *(done)* — `CharacterClassData` + `CharacterData.knownClasses` (replacing the flat
   `talentRanks`/`attributePointsSpent`) with serialization; realm DB load/save of `character_classes`
   and class-scoped `character_talents`; `CreateCharacter` seeds the initial class row. The active
   class is resolved via `CharacterData::GetActiveClass()` / `GetOrCreateActiveClass()`. GamePlayerS
   stays single-active-class until Phase 3, so in this phase every character knows exactly one class.
3. **World runtime** — `GamePlayerS` multi-class state, `HandleChangeClass`, per-class talent points &
   attribute spending, runtime spellbook rebuild, level/maxlevel guards.
4. **Character creation** — grant initial class + its class-change spell, seed `character_classes`.
5. **Client/UI** — replicate known classes + levels, character-window list.

## Open items / deferred

- Class XP and a mechanism to raise class level (currently frozen at 1).
- Per-class action bar layouts.
- Dropping the now-redundant `characters.attr_0..attr_4` columns after Phase 3.
- Possibly an explicit `RaceEntry.allowedClasses` list instead of inferring legality from the race's
  initial-data maps.
