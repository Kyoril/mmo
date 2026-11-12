# Pet System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Units own pets: temporary nameless swarms (players and NPCs) and persistent named pets (players), with owner-based friendliness, pet AI, persistence, taming, UI and GM commands.

**Architecture:** The replicated `object_fields::Owner` guid marks every pet; friendliness resolves owner chains on server, client and bots. A per-owner `PetManager` (world side, `src/shared/game_server/pets/`) tracks pet groups, caps and lifetimes and drives a pet mode of `CreatureAI`. Persistent pets are `PetEntry` data referencing a creature entry, stored by a realm `PetModule` in two new realm tables and handed realm↔world like quest data.

**Tech Stack:** C++17, protobuf (proto_data), asio servers, MySQL migrations, Catch2, frame_ui Lua/XML, Python content scripts, Lua E2E scenarios.

**Spec:** `docs/superpowers/specs/2026-11-01-pet-system-design.md` (read it first; section numbers below refer to it).

## Global Constraints

- Code style per CLAUDE.md: Allman braces, tabs, braces on every `if`, `m_camelCase`, PascalCase methods, `#pragma once`, Doxygen on public members, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions; use `ASSERT`/`VERIFY`/`ELOG` etc.
- `spell_effects` is append-only: `SummonPersistentPet = 58`, `GrantPersistentPet = 59`, `DismissPet = 60`, `RevivePet = 61`, then `Count_`.
- Wire changes: bump `game::ProtocolVersion` (0x22 → 0x23) and `auth::ProtocolVersion` (0x0C → 0x0D) once each, run `python tools/protocol_version_check.py --update`, record in `docs/protocol_versions.md`.
- Realm migration is additive: `data/realm/updates/20261101_1_character_pets.sql` plus the history INSERT and tables in `data/realm/realm_db_schema_full.sql`.
- Servers single-threaded io; pet code runs on the world thread only. Spawning from a spell effect is deferred via `world->GetUniverse().Post(...)` like `HandleSummon`.
- New proto manager `pets` is optional (`ManagerEntry(..., true)`), so older data folders still load.
- Localize every UI string in deDE, enUS, frFR, ruRU.
- New subsystem `PETS` (`src/shared/game/subsystem.h`).
- Server code must build with gcc on Linux (servers + `game_server_tests` headless).
- Stored persistent pets per character: 5 (`MaxStoredPets` constant in `pets/pet_defines.h`). `ClassEntry.max_summoned_persistent_pets` default 1.
- Data submodules (`data/client`, `data/editor`) get their own commits; the gitlink is bumped in the main repo.

## Review Focus

1. **Owner dies/despawns mid-`Post`**: a summon posted to the universe after its owner left must not spawn (weak owner lock fails → no spawn). Test in Task 4.
2. **Pet guid collisions**: two characters' stored pets must never share a `GuidType::Pet` guid; guid = `CreateEntryGUID(petDbId, petEntry, GuidType::Pet)` built from the realm row id. Test in Task 7.
3. **Rename input**: empty, >12 chars, non-letters, or a pet whose entry forbids naming are refused with an error and change nothing. Test in Task 9.
4. **Owner logs out while a pet is in combat**: temporary pets despawn, persistent pet state (health, power, XP) is saved, no crash on the pet's threat list referencing the gone owner. Test in Task 4 (temporary) and Task 7 (persistent).
5. **Owner chain loops / depth**: a pet whose owner is itself or a 5-deep chain must not recurse forever; resolution stops at depth 4. Test in Task 1.

---

### Task 1: Owner-aware friendliness (server)

**Files:**
- Modify: `src/shared/game_server/objects/game_unit_s.h/.cpp` (`UnitIsEnemy` :3502, `UnitIsFriendly` :3544)
- Modify: `src/shared/game_server/spells/spell_effects.cpp` (`HandleSummon` sets `Owner`)
- Test: `src/tests/game_server_tests/pet_friendliness_test.cpp`

**Interfaces:**
- Produces: `[[nodiscard]] uint64 GameUnitS::GetOwnerGuid() const` (reads `object_fields::Owner`); `void GameUnitS::SetOwnerGuid(uint64)`; `[[nodiscard]] const GameUnitS& GameUnitS::GetTopOwner() const` (follows Owner via `GetWorldInstance()->FindByGuid`, depth limit `MaxOwnerChainDepth = 4`, self when unresolved); `[[nodiscard]] bool GameUnitS::SharesTopOwnerWith(const GameUnitS&) const`.
- Rule: `UnitIsFriendly(other)`: true if `SharesTopOwnerWith(other)`; otherwise the existing faction logic applied to `GetTopOwner()` vs `other.GetTopOwner()`. `UnitIsEnemy` mirrors it (false when sharing a top owner). Keep the faction body as a private `FactionIsEnemy/FactionIsFriendly(const GameUnitS&, const GameUnitS&)`.

