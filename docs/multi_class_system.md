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
| **Class level** | per known class | that class's talent-point **pool**. Leveled via **class XP** against `ClassEntry.classlevels` (own cap, may exceed the character cap). |
| **Talent points / ranks** | per known class | active class's talents apply; others are dormant |
| **Attribute total** | character-wide | how many attribute points exist to spend at the current character level |
| **Attribute spending** | per known class | how those points are allocated for the active class |
| **Spellbook** | class-bound + persistent | which spells are currently castable |

### Talent points

Talent points derive from **class level**, not character level. A freshly-acquired class starts at
class level 1 and therefore has a near-empty talent pool (the "0 warrior points" case), independent of
how high the character level is.

With class leveling (phase 6), the talent-point pool is the sum of
`ClassEntry.classlevels[0..classLevel-1].talentpoints`. A class **without** a `classlevels` curve
falls back to the legacy pool derived from `levelbasevalues` and stays frozen at class level 1.

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
classLevel   (uint8, default 1; raised by class XP against ClassEntry.classlevels)
classXp      (uint32, XP towards the next class level; 0 at the class cap)
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
5. **Client/UI** *(done)* — the world node sends a `KnownClasses` packet (active class id, then every
   configured class with unlocked state, class level/max level, reserved class XP/next-rank XP, and
   class-change spell id) on spawn and after every switch (alongside the active-class
   `object_fields::Class` and the refreshed spellbook). The client caches it on `GamePlayerC`
   (`SetKnownClasses` / `GetKnownClasses` / `GetKnownClassEntry`) and exposes it to Lua via
   `UnitHandle` (`GetKnownClassCount`, `GetKnownClassName`, `GetKnownClassLevel`,
   `IsKnownClassActive`, `GetKnownClassChangeSpell`, and progression accessors). A
   `PLAYER_KNOWN_CLASSES_CHANGED` event fires on update; the character window has separate Attributes
   and Classes tabs. The Classes tab shows configured classes, disables locked entries, marks the
   active class, and shows rank and class-XP progress. Each unlocked non-active row casts that class's
   class-change spell to switch to it. Class-change spells remain persistent regardless of class mask,
   and the spellbook refreshes on `PLAYER_SPELLS_CHANGED` after a switch.

   A class is registered (and replicated) the moment its **class-change spell is learned**, not only
   when first switched to: `GamePlayerS::OnSpellLearned` detects the `ChangeClass` effect, adds the
   class at level 1, and fires `knownClassesChanged` (→ world `Player::OnKnownClassesChanged` →
   `SendKnownClasses` + persist), so a freshly-trained class appears in the list right away even
   before activation.

