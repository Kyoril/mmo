# Pet System Design

Date: 2026-11-01
Status: draft, awaiting approval

## Intent

Units (players and NPCs) can own other units — *pets* — that follow their owner and fight for
it. The first content that proves the system out is a **necromancer NPC raising a skeleton
swarm** and an **acolyte shadow pet** that the acolyte learns and summons and that behaves like a
warlock's demon in a well-known MMO: persistent, named, with its own abilities, a pet bar and a pet
unit frame. The system behind it is the whole thing: temporary swarms, persistent pets with or
without their own experience bar, taming, and GM tooling. We take the feature set as reference,
not the content: no imps, no copied pet rosters.

## Goals

1. **Ownership.** Any unit can own any number of pets. Owned units are friendly to their owner and
   to everyone friendly with the owner, and hostile to whoever is hostile to the owner — regardless
   of their own faction. A skeleton a player raises is friendly to that player even if skeleton
   creatures are normally hostile.
2. **Temporary pets.** Nameless, summoned by a spell effect, from a creature entry. With or without
   a duration; without one they live exactly as long as the owner is alive and in the world. They
   never grant kill rewards. NPCs and players both use them.
3. **Pet groups and limits.** Every pet summon belongs to a group with a cap and a policy, so
   "at most 5 skeletons, the 6th replaces the oldest" and "only one, recast fails while it is out"
   are both expressible, and a unit can hold several groups at once (a swarm *and* a big minion).
   A proc that summons one pet builds an army up to its cap.