- [ ] **Step 1: Write failing tests** — set up a world instance like `kill_reward_distance_test.cpp`; factions: A (player), B (hostile monster) enemies of each other.
  - `"pet of a player is friendly to its owner even with a hostile faction"`: creature with faction B, `SetOwnerGuid(player)` → `pet.UnitIsFriendly(player)` and `player.UnitIsFriendly(pet)` true, `UnitIsEnemy` false both ways.
  - `"pet is hostile to its owner's enemies"`: other creature faction B (no owner) → `pet.UnitIsEnemy(other)` true.
  - `"pets of the same owner are friendly"`.
  - `"owner chain deeper than the limit or looping terminates"`: A owns B owns A → calls return without hanging.
- [ ] **Step 2: Run** `ctest -C Debug -R game_server_tests` (or `bin/Debug/game_server_tests "[pets]"`) — FAIL.
- [ ] **Step 3: Implement** the accessors and rule; `HandleSummon` calls `summon->SetOwnerGuid(owner->GetGuid())` next to `SetSummonOwner`.
- [ ] **Step 4: Run** — PASS, and the full `game_server_tests` suite stays green.
- [ ] **Step 5: Commit** `feat(pets): owner-aware friendliness on the server`.

### Task 2: Pet data model

**Files:**
- Create: `src/shared/proto_data/pets.proto`
- Modify: `src/shared/proto_data/units.proto` (`optional uint32 tame_pet_entry = <next free>`), `classes.proto` (`optional uint32 max_summoned_persistent_pets = <next free> [default = 1]`), `spells.proto` `SpellEffect` (`pet_group`, `pet_max_count`, `pet_limit_policy`, `pet_level_source`, all optional uint32), `src/shared/proto_data/project.h` (typedef, member `pets`, load as optional, save)
- Modify: `src/shared/game/spell.h` (append effects 58–61), `src/mmo_edit/editor_windows/spell_editor_window.cpp:98/159` (effect names)
- Create: `src/shared/game/pet_defines.h` (enums shared by server and client)
- Test: `src/tests/game_tests/pet_defines_test.cpp`

**Interfaces:**
- Produces in `pets.proto` (package `mmo.proto`): `message PetLevelSpell { required uint32 spell = 1; required uint32 level = 2; }`, `message PetEntry { required uint32 id = 1; required string name = 2; repeated LocalizedString name_loc = 200; required uint32 unit_entry = 3; optional uint32 display_id = 4; optional uint32 name_mode = 5; repeated string random_names = 6; optional bool allow_player_naming = 7; optional uint32 level_mode = 8; optional uint32 level_source = 9; repeated uint32 default_spells = 10; repeated PetLevelSpell level_spells = 11; repeated UnitSpellEntry ai_spells = 12; optional float health_multiplier = 13 [default=1]; optional float mana_multiplier = 14 [default=1]; optional float damage_multiplier = 15 [default=1]; optional float armor_multiplier = 16 [default=1]; }` and `message Pets { repeated PetEntry entry = 1; }` (match the container pattern of the other protos). `random_names` localization: builder may use `repeated LocalizedString`; keep it simple.
- `pet_defines.h` (namespace `mmo`): `namespace pet_name_mode { enum Type { Fixed, RandomFromList }; }`, `namespace pet_level_mode { enum Type { OwnLevel, MasterLevel }; }`, `namespace pet_level_source { enum Type { Creature, CharacterLevel, ClassLevel }; }` (Creature only valid for temporary pets), `namespace pet_limit_policy { enum Type { ReplaceOldest, FailAtCap }; }`, `namespace pet_strategy { enum Type { Assist, Passive }; }`, `namespace pet_command { enum Type { Attack, Follow, Stay, SetStrategy, Dismiss }; }`, `namespace pet_result { enum Type { Ok, TooManyPets, NoPet, PetDead, NotTamable, TooHighLevel, NoFreeSlot, AlreadyHavePet, WrongClass, InvalidName, NamingNotAllowed, Disabled }; }`, `constexpr uint32 MaxStoredPets = 5;`, `constexpr uint32 MaxPetNameLength = 12;`, `bool IsValidPetName(const String& name)` (1..12 chars, letters only, first upper-case after normalization).

- [ ] **Step 1: Failing test** `pet_defines_test.cpp`: `IsValidPetName("Shade")` true; `""`, `"ThirteenChars"`(13), `"Sh4de"`, `"Sha de"` false; `spell_effects::Count_ == 62`.
- [ ] **Step 2: Run** `bin/Debug/game_tests` — FAIL.
- [ ] **Step 3: Implement** the proto/enum/registration changes. Load `pets` with `optional = true`.
- [ ] **Step 4: Build** `mmo_edit`, `world_server`, `realm_server`; run `game_tests` — PASS; open nothing yet.
- [ ] **Step 5: Commit** `feat(pets): pet data model and spell effect ids`.

