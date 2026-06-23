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

A spell's owning class is **derived at runtime from its `classmask`** (a spell with no class mask is
persistent; otherwise it belongs to the masked class(es)), rather than stored as a tag, so no
spell-table migration is needed and existing data (racials etc.) is classified correctly.
`GamePlayerS::m_knownSpellIds` holds every learned spell across all classes (the persistence set,
talent spells excluded); the live `m_spells` is the subset usable by the active class (plus persistent
spells). On login (`SetKnownSpells`) and on every class switch (`ActivateKnownSpellsForCurrentClass`
+ `GrantClassSpells` + `InitializeTalents`) the live book is rebuilt; inactive classes' spells are
kept (preserved across logout) but hidden and uncastable.

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

> **Class ids are 0-based.** In the shipped data Mage = 0, Warrior = 1, Cleric = 2, Acolyte = 3,
> Scout = 4. **Class id 0 is a valid class**, so never treat a `0` class id (or a `ChangeClass`
> effect's `miscvaluea` of 0) as "no class" — guard on `classes.getById(id) != nullptr` or on
> `m_classEntry` instead. (Several early guards using `== 0` were fixed because they silently dropped
> Mage from registration / per-class persistence.)

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
active class. The legacy `characters.attr_0..attr_4` columns were copied into the initial class row by
the first migration and have since been dropped (`20260623_3_drop_legacy_attr_columns.sql`) now that
per-class attribute spending lives entirely in `character_classes`. `character_actions` gains a
`class` column so each known class keeps its own action bar layout.

The realm bootstraps from the `data/realm/updates/*.sql` migrations only (the
`realm_db_schema_full.sql` snapshot is not referenced by the server), so the DB changes are new
migrations: `20260623_1_multi_class.sql`, `20260623_2_per_class_action_bars.sql` and
`20260623_3_drop_legacy_attr_columns.sql`.

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
3. **World runtime** *(done)* — `GamePlayerS` holds `m_knownClasses`; `ChangeClass(classId)` swaps the
   active class (combat + race-legality gating, adds at class level 1 if new), rebuilds the class-bound
   spellbook (removes old class + talent spells, grants new), swaps the attribute-spending profile,
   restores per-class talents, and clamps level to the new class's max. Talent points now derive from
   class level via `UpdateTotalTalentPoints`; attribute total stays character-level via
   `UpdateTotalAttributePoints` (both recomputed in `RefreshStats`). `HandleChangeClass` spell effect
   (miscvaluea = target class id) is wired into the dispatch table. `GamePlayerS` serialization +
   enter-world (`SetKnownClasses`) + realm `NotifyCharacterUpdate` now carry the full known-class set
   and active class id. Live client sync via existing spellLearned/Unlearned/talentsReset signals.
4. **Character creation + spellbook persistence** *(done)* — creation grants the initial class's
   `class_change_spell`. `GamePlayerS` now keeps `m_knownSpellIds` (the full learned set across all
   classes, talent spells excluded) as the persistence authority, separate from the live/castable
   `m_spells` (active class + persistent spells only). `SetKnownSpells` loads all and activates only
   the active-class subset; `ActivateKnownSpellsForCurrentClass` rebuilds the live book on switch.
   Spells of inactive classes are preserved (no longer dropped on login) but hidden from the spellbook
   and uncastable. Save persists `GetKnownSpellIds()`; the set is serialized in `GamePlayerS`.
5. **Client/UI** *(done)* — the world node sends a `KnownClasses` packet (active class id, then per
   class: id, class level, class-change spell id) on spawn and after every switch (alongside the active-class
   `object_fields::Class` and the refreshed spellbook). The client caches it on `GamePlayerC`
   (`SetKnownClasses` / `GetKnownClasses` / `GetKnownClassEntry`) and exposes it to Lua via
   `UnitHandle` (`GetKnownClassCount`, `GetKnownClassName`, `GetKnownClassLevel`,
   `IsKnownClassActive`, `GetKnownClassChangeSpell`). A `PLAYER_KNOWN_CLASSES_CHANGED` event fires on
   update; the character window shows a "Classes" section listing each known class with its level,
   highlighting the active one. Each non-active row is a button that casts that class's class-change
   spell to switch to it. The spellbook refreshes on the `PLAYER_SPELLS_CHANGED` event so a class
   switch updates the visible spells immediately.

   A class is registered (and replicated) the moment its **class-change spell is learned**, not only
   when first switched to: `GamePlayerS::OnSpellLearned` detects the `ChangeClass` effect, adds the
   class at level 1, and fires `knownClassesChanged` (→ world `Player::OnKnownClassesChanged` →
   `SendKnownClasses` + persist), so a freshly-trained class appears in the list right away even
   before activation.

## Open items / deferred

- Class XP and a mechanism to raise class level (currently frozen at 1). **Still deferred** — the
  class-leveling concept itself is not yet designed.

### Done since the initial plan

- **Per-class action bar layouts** *(done)* — `character_actions` gains a `class` column
  (`20260623_2_per_class_action_bars.sql`), so each known class keeps its own bar. The realm
  `Player` tracks which class the live `m_actionButtons` belong to (`m_actionButtonClassId`), loads
  the active class's bar on login, and `SwitchActionBarClass` saves the old bar and loads/sends the
  new one when the active class changes. The change is detected in `NotifyCharacterUpdate`; the world
  node now calls `SaveCharacterData()` from `OnClassChanged` so the realm swaps promptly instead of
  waiting for the next level-up/logout save. `CreateCharacter` seeds the initial bar under the
  initial class. When a class first becomes known, `NotifyCharacterUpdate` seeds its bar via
  `SeedDefaultActionButtons` / `BuildDefaultActionButtons` (the class's non-passive ability spells, up
  to the character level) — the same rule character creation uses — so a freshly-acquired class
  switches into a populated bar instead of an empty one.
- **`RaceEntry.allowedClasses`** *(done)* — explicit `repeated uint32 allowedClasses` on `RaceEntry`
  (with an editor checkbox list). `IsClassAllowedForRace` and character-creation legality prefer this
  list when non-empty and fall back to inferring from the initial-data maps (`initialSpells` /
  `initialItems` / `initialActionButtons`) when it is empty, so existing data keeps working.
- **Dropped `characters.attr_0..attr_4`** *(done)* — the realm no longer reads or writes these legacy
  character-wide columns (per-class spending lives in `character_classes`); they are dropped by
  `20260623_3_drop_legacy_attr_columns.sql`.