6. **Class XP & class leveling** *(done)* — killing creatures rewards the ACTIVE class with class XP
   computed by the same formula as character XP but with **class levels substituted everywhere**
   (hooked in `CreatureAIDeathState`): the level-difference scaling uses the highest eligible class
   level among the recipients, the grey cutoff uses each recipient's own class level, and the group
   split weighs by class-level share. A level-5 character with a level-2 class killing a level-1
   creature therefore gets near-full class XP even though its character XP is heavily reduced — and
   conversely, creatures below the *class's* grey level grant no class XP even if the character
   still gets some. Class XP keeps flowing when the character level is capped
   (`RewardClassExperience` is independent of the character cap). Regular quest `rewardxp`
   deliberately does **not** feed class XP (a completed quest could otherwise be banked and turned
   in as a different class); quests instead carry an explicit `rewardclassxp` field. On turn-in it
   is scaled by the active class level relative to `questlevel` (`ScaleQuestClassXp`,
   `src/shared/game_server/quest_class_xp.h`): an under-leveled class receives the same fraction of
   its own class-level bar that the full reward represents at the quest's level — the XP a quest
   designed for the class level would grant — so banked high-level quests cannot power-level a
   fresh class, while an over-leveled class steps down through the character quest XP factor table
   (full up to quest level + 5, then 80/60/40/20/10%). The per-class curve lives in `ClassEntry.classlevels` (repeated
   `ClassLevelEntry { xptonextlevel, talentpoints }`, editable in the class editor): the class max
   level is the entry count (clamped to 255, may exceed the character cap), and the talent-point
   pool is summed from it (`UpdateTotalTalentPoints`; classes without a curve keep the legacy
   `levelbasevalues` pool and stay frozen at class level 1, accruing no XP). XP gains are pushed via
   the lightweight `ClassXpUpdate` packet (updates the Classes tab via
   `PLAYER_KNOWN_CLASSES_CHANGED`, chat notification on level-up); a level-up additionally resends
   `KnownClasses` and persists via `SaveCharacterData` (per-kill XP is covered by the regular save
   points: logout, character level-up, class switch).

   **Quest gating by ACTIVE class:** `requiredclasses` is enforced against the active class at
   offer, accept, and (newly) turn-in (`RewardQuest`/`OnQuestGiverCompleteQuest`). Quests already in
   the log are **frozen** while a non-matching class is active: no kill/item/spell-cast objective
   credit, no questgiver icon or menu entry, no turn-in. The client renders frozen log/tracker
   entries greyed with a "(Wrong Class)" annotation (`QuestInfo.requiredClasses` +
   `IsQuestAllowedForClass` in Lua, refreshed via `QUEST_LOG_UPDATE` on every class switch).

   **Trainer gating:** class trainers (`TrainerEntry.type == CLASS_TRAINER`) now enforce their
   `classid` (only the matching active class is served — list and buy; note class id 0 = Mage, so
   the check uses `has_classid()`), and their per-spell `reqlevel` is checked against the player's
   **class level** instead of the character level (other trainer types keep character-level
   semantics). The `TrainerList` packet carries the trainer type so the client mirrors the check
   (`GetTrainerSpellReqLevel`/`IsTrainerClassTrainer`/`UnitHandle:GetActiveClassLevel`), and a new
   `FailedWrongClass` buy error is surfaced via the error frame.

## Open items / deferred

- Per-class action bars, class-change spells, proficiency handling etc. are done (see below); no
  known open items remain from the original plan.

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
- **Proficiency spells & disabled gear on class switch** *(done)* — weapon/armor proficiencies are
  granted by passive spells carrying a `Proficiency` effect (`spell_effects::Proficiency`,
  `miscvaluea` = proficiency id) that live in a class's `ClassEntry.spells`. They are identified by
  that effect rather than tagged in proto data. The leak had two causes: (a) these spells carry **no
  class mask**, so the spellbook logic treated them as *persistent* and never deactivated them on a
  switch (they stayed castable, kept showing in the spellbook, and kept granting their proficiency);
  and (b) `GameUnitS::RemoveSpell` does not drop proficiencies. The fix treats a proficiency spell as
  **class-bound to whichever class lists it in `ClassEntry.spells`**: `IsSpellActiveForCurrentClass`
  returns false for a proficiency spell the active class does not grant (`IsSpellGrantedByActiveClass`),
  and `ActivateKnownSpellsForCurrentClass` deactivates such spells on a switch even though they have no
  class mask. `GamePlayerS::RefreshProficiencies()` then recomputes the active proficiency set from the
  remaining active passive spells after every switch (and the spawn path), so only the active class's
  (plus persistent) proficiency spells contribute. `ChangeClass` strips all
  equipped item stats while the old proficiencies still hold, prunes proficiencies to the new class,
  then re-applies — the existing proficiency guard in `GamePlayerS::ApplyItemStats` lets only the new
  class's usable items contribute. Equipped items the new class can no longer use (revoked proficiency,
  or dual-wield for an off-hand weapon — see `ItemValidator::IsEquippedItemUsable`) are kept in their
  slot but disabled by `Inventory::RevalidateEquippedItems()`: the runtime `item_flags::Disabled` flag
  is set (replicated, stripped on load so it is recomputed), the visual/display id is cleared, and the
  item set effect is removed. Switching back re-enables them automatically; the flag is also cleared
  when an item leaves its equipment slot (`EquipmentManager`). The client exposes `ItemHandle:IsUsable()`
  to Lua: the character window tints disabled equipped icons red, adds a tooltip line
  (`ITEM_DISABLED_WRONG_CLASS`) and shows an aggregate `CharacterEquipmentWarning`
  (`CHARACTER_EQUIPMENT_DISABLED_WARNING`) when any slot is disabled.