### Task 3: Pets editor + new fields in existing editors

**Files:**
- Create: `src/mmo_edit/editor_windows/pet_editor_window.h/.cpp` (follow `creature_editor_window.*` / `editor_entry_window_base.h`)
- Modify: `src/mmo_edit/mmo_edit.cpp:263` (register), `creature_editor_window.cpp` (`tame_pet_entry` combo), class editor (`max_summoned_persistent_pets`), `spell_editor_window.cpp` (pet fields shown for effects 43/58/59; `pet_limit_policy`/`pet_level_source` combos)

- [ ] **Step 1:** Implement the window: id/name/name_loc, unit entry combo, display override, name mode + random names list, allow naming, level mode/source, default spells list, level spells table, ai_spells (reuse the creature editor's `UnitSpellEntry` table widget — extract it into a shared helper if it's inline), multipliers.
- [ ] **Step 2: Build** `mmo_edit` (Debug) — compiles; launch, create a pet entry, save, reopen: values persist.
- [ ] **Step 3: Commit** `feat(editor): pet entry editor and pet fields`.

### Task 4: PetManager and temporary pets (`SummonPet`)

**Files:**
- Create: `src/shared/game_server/pets/pet_manager.h/.cpp`
- Modify: `game_unit_s.h/.cpp` (owns a `std::unique_ptr<PetManager> m_pets` lazily; `PetManager& GetPets()`; death/despawn hooks call `m_pets->DespawnTemporaryPets()`), `game_creature_s.*` (`bool IsPet() const`, `uint32 GetPetGroup() const`, `SetPetGroup`), `spell_effects.cpp` (`HandleSummonPet`), `creature_ai.cpp` (`Reset()` of an owner despawns its temporary pets)
- Test: `src/tests/game_server_tests/pet_manager_test.cpp`