4. **Persistent pets.** Defined as data (`PetEntry`), stored per character, named, summoned by a
   spell, dismissed, resummoned. Either with their own level and experience bar (capped at the
   master's class level or character level, per entry) or always at the master's class or
   character level (per entry). They have their own ability list, can be taught new abilities,
   and learn abilities automatically at levels.
5. **Getting a persistent pet.** A "grant pet" spell effect (warlock-style: learning the spell gives
   you the pet) and a "tame" spell effect (turns a supported target creature into a stored pet and
   summons it).
6. **Pet AI with strategies.** Pets use their abilities automatically, the way creature AI does.
   The default *Assist* strategy and a *Passive* strategy (see Behavior). The owner can command
   Attack, Follow and Stay, and order a persistent pet to cast a specific ability.
7. **UI.** A pet unit frame and a pet action bar for the summoned persistent pet; a Pet tab in the
   Repertoire window listing the summoned persistent pet's abilities; a rename dialog for pets
   whose entry allows player naming. Temporary pets get no unit frame.
8. **GM commands.** Add/remove a persistent pet, tame the targeted creature instantly, and make
   any targeted NPC the GM's temporary pet.

## Non-goals (this round)

- Per-ability auto-cast toggles. Pets use their abilities "smartly" on their own for now.
- More than one *summoned persistent* pet per player in the UI. The server keeps a list and a cap
  per class (default 1), so lifting it later is UI work only. Temporary pets are unaffected.
- A stable master NPC, pet happiness/loyalty/food, pet talents.
- Pets for players' parties showing in party frames.
- A Repertoire Pet tab per stored pet; it shows only the summoned persistent pet.

## Anti-goals

- Recreating the reference game's content (names, pets, spell kits).
- A second creature editor. Pets reference creature entries for model, stats and stat scaling.
- Summoned pets that are a kill-reward or loot farm.
- Stray armies: an NPC's pets must never outlive its fight or its life.

## Constraints from the code base

- `object_fields::Owner` (`src/shared/game/object_type_id.h:172`, 64-bit, on every object) exists
  and is unused on the server. It becomes the replicated owner guid. No new unit field is needed,
  so `PlayerFields` indices do not shift. The client nameplate filter already reads it
  (`src/mmo_client/ui/nameplate_manager.cpp:169`).
- `GameCreatureS::m_summonOwner` (server-only weak pointer) already drives kill-reward attribution
  (`fightsForPlayer`). It stays and is set alongside `Owner`.
- `spell_effects` (`src/shared/game/spell.h`) is append-only. `SummonPet = 43`, `TameCreature = 42`
  and `LearnPetSpell = 44` already exist as stubs or unused; new effects append after
  `TeachEmote = 57`.
- New opcodes and the realm↔world pet data transfer change the wire: bump
  `game::ProtocolVersion` and `auth::ProtocolVersion`, run
  `python tools/protocol_version_check.py --update`, record in `docs/protocol_versions.md`.
- Realm DB changes are additive migrations in `data/realm/updates/` (plus the history INSERT in the
  full schema). Realm-side features live in a `RealmModule` with a `game::Subsystem`.
- Realm and world servers are single-threaded io; DB work keyed by character.
- UI strings are localized in all four locales (deDE, enUS, frFR, ruRU).
- `data/client` (and the other data submodules) are empty in this worktree; they must be
  initialized before content/UI work, and their commits land via the submodule rules.

## The design

### 1. Ownership and friendliness

- Every pet (temporary or persistent) has `Owner` set to its owner's guid and `m_summonOwner` set.
- Both servers' and the client's friendliness checks resolve owners first:
  - Server: `GameUnitS::UnitIsEnemy` / `UnitIsFriendly` (`game_unit_s.cpp:3502/3544`).
  - Client: `GameUnitC::IsFriendlyTo` / `IsHostileTo` (`game_unit_c.cpp:2763/2790`),
    `PartyUnitHandle` overrides, and the bot copy in `bot_core/bot_unit.cpp`.
- Rule: walk each side up its owner chain to the top owner (depth-limited, like
  `fightsForPlayer`). A unit is always friendly with its own owner chain and with other pets of
  the same top owner. Otherwise the comparison uses the top owners' factions: a pet "acts as if it
  had the owner's faction".
- The existing `Summon` effect (boss adds) also sets `Owner`, so a player casting it gets friendly
  units. Its other behavior (no follow, no pet AI, no despawn with owner) is unchanged.

### 2. Pet data (`pets.proto`, new `PetEntry` manager)

Registered in `src/shared/proto_data/project.h` as an *optional* manager, with a Pets editor
window in `mmo_edit` next to the creature editor. Fields:

| Field | Meaning |
|---|---|
| `id`, `name` (+ `name_loc`) | Editor/default name. |
| `unit_entry` | Creature entry providing model, stats (via `unitClassId` stat formulas), speeds, weapons, damage school. |
| `display_id` | Optional override of the creature's model. |
| `name_mode` | `Fixed` (use `name`), `RandomFromList` (pick from `random_names`, localized). |
| `random_names` | Name list for `RandomFromList`. |
| `allow_player_naming` | Owner may rename the pet. |
| `level_mode` | `OwnLevel` (has XP bar) or `MasterLevel`. |
| `level_source` | `CharacterLevel` or `ClassLevel` of the master: the cap for `OwnLevel`, the level for `MasterLevel`. |
| `default_spells` | Abilities the pet always knows. |
| `level_spells` | `{ spell, level }` learned automatically on reaching a pet level. |
| `ai_spells` | Optional `UnitSpellEntry` list (priority, cooldowns, target, ranges) tuning how the AI uses its abilities; abilities without an entry get defaults. |
| `stat_overrides` | Optional per-level health/mana/damage/armor multipliers, for when the creature's class formula is not enough. |

Creature entries (`UnitEntry`) get one new optional field: `tame_pet_entry` — taming this creature
yields that `PetEntry`, keeping the tamed creature's own model (one "Wolf" entry covers every wolf).

Classes (`ClassEntry`) get `max_summoned_persistent_pets` (default 1).
A realm-wide cap of stored persistent pets per character (default 5) lives in the world/realm
config or combat settings — builder's call where.

### 3. Spell effects

Effect settings use existing `SpellEffect` fields where they fit and these new optional fields:
`pet_group` (0 = the spell's id), `pet_max_count`, `pet_limit_policy`, `pet_level_source`.

| Effect | Id | Behavior |
|---|---|---|
| `Summon` | 25 (existing) | Unchanged boss-add summon, plus `Owner` set (see §1). |
| `SummonPet` | 43 (existing stub) | **Temporary pets.** Creature from `summonunit`; count = basepoints (min 1); spell `duration` (0 = lives as long as the owner). `pet_level_source`: `Creature` (entry's level range), `CharacterLevel`, `ClassLevel` (both = the NPC's level for NPC casters). Group/cap/policy per the fields above. |
| `TameCreature` | 42 (existing) | Target must be a living, un-owned creature whose entry has `tame_pet_entry`, with level ≤ caster level, and the caster must have a free stored slot. Success removes the creature, stores a new persistent pet (level = creature level, capped; model = creature's) and summons it. |
| `LearnPetSpell` | 44 (existing) | Teaches `triggerspell` to the summoned persistent pet. |
| `SummonPersistentPet` | 58 (new) | Summons the caster's stored pet of `PetEntry` `miscvaluea` (0 = the most recently summoned stored pet). Dismisses the currently summoned persistent pet first if the class cap is reached. A dead stored pet is summoned at full health (summoned pets are recreated). |
| `GrantPersistentPet` | 59 (new) | Adds a stored pet of `PetEntry` `miscvaluea` if the caster doesn't have one yet. Used by a "learn" spell (warlock-style); the summon spell is separate. |
| `DismissPet` | 60 (new) | Dismisses (stores) the summoned persistent pet. Also reachable from the pet bar. |
| `RevivePet` | 61 (new) | Revives a dead stored pet (for tamed pets) at a percentage of health (basepoints). |

`pet_limit_policy`:
- `ReplaceOldest` — at the cap, the oldest pet in the group despawns and the new one spawns. A proc
  summoning into a full army therefore keeps renewing it.
- `FailAtCap` — the cast fails with a "too many pets" error before it starts.

Group counts are per owner, per group. Persistent pets don't use groups; they use the class cap.

### 4. Lifetime rules

- **Temporary pets** despawn when their duration ends, the owner dies, despawns, leaves the
  world/map/instance, logs out, or (NPC owner) resets/evades. They never grant XP, loot or
  reputation.
- **Persistent pets** are dismissed (stored, with current health and power) when the owner logs
  out, changes map, or dismisses them; on login/map change the pet that was summoned comes back.
  If the owner dies, the pet is dismissed alive. When a persistent pet dies it is stored as dead:
  summon-style pets come back via their summon spell (full health); tamed pets need `RevivePet`.
  *(Builder: a tamed pet's summon spell is a generic "Call Pet" `SummonPersistentPet` with
  `miscvaluea = 0`.)*
- A pet that gets too far from its owner (other map, > ~100 yd, or no path) teleports to the
  owner's side.

### 5. Behavior (pet AI)

A pet runs a pet-specific mode of `CreatureAI` (states can be new classes or owner-aware
branches; builder's call), replacing home/idle/reset with owner-relative behavior:

- **Following:** a slot beside the owner (offset left/right/behind, spread for swarms so 5
  skeletons don't stack), using the existing follow step (`GetMover().MoveTo`), running when far.
  Pets match the owner's speed so they keep up.
- **Assist (default):** joins whatever the owner attacks (owner's victim / owner's
  `TargetUnit` when the owner starts auto-attack or casts a harmful spell on it); defends the owner
  (anything attacking the owner); defends itself. Priority: owner's chosen target, then attackers
  of the owner, then attackers of itself. Never pulls on its own.
- **Passive:** follows; does nothing unless commanded (Attack, cast). It still doesn't take
  threat on its own.
- **Stay:** holds position (still fights per strategy within its position's reach); Follow ends it.
- **Leaving combat:** when the owner leaves combat or targets are dead/out of reach, the pet
  returns to its follow slot — no reset-to-home, no evade-heal.
- **Abilities:** chosen the way creature AI chooses `creaturespells` (priority, cooldowns, target
  kind, ranges), sourced from the pet's known spells and the entry's `ai_spells` tuning.
- **Commands** from the owner apply to the summoned persistent pet and all temporary pets of that
  owner: Attack (owner's target), Follow, Stay, set strategy. Cast-spell commands apply to the
  persistent pet only.
- NPC-owned pets use the same AI; their owner's AI combat state drives them (the owner's victim).

### 6. Persistence

- Realm DB, new additive migration:
  - `character_pets`: `id` (pet id, part of the pet's guid), `character_id`, `class_id`
    (granting class, see decision 14), `pet_entry`,
    `unit_entry` (tamed model source; 0 = entry default), `name`, `level`, `xp`, `health`,
    `power`, `is_dead`, `was_summoned`, `created_at`.
  - `character_pet_spells`: `pet_id`, `spell_id`.
- A realm `PetModule` (`src/realm_server/modules/`) owns these tables, loads them with the
  character, hands them to the world node with the character join, and saves what the world node
  reports back (separate `world_realm_packet::PetData` / realm→world counterpart, the way
  `QuestData` is separate from `CharacterData`). Saves keyed by character id (DELETE-then-INSERT by
  owner, matching the READ COMMITTED rule). Subsystem `PETS`; when off, persistent pet summon/
  grant/tame spells fail with an error and no pet data is touched.
- Pet guids use `GuidType::Pet` (`guid_utils.h:16`) with the stored pet id, so a pet keeps its
  guid across summons (needed for names and client state).

### 7. Network

New `game` opcodes (exact numbering is the builder's):

- client→server: `PetCommand` (attack / follow / stay / set strategy / dismiss),
  `PetCastSpell` (spell id + target), `PetRename` (name; validated like character names, only when
  the entry allows it), `PetNameQuery` (guid).
- server→client: `PetSpells` (sent when a persistent pet is summoned or changes: pet guid, entry,
  name, level mode, strategy, ability list; an empty one = no persistent pet),
  `PetLearnedSpell`, `PetXp` (current/next-level XP, only for `OwnLevel` pets),
  `PetNameQueryResult` (so every client can name any persistent pet in nameplates/target frames),
  plus error results for failed pet commands/casts. Temporary pets keep their creature entry's
  name.
- realm↔world: pet data on join and save (see §6).

Bumps: `game::ProtocolVersion`, `auth::ProtocolVersion`, fingerprint update,
`docs/protocol_versions.md`.

### 8. Pet experience

`OwnLevel` pets gain the same kill XP their master gets (same formula, same group split), on top
of — never instead of — the master's XP. They stop at the cap (`level_source` of the master). When
the master's cap rises, the pet resumes gaining. Leveling refreshes stats from the creature's
stat formula and learns `level_spells`.

### 9. Client UI

- **`"pet"` unit token** in both resolvers (`object_mgr.cpp:171`, `game_script.cpp:151`), resolving
  to the summoned persistent pet. `UNIT_*_UPDATED` events fire with `"pet"`; new events
  `PET_CHANGED`, `PET_SPELLS_CHANGED`, `PET_XP_UPDATE`.
- **Pet frame** (`PetFrame.lua/.xml`, in `GameUI.toc`): portrait/name/level/health/power, an XP
  bar for `OwnLevel` pets, anchored near the player frame. Right-click menu: Rename (when allowed),
  Dismiss.
- **Pet action bar**: separate client state, not new character action slots. Fixed command
  buttons Attack, Follow, Stay, Assist/Passive toggle, then the pet's active abilities filled from
  `PetSpells` (clicking casts via `PetCastSpell`, with cooldowns). Shown while a persistent pet is
  summoned, or while only temporary pets are out (commands only). Placed so it doesn't collide
  with `ActionBar2` re-anchoring.
- **Repertoire Pet tab** (`SpellBook.lua/.xml`): 4th tab, visible only with a persistent pet
  summoned; lists its abilities; dragging an ability onto the pet bar is not required this round.
- **Rename**: `StaticDialog` with an edit box (`hasEditBox`).
- Friendliness/coloring follow automatically from §1 once the client checks resolve owners
  (nameplates, name color, selection ring, cursor).
- Strings localized in all four locales.

### 10. GM commands (dev commands, `MMO_WITH_DEV_COMMANDS`, GM level checked at the realm)

- `petadd <petEntryId>` — grant a stored persistent pet to the target player (self if none).
- `petremove <petEntryId>` — remove a stored pet (dismissing it if summoned).
- `pettame` — instantly tame the targeted creature, bypassing spell requirements except "is a
  creature".
- `petmake` — make the targeted NPC the GM's temporary pet with no duration: sets `Owner`, pet AI,
  follows the GM, assists. Any creature works, tamable or not. (The `CheatFollowMe` TODO handler is
  the natural neighbour.)
- E2E `GM_*` bindings for the new commands.

### 11. Content

- **Necromancer NPC** (new creature) with a "Raise Skeletons" spell: `SummonPet`, 3 skeletons,
  group cap 5, `ReplaceOldest`, level = caster, no duration. Placed for testing; an existing
  undead area is fine.
- **Acolyte shadow pet**: a `PetEntry` (`MasterLevel` from the class level, random name list,
  player naming allowed) with 2–3 shadow abilities, a learn spell (`GrantPersistentPet`) from the
  acolyte trainer, a summon spell (`SummonPersistentPet`), and one `LearnPetSpell` trainer ability.
  Name, model and abilities are original.
- **A tame spell and one tamable creature** exist for testing, but no class gets the tame spell
  yet. *(My call: which class tames is a content decision for later.)*
- An acolyte "army" proc example (`SummonPet` with duration, cap, `ReplaceOldest`) is optional.

### 12. Verification

- Unit tests (`game_server_tests`): owner-chain friendliness, group cap/policies, temporary pet
  lifetime on owner death/despawn, no kill rewards from pets, pet XP cap.
- E2E scenario(s): summon a temporary pet and a persistent pet, pets follow, assist on attack,
  despawn on owner death; dismiss/resummon keeps name and level; GM `petmake`.
- Linux gate (servers must build with gcc).

## Decisions made on the builder's side (my calls, flagged for review)

1. `Summon` (25) stays the boss-add effect but gains `Owner`; temporary pets are `SummonPet` (43).
2. Pet data references a creature entry rather than duplicating creature fields.
3. Taming keeps the tamed creature's model; one pet entry per creature family.
4. A dead summon-style pet comes back at full health on its next summon; tamed pets need a revive.
5. Owner death dismisses a persistent pet alive; it is not killed.
6. Pet XP equals the master's kill XP, never shared away from the master.
7. Pet bar is client-side state, not stored character action slots; the layout is fixed
   (commands + abilities in order).
8. No class gets taming yet.
9. Realm-wide stored-pet cap of 5.
10. **Pet combat is the owner's combat.** A pet engaging or being attacked puts its owner in combat
    (and an NPC owner's pets put the NPC in combat). Pet damage counts as the owner's for tagging,
    XP, loot and quest credit (extends today's `fightsForPlayer`). Pets never grant rewards when
    *they* die. Assist never pulls, so an AFK owner earns nothing.
11. **Pet XP stream.** Kill XP already feeds character and class XP alike; an `OwnLevel` pet
    receives the same amount, capped by its `level_source`.
12. **Resurrection.** A persistent pet that was summoned when the owner died comes back when the
    owner is resurrected (`was_summoned` stays set through death).
13. **Hostility via top owners.** After resolving owner chains, the comparison is the normal
    unit-vs-unit check between the top owners (so whatever player-vs-player rules exist apply to
    pets too); pets of the same top owner are always friendly.
14. **Class switching (user decision).** A persistent pet belongs to the class that granted it
    (the active class when `GrantPersistentPet` / `TameCreature` ran; `character_pets.class_id`).
    `ClassLevel` always means that class's level, and that class's
    `max_summoned_persistent_pets` applies. Switching the active class dismisses its summoned
    persistent pet (stored alive) and despawns all temporary pets; a pet of another class cannot be
    summoned until the player switches back. The pet bar, frame and Repertoire tab go away with it.
15. **Saving.** Pet state is reported to the realm with the periodic character save as well as on
    dismiss, logout and map change, so a crash loses no more pet progress than character progress.