**Interfaces:**
- Produces: `class PetManager final : public NonCopyable` with `explicit PetManager(GameUnitS& owner)`;
  - `pet_result::Type CanAddToGroup(uint32 group, uint32 maxCount, pet_limit_policy::Type policy) const`
  - `void AddTemporaryPet(GameCreatureS& pet, uint32 group, uint32 maxCount, pet_limit_policy::Type policy)` (replace-oldest despawns the oldest of that group first; pets in insertion order)
  - `uint32 GetGroupCount(uint32 group) const`
  - `void DespawnTemporaryPets()`
  - `void ForEachPet(const std::function<void(GameCreatureS&)>&)` (temporary + persistent)
  - `void Command(pet_command::Type, uint64 targetGuid, uint32 param)` (forwarded to each pet's AI, Task 5)
  - `GameCreatureS* GetPersistentPet() const` / `void SetPersistentPet(GameCreatureS*)` (used by Task 7)
  - Pets are held as `std::weak_ptr<GameCreatureS>`; a pet's `despawned` signal removes it.
- `uint32 ResolvePetLevel(pet_level_source::Type, const GameUnitS& owner, const proto::UnitEntry&)` free function in `pet_manager.h`: Creature → entry range roll (keep `Initialize`'s roll), CharacterLevel → owner `GetLevel()`, ClassLevel → owner's active class level for players (`GamePlayerS::GetActiveClassLevel`), `GetLevel()` for creatures.
- `HandleSummonPet`: entry `summonunit`, count `max(1, basePoints)`, group `pet_group ?: spell id`, cap `pet_max_count ?: count`, policy; `FailAtCap` checked *before* the cast (add a check in the spell's cast validation path returning `spell_cast_result` "too many pets" — reuse an existing failure result if one fits, otherwise append one); spawn via `Post`, set `Owner`, `SetSummonOwner`, `SetGrantsKillRewards(false)`, faction = owner's faction template, level via `ResolvePetLevel` then `SetLevel`, `SetPetGroup`, start pet AI (Task 5), despawn timer if spell `duration > 0`.

- [ ] **Step 1: Failing tests** (`[pets]`):
  - `"replace oldest keeps the group at its cap"`: cap 5, add 6 → count 5, the first-added is despawned.
  - `"fail at cap refuses the sixth"`: `CanAddToGroup` returns `TooManyPets`.
  - `"groups are counted separately"`.
  - `"owner death despawns temporary pets"`, `"owner despawn despawns temporary pets"`, `"npc owner reset despawns its pets"`.
  - `"summon posted after the owner is gone spawns nothing"` (Review Focus 1).
  - `"temporary pet without duration lives while the owner lives"`.
  - `"ResolvePetLevel uses class level for players and creature level for npcs"`.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — PASS; full suite green.
- [ ] **Step 5: Commit** `feat(pets): pet manager and temporary pet summons`.

### Task 5: Pet AI (follow, assist, passive, stay, commands)

**Files:**
- Create: `src/shared/game_server/ai/creature_ai_pet_state.h/.cpp` (follow + engage logic while owned), `src/shared/game_server/pets/pet_behavior.h/.cpp` (pure helpers)
- Modify: `creature_ai.h/.cpp` (`void EnterPetMode(GameUnitS& owner)`; `Reset`/`Idle` route to the pet state when owned; `void OnPetCommand(pet_command::Type, uint64 target, uint32 param)`; strategy member), `creature_ai_combat_state.cpp` (when owned: no reset-to-home, `ShouldResetAI` → return to pet state when no targets or owner out of combat; chooses spells from the pet's known spells + `ai_spells`), `game_unit_s.cpp` (owner enters combat when its pet does; owner attacked/attacking notifies `PetManager`)
- Test: `src/tests/game_server_tests/pet_ai_test.cpp`

**Interfaces:**
- `Vector3 PetFollowSlot(const Vector3& ownerPos, const Radian& ownerFacing, uint32 slotIndex, uint32 slotCount)` — slot 0 right side at 2 yd, then left, then behind; swarms spread on an arc behind the owner (2.5 yd radius).
- `uint64 ChoosePetTarget(uint64 commandedTarget, uint64 ownerVictim, const std::vector<uint64>& ownerAttackers, const std::vector<uint64>& selfAttackers, pet_strategy::Type)` — Assist: commanded, then owner victim, then first owner attacker, then first self attacker; Passive: commanded only.
- Teleport to the owner when > 100 yd away or on another map (persistent pets resummon instead on map change, Task 7).
- Speed: pet run speed = max(own, owner's).

- [ ] **Step 1: Failing tests**: `PetFollowSlot` slot 0 is to the owner's right at 2 yd; 5 slots are pairwise ≥ 1 yd apart. `ChoosePetTarget` priority order for Assist; Passive ignores everything but the command. World test: owner attacks a creature → pet's victim becomes it within one AI update; creature attacks owner → pet engages it; pet engaged → owner `IsInCombat()`; Stay command → pet doesn't move when owner walks 10 yd; owner leaves combat → pet returns to follow without evade-heal.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — PASS; full suite + `bin/Debug/game_server_tests "[ai]"` green.
- [ ] **Step 5: Commit** `feat(pets): pet AI with follow, assist and passive strategies`.

### Task 6: Kill rewards for pets

**Files:**
- Modify: `game_creature_s.cpp` (`fightsForPlayer` follows `Owner` as well as `m_summonOwner`), `creature_ai_death_state.cpp` (a pet's kill tags/credits its top owner; a pet dying grants nothing — keep the existing `GrantsKillRewards` check)
- Test: extend `src/tests/game_server_tests/kill_reward_distance_test.cpp` or new `pet_kill_reward_test.cpp`

- [ ] **Step 1: Failing tests**: creature killed only by a player's pet → player gets XP and quest kill credit and may loot; killing a pet (temporary or persistent) gives no XP/loot; a creature killed by an NPC's pet gives nobody rewards.
- [ ] **Step 2–4:** run (FAIL) → implement → run (PASS).
- [ ] **Step 5: Commit** `feat(pets): pet kills credit the owner, pets grant no rewards`.

### Task 7: Persistent pets on the world server

**Files:**
- Create: `src/shared/game_server/pets/persistent_pet.h/.cpp` (data + serialization), `src/shared/game_server/pets/persistent_pet_spawner.h/.cpp` (spawn/dismiss/level/xp)
- Modify: `game_player_s.h/.cpp` (stored pets list; `ChangeClass` hook dismisses; death keeps `wasSummoned`; resurrect resummons; `RewardExperience` feeds the summoned `OwnLevel` pet), `spell_effects.cpp` (`HandleSummonPersistentPet`, `HandleGrantPersistentPet`, `HandleDismissPet`, `HandleRevivePet`, `HandleTameCreature`, `HandleLearnPetSpell`; register 58–61 in `single_cast_state.cpp:1120`), `src/shared/game/guid_utils.h` (use `GuidType::Pet`)
- Test: `src/tests/game_server_tests/persistent_pet_test.cpp`

**Interfaces:**
- `struct PersistentPetData { uint64 dbId; uint32 classId; uint32 petEntry; uint32 unitEntry; String name; uint32 level; uint32 xp; uint32 health; uint32 power; bool isDead; bool wasSummoned; std::vector<uint32> spells; };` with `io::Writer& operator<<` / `io::Reader& operator>>` (format: fields in declaration order, name as uint8-length string, spells as uint16 count + uint32 each).
- `uint64 MakePetGuid(uint64 dbId, uint32 petEntry)` → `CreateEntryGUID(dbId, petEntry, GuidType::Pet)` (Review Focus 2).
- On `GamePlayerS`: `const std::vector<PersistentPetData>& GetStoredPets() const`, `void SetStoredPets(std::vector<PersistentPetData>)`, `PersistentPetData* FindStoredPet(uint32 petEntry)`, `pet_result::Type SummonStoredPet(uint32 petEntry /*0 = last summoned*/)`, `void DismissPersistentPet(bool keepWasSummoned)`, `void AddPetExperience(uint32 xp)`, `signal<void()> storedPetsChanged` (Task 8 sends them to the realm).
- New stored pets are created by the realm, so the guid is stable from the start: grant/tame sends `world_realm_packet::CreatePet { characterGuid, PersistentPetData (dbId 0) }`; the realm inserts the row and answers `realm_world_packet::PetCreated { characterGuid, PersistentPetData (with dbId) }` (Task 8); only then is the pet added to the stored list (and, for tame, summoned). The tamed creature despawns immediately. If the owner logged out meanwhile, the realm keeps the row and the pet appears on next login. Until Task 8 lands, tests inject `PetCreated` directly via `GamePlayerS::OnPetCreated(PersistentPetData)` and the world-side send goes through a `signal<void(const PersistentPetData&)> petCreationRequested`.
- Level: `MasterLevel` → the granting class's level (`pet_level_source::ClassLevel`) or character level; `OwnLevel` → stored level, capped; on level up `SetLevel`, learn `level_spells` ≤ level, `PetXp` notify (Task 9). Stats from the creature formula times entry multipliers.
- Tame: target creature alive, no owner, entry has `tame_pet_entry`, level ≤ caster level, stored count < `MaxStoredPets` → despawn creature, add stored pet (`unitEntry` = tamed creature's entry, level = creature level), summon it.
- Class lock: `SummonStoredPet` returns `WrongClass` when `classId != active class`; `ChangeClass` dismisses the persistent pet and despawns temporary pets.

- [ ] **Step 1: Failing tests**: `PersistentPetData` round-trip; `MakePetGuid` differs for different dbIds; summon → pet in world with `Owner`, stored name, level; dismiss stores health/power; owner death → pet gone, `wasSummoned` still true, resurrect → back; class switch → dismissed and `SummonStoredPet` = `WrongClass`; `OwnLevel` pet levels from `AddPetExperience`, stops at the cap, learns a level spell; `MasterLevel` pet follows the class level when it rises; tame success/failure cases (not tamable, too high level, no free slot); owner logout while the pet fights saves state (Review Focus 4).
- [ ] **Step 2–4:** run (FAIL) → implement → run (PASS).
- [ ] **Step 5: Commit** `feat(pets): persistent pets, taming and pet experience on the world server`.

### Task 8: Realm persistence (PetModule, migration, realm↔world)

**Files:**
- Create: `data/realm/updates/20261101_1_character_pets.sql`, `src/realm_server/modules/pet_module.h/.cpp`
- Modify: `data/realm/realm_db_schema_full.sql` (tables + history INSERT), `src/realm_server/database.h` + `mysql_database.cpp` (`LoadCharacterPets(characterId)`, `SaveCharacterPets(characterId, pets)`, `CreateCharacterPet(characterId, PersistentPetData) -> uint64`, `DeleteCharacterPets(characterId)`), `src/realm_server/program.cpp` (register module), `world.cpp` (handlers), `src/shared/auth_protocol/auth_protocol.h` (`realm_world_packet::PlayerPetData`, `PetCreated`; `world_realm_packet::PetData`, `CreatePet`; bump `auth::ProtocolVersion` to 0x0D), `src/world_server/realm_connector.cpp` (send `PetData` with every `SendCharacterData`, and on `storedPetsChanged`), `src/shared/game/subsystem.h` (`Pets`)
- Test: `src/tests/auth_protocol_tests/pet_data_packet_test.cpp`, `src/tests/realm_server_tests/pet_module_test.cpp` (module logic with a fake `ICharacterDatabase` if the other module tests do that; otherwise packet tests only)

**SQL:**
```sql
CREATE TABLE IF NOT EXISTS `character_pets` (
	`id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
	`character_id` BIGINT UNSIGNED NOT NULL,
	`class_id` INT UNSIGNED NOT NULL,
	`pet_entry` INT UNSIGNED NOT NULL,
	`unit_entry` INT UNSIGNED NOT NULL DEFAULT 0,
	`name` VARCHAR(32) NOT NULL,
	`level` INT UNSIGNED NOT NULL DEFAULT 1,
	`xp` INT UNSIGNED NOT NULL DEFAULT 0,
	`health` INT UNSIGNED NOT NULL DEFAULT 0,
	`power` INT UNSIGNED NOT NULL DEFAULT 0,
	`is_dead` TINYINT(1) NOT NULL DEFAULT 0,
	`was_summoned` TINYINT(1) NOT NULL DEFAULT 0,
	`created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
	PRIMARY KEY (`id`),
	KEY `character_id` (`character_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_pet_spells` (
	`pet_id` BIGINT UNSIGNED NOT NULL,
	`spell_id` INT UNSIGNED NOT NULL,
	PRIMARY KEY (`pet_id`, `spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```
Check the column types against the `characters` table in the full schema (character id type) and match them.

- Saves are keyed by character (DB pool key = character id): update rows by `id` (never delete-and-reinsert the pet rows, ids are guids), delete/reinsert `character_pet_spells` per pet. Removing a stored pet (GM) deletes its rows. Character deletion: `OnCharacterDeleting` deletes them.
- Join: realm loads pets after `CharacterEnterWorld` and sends `PlayerPetData` right after `PlayerCharacterJoin`; world applies it before the player spawns its pet (`wasSummoned`).
- Subsystem off: realm still loads/saves (data hooks always run, per `RealmModule` contract); world refuses pet spells with `pet_result::Disabled` (world-owned subsystem via `SetSubsystemEnabled` — pick whichever side the other world-relevant subsystems use).

- [ ] **Step 1: Failing tests**: `PlayerPetData`/`PetData` packets round-trip a list of two pets with spells; `CreatePet`/`PetCreated` round-trip.
- [ ] **Step 2–4:** run (FAIL) → implement → run (PASS). Bump `auth::ProtocolVersion`, `python tools/protocol_version_check.py --update`.
- [ ] **Step 5: Manual check** (dev stack with MySQL): start realm → migration applies; `history` has `20261101_1_character_pets`.
- [ ] **Step 6: Commit** `feat(pets): realm pet module and persistence`.

### Task 9: Game protocol and world handlers

**Files:**
- Modify: `src/shared/game_protocol/game_protocol.h` (append to `client_realm_packet` before `Count_`: `PetCommand`, `PetCastSpell`, `PetRename`, `PetNameQuery`, `CheatPetAdd`, `CheatPetRemove`, `CheatPetTame`, `CheatPetMake`; append to `realm_client_packet`: `PetSpells`, `PetLearnedSpell`, `PetXp`, `PetNameQueryResult`, `PetResult`; doc-comment each payload; bump `game::ProtocolVersion` to 0x23), realm `player.cpp` proxy registration (cheats under `#if MMO_WITH_DEV_COMMANDS`, GM level checked like `OnProxyPacket`), `src/world_server/player.cpp` dispatch + new `src/world_server/player_pet_handlers.cpp`, `docs/protocol_versions.md`
- Test: `src/tests/game_protocol_tests/pet_packets_test.cpp`, `src/tests/game_server_tests/pet_rename_test.cpp`

**Payloads:**
- `PetCommand`: uint8 command, uint64 target, uint32 param (strategy for SetStrategy).
- `PetCastSpell`: uint32 spellId, `SpellTargetMap` (same as `CastSpell`).
- `PetRename`: uint8-length string.
- `PetNameQuery`: uint64 guid → `PetNameQueryResult`: uint64 guid, uint8 found, string name.
- `PetSpells`: uint64 petGuid (0 = no persistent pet; client clears), uint32 petEntry, string name, uint8 levelMode, uint8 strategy, uint8 canRename, uint8 count, uint32 spellIds…. Temporary pets need no packet: the client shows a command-only pet bar while it sees units whose `Owner` is the active player (Task 10).
- `PetLearnedSpell`: uint32 spellId. `PetXp`: uint32 xp, uint32 nextLevelXp. `PetResult`: uint8 pet_result.
- Rename validation: `IsValidPetName`, entry `allow_player_naming`; refused → `PetResult` `InvalidName`/`NamingNotAllowed`, nothing changes (Review Focus 3). Valid → update stored name, re-send `PetSpells`, saved with the next pet save.
- `PetCastSpell` casts through the pet's `CastSpell` (pet must know the spell; owner's target rules).

- [ ] **Step 1: Failing tests**: packet round-trips; rename: `"Shade"` accepted, `""`/13 chars/`"Sh4de"` and naming-forbidden entry refused with unchanged name.
- [ ] **Step 2–4:** run (FAIL) → implement → run (PASS). Run `python tools/protocol_version_check.py --update`; add a `docs/protocol_versions.md` entry for game 0x23 and auth 0x0D.
- [ ] **Step 5: Commit** `feat(pets): pet game protocol and world handlers`.

### Task 10: Client core (friendliness, pet state, Lua API)

**Files:**
- Modify: `src/shared/game_client/game_unit_c.cpp` (`IsFriendlyTo`/`IsHostileTo` resolve owners via `object_fields::Owner` and `ObjectMgr`, same rule as Task 1, depth 4), `src/mmo_client/.../party_unit_handle.cpp:162,168`, `src/bot_core/bot_unit.cpp:152/221`
- Create: `src/mmo_client/systems/pet_info.h/.cpp` (holds `PetSpells` state, XP, strategy; sends `PetCommand`/`PetCastSpell`/`PetRename`; caches pet names from `PetNameQueryResult`)
- Modify: `src/shared/game_client/object_mgr.cpp:171` and `src/mmo_client/game_script.cpp:151` (`"pet"` token), `game_script.cpp` (Lua: `HasPetUI()`, `GetPetSpell(index)`, `GetNumPetSpells()`, `PetCastSpell(index)`, `PetAttack()`, `PetFollow()`, `PetStay()`, `PetSetStrategy(n)`, `GetPetStrategy()`, `PetDismiss()`, `PetRename(name)`, `CanRenamePet()`, `GetPetXp()` → xp, next, `HasTemporaryPets()`), `world_state.cpp` (register handlers; events `PET_CHANGED`, `PET_SPELLS_CHANGED`, `PET_XP_UPDATE`, `PET_RESULT` (localized error text key), `UNIT_HEALTH_UPDATED`/`UNIT_POWER_UPDATED`/`UNIT_LEVEL_UPDATED`/`UNIT_NAME_UPDATED` with `"pet"`), `src/mmo_client/net/realm_connector.*` (send functions), creature name display uses the pet name for `GuidType::Pet` guids (query on first sight)
- Test: `src/tests/game_client_tests/pet_friendliness_test.cpp`

- [ ] **Step 1: Failing test**: a `GameUnitC` with a hostile faction and `Owner` = active player → `IsFriendly()` true, `IsHostile()` false; a unit with a friendly faction whose `Owner` is a hostile creature → `IsHostile()` true; a 5-deep or looping owner chain terminates.
- [ ] **Step 2–4:** run (FAIL) → implement → run (PASS); client builds.
- [ ] **Step 5: Commit** `feat(pets): client pet state, owner-aware reactions and Lua API`.

### Task 11: Client UI (data/client submodule)

**Files (in `data/client/Interface/GameUI/`):**
- Create: `PetFrame.lua/.xml`, `PetActionBar.lua/.xml`
- Modify: `GameUI.toc` (after `PlayerFrame.xml`), `SpellBook.lua/.xml` (tab 4 "Pet": `SPELLBOOK_TAB_PET = 4`, `SPELLBOOK_MAX_TABS = 4`, `SpellBookTab4`, hidden unless `HasPetUI()`; lists `GetPetSpell(i)`; click casts via `PetCastSpell`), `StaticDialog.lua` (`RENAME_PET` with `hasEditBox = true`), `ActionBar.lua` (pet bar anchoring vs. `ActionBar2` re-anchoring at lua:57-86)
- Modify: `data/client/Locales/Locale_{deDE,enUS,frFR,ruRU}/Localization.txt` (keys: `PET`, `PET_ATTACK`, `PET_FOLLOW`, `PET_STAY`, `PET_ASSIST`, `PET_PASSIVE`, `PET_DISMISS`, `PET_RENAME`, `PET_RENAME_PROMPT`, `PET_TAB`, and one key per `pet_result` e.g. `PET_ERR_TOO_MANY_PETS` … `PET_ERR_DISABLED`)

- Pet frame: portrait, name, level, health, power, XP bar only for `OwnLevel`; right-click menu Rename (when `CanRenamePet()`) / Dismiss. Pet bar: Attack, Follow, Stay, Assist/Passive toggle (checked state), Dismiss, then up to 6 ability buttons with cooldowns; shown when `HasPetUI()` or `HasTemporaryPets()` (abilities hidden in the latter case). `PET_RESULT` shows the localized error in the UI error frame.

- [ ] **Step 1:** Implement; run the client against a dev stack (or the visual-verification flow from memory `client-visual-verification`), summon the shadow pet via GM: frame, bar and Repertoire tab show; rename works; switching locale shows translated strings.
- [ ] **Step 2:** Screenshot for the user.
- [ ] **Step 3: Commit** in `data/client`, then bump the gitlink in the main repo: `feat(ui): pet frame, pet bar and Repertoire pet tab`.

### Task 12: GM commands

**Files:**
- Modify: `src/mmo_client/game_states/world_state.cpp:2223` + `world_state_commands.cpp` (console commands `petadd <entry>`, `petremove <entry>`, `pettame`, `petmake`), `src/mmo_client/net/realm_connector.*` (send `CheatPet*`), `src/world_server/player_dev_handlers.cpp` (handlers), `src/e2e_client/scenario_engine.cpp:1879` (`GM_PetAdd`, `GM_PetRemove`, `GM_PetTame`, `GM_PetMake`)
- Test: covered by Task 14's E2E scenario; unit test for `petmake` in `pet_manager_test.cpp`: `"making a hostile creature a pet makes it friendly and following"`.

- `petadd`: target player (self when none), grants a stored pet of that entry (class = target's active class). `petremove`: removes the stored pet of that entry (dismisses if summoned). `pettame`: tames the targeted creature ignoring level/tamable checks (still needs a free slot and an existing `PetEntry` — use the creature's `tame_pet_entry`, else refuse with `NotTamable`). `petmake`: targeted creature becomes the GM's temporary pet without duration, group `0xFFFFFFFF`, cap unlimited.

- [ ] **Steps:** failing unit test → implement → PASS → commit `feat(pets): GM pet commands`.

### Task 13: Content (data/editor submodule, Python authoring script)

**Files:**
- Create: `tools/pets/author_pet_content.py` (same pattern as `tools/racial_abilities/author_human_racials.py`: validate by default, `--apply` writes editor data and the ClientDB copy; idempotent ids)

Content (pick free ids by scanning the data; record them at the top of the script):
- Creature "Risen Skeleton" (undead, melee, stat-based, hostile faction by default), creature "Gravecaller" necromancer NPC with "Raise Skeletons" in `creaturespells` (`SummonPet`: 3 skeletons, `pet_max_count` 5, `ReplaceOldest`, `pet_level_source` CharacterLevel (= NPC level), no duration). Spawn one Gravecaller in an existing undead area for testing.
- Acolyte shadow pet: creature "Umbral Wisp"-style original shadow creature, `PetEntry` (`MasterLevel`, `ClassLevel`, `RandomFromList` with ~8 original names, `allow_player_naming` true, 2 default abilities e.g. a shadow bolt and a short-duration weaken debuff, 1 level spell), spells: "Bind Shadow" (`GrantPersistentPet`, learned from the acolyte trainer), "Summon Shadow" (`SummonPersistentPet` with `miscvaluea` = entry), "Dismiss Shadow" (`DismissPet`), one trainer `LearnPetSpell` ability. Names and visuals original (reuse existing acolyte spell visuals).
- Taming test kit: a "Tame Beast" spell (`TameCreature`, channel ~5 s), "Call Pet" (`SummonPersistentPet`, `miscvaluea` 0), "Revive Pet" (`RevivePet`), a "Wolf" `PetEntry` (`OwnLevel`, `CharacterLevel`, fixed name "Wolf", naming allowed), and `tame_pet_entry` set on one existing wolf creature. No class gets these spells.
- Localize spell/creature names in all four locales.

- [ ] **Step 1:** `py tools/pets/author_pet_content.py` validates; `--apply` writes; run twice → no diff on the second run.
- [ ] **Step 2:** Load `mmo_edit`: entries visible and editable.
- [ ] **Step 3: Commit** in `data/editor` and `data/client` (ClientDB), bump gitlinks, commit the script: `feat(pets): necromancer swarm, acolyte shadow pet and taming test content`.

### Task 14: E2E scenarios, docs, gate

**Files:**
- Create: `e2e/scenarios/pet_temporary_swarm.lua` (GM spawns a Gravecaller, player attacks it → skeletons spawn, are hostile to the player, despawn when the Gravecaller dies; player casts a `SummonPet` test spell → its pets are friendly and follow), `e2e/scenarios/pet_persistent.lua` (acolyte learns Bind Shadow, summons, pet follows and assists against a target, dismiss → resummon keeps name/level, rename, relog → pet comes back, class switch → pet dismissed and summon fails), `e2e/scenarios/gm_pet_commands.lua` (`petadd`, `petremove`, `pettame`, `petmake`)
- Create: `docs/pets.md` (how to author temporary/persistent pets: effects, fields, policies, level sources, GM commands)
- Modify: `CLAUDE.md` "Implemented Gameplay Features" (add **Pet system**)

- [ ] **Step 1:** `powershell -File tools/e2e/e2e_run.ps1` — the three new scenarios pass, nothing else regresses.
- [ ] **Step 2:** `python tools/shutdown_check.py` passes (no new timers without `Stop()`).
- [ ] **Step 3:** `/gate full` (pet system touches wire, DB and AI: risky) — green incl. Linux WSL step.
- [ ] **Step 4: Commit** `test(pets): e2e scenarios and pet authoring docs`.
