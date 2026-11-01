# Pets: authoring guide

How to give creatures and players pets: temporary swarms from a spell, persistent pets that a
character keeps, taming, and the GM tools to test them. Background and design decisions:
[the pet system design](superpowers/specs/2026-11-01-pet-system-design.md).

## Two kinds of pet

| | Temporary pet | Persistent pet |
|---|---|---|
| Created by | `SummonPet` (43) from any caster, player or creature; GM `petmake` | `GrantPersistentPet` (59), `TameCreature` (42), GM `petadd` / `pettame` |
| Stored | No. Lives until its duration ends or its owner dies, despawns, logs out, changes map or class | Yes, in the realm database (`character_pets`), at most `MaxStoredPets` = 5 per character |
| How many out | Per *pet group*, capped by the spell (`pet_max_count`) | One summoned at a time (`ClassEntry.max_summoned_persistent_pets`, default 1; 0 = the class cannot summon any) |
| Client state | None (the unit has `Owner` set; that is all) | `PetSpells` packet: guid, entry, name, strategy, spells; pet frame and pet bar |
| Rewards | Never grants XP, loot or reputation | Killing a persistent pet grants nothing either |

Both kinds set the unit's `Owner` field and run the pet mode of `CreatureAI`: they follow their
owner, assist it (Assist strategy, the default) and act with their owner's side. A pet keeps its own
faction template; *owner-aware friendliness* (`src/shared/game/owner_chain.h`) makes every reaction
check use the top owner instead. So an undead skeleton raised by a player is friendly to that
player's allies and hostile to its enemies.

## Spell effects

New `SpellEffect` fields (spells.proto 29-32) used by the pet effects:

| Field | Meaning |
|---|---|
| `pet_group` | Group the summoned pets count against. 0 = the spell's own id, so every summon spell has its own cap unless spells share a group on purpose. |
| `pet_max_count` | Cap of the group. 0 = the effect's summon count. |
| `pet_limit_policy` | At the cap: `0` ReplaceOldest (the oldest pet of the group despawns, the new one spawns; a proc into a full army keeps renewing it), `1` FailAtCap (the cast fails with `FailedTooManyPets` before it starts). |
| `pet_level_source` | Level of a temporary pet: `0` Creature (the unit entry's level range), `1` CharacterLevel, `2` ClassLevel (both are the caster's level when a creature casts). |

| Effect | Id | Fields | Behavior |
|---|---|---|---|
| `TameCreature` | 42 | target: enemy unit | The target must be a living creature without owner whose unit entry has `tame_pet_entry`, at most the caster's level, and the caster needs a free stored slot (checked when the cast starts and again when it finishes). The creature leaves the world; a new stored pet (its unit entry, model and name, level capped to the master level) is created and summoned. Use a *cast*, not a channel: a channeled spell applies its effects when the channel starts. |
| `SummonPet` | 43 | `summonunit` (or `miscvaluea`), `basepoints` = count (min 1), `radius` (spread), spell `duration` (0 = no limit), the four pet fields | Spawns `count` temporary pets around the caster. The spawn is deferred to the next universe tick. With FailAtCap the whole cast fails at the cap; with ReplaceOldest the oldest pets make room. |
| `LearnPetSpell` | 44 | `triggerspell` | Teaches the spell to the summoned persistent pet and stores it on the pet (sent as `PetLearnedSpell` + `PetSpells`). |
| `SummonPersistentPet` | 58 | `miscvaluea` = `PetEntry` id, 0 = the stored pet summoned last | Summons a stored pet. The current persistent pet is dismissed first. A dead summon-style pet comes back at full health; a dead *tamed* pet fails with `FailedPetDead` and needs `RevivePet`. |
| `GrantPersistentPet` | 59 | `miscvaluea` = `PetEntry` id | Adds a stored pet of that entry if the caster has none yet (`FailedAlreadyHavePet` otherwise, `FailedNoFreePetSlot` when full). Does not summon it: the summon spell is separate. The realm creates the database row asynchronously, so the pet is summonable a moment after the cast. |
| `DismissPet` | 60 | - | Dismisses (stores, with health and power) the summoned persistent pet. The pet bar's Dismiss does the same for every pet. |
| `RevivePet` | 61 | `miscvaluea` = `PetEntry` id (0 = any dead pet), `basepoints` = health % (0 = 100) | Revives a dead stored pet and summons it at that health. |

Pet cast checks (pet effects fail the cast before it starts, with the `FailedPet*` /
`FailedTooManyPets` / `FailedNotTamable` / `FailedTargetTooHighLevel` results of
`spell_cast_result`) run in `CheckPersistentPetEffects`, `CheckTameTarget` and
`CheckSummonPetLimit` (`src/shared/game_server/pets/pet_manager.cpp`).

## PetEntry (pets.proto, manager `pets`, `pets.data`)

Persistent pets are described by a `PetEntry`. The manager is optional: a data folder without
`pets.data` still loads (with a warning).

| Field | Meaning |
|---|---|
| `id`, `name`, `name_loc` | The entry and its localized name. |
| `unit_entry` | The creature the pet spawns as (a tamed pet uses the tamed creature's entry instead). |
| `display_id` | Model override for a pet that is not tamed; 0 = the unit's model. |
| `name_mode` | `0` Fixed (the entry's name; a tamed pet takes the creature's name), `1` RandomFromList (from `random_names`). |
| `allow_player_naming` | The owner may rename it (1-12 ASCII letters, normalized to "Capitalized"). |
| `level_mode` | `0` OwnLevel (the pet gains experience from kills and has its own level, never above its master level), `1` MasterLevel (always the master level). |
| `level_source` | What "master level" means: `1` CharacterLevel or `2` ClassLevel (the owner's active class level). |
| `default_spells` | Spells every pet of the entry knows. |
| `level_spells` | `(spell, level)`: learned once the pet reaches the level. |
| `ai_spells` | `UnitSpellEntry` tuning (priority, cooldown, range, target) for the pet AI. Tuning only: the AI casts the spells the pet *knows* (default, level and learned spells), and uses an entry here only to tune one of them. |
| `health_multiplier`, `mana_multiplier`, `damage_multiplier`, `armor_multiplier` | Applied on top of the unit's stat formulas. |

Pet spells should have classmask 0, no reagents and no power cost the creature cannot pay, so its
AI and the pet bar can always cast them.

Taming: set `tame_pet_entry` on the `UnitEntry` (units.proto 88) to the `PetEntry` the creature
tames into. One "Wolf" entry can serve every wolf, since a tamed pet keeps the creature's own model
and unit entry.

## Rules a designer should know

- **Class lock.** A persistent pet belongs to the class that was active when it was granted or
  tamed. Changing class dismisses it (it stays stored) and despawns all temporary pets; while
  another class is active, summoning it fails with `FailedPetWrongClass`. Back in its class it can
  be summoned again. Class spells taught by `LearnSpell` effects follow the class like any other.
- **Lifetimes.** Logout and map changes store the summoned pet; it comes back with its owner when
  they enter a world again. If the owner dies, the pet is dismissed alive and returns when they are
  resurrected. A persistent pet that dies is stored dead.
- **Identity.** A persistent pet's guid is derived from its database id, so the same pet comes back
  with the same guid, name and level after a dismiss, a relog or a class round trip.
- **Commands** (`PetCommand`, the pet bar): Attack (the owner's target), Follow, Stay, strategy
  Assist / Passive, Dismiss. They go to every pet of the owner.
- **Subsystem `PETS`** (`subsystem PETS off`): summoning, granting, taming and reviving are refused
  with `Disabled`; the realm keeps loading and saving pets.

## GM commands

Dev builds (`MMO_WITH_DEV_COMMANDS`), GM level 2 (SeniorGm) and above, checked at the realm.
Client console commands:

| Command | Effect |
|---|---|
| `petadd <petEntry>` | Grants a stored pet of that entry to the selected player (yourself without a selection). |
| `petremove <petEntry>` | Deletes that stored pet from the selected player (dismissing it first). |
| `pettame` | Tames the selected creature for you, ignoring the level check. |
| `petmake` | Makes the selected creature your temporary pet without a duration. Any creature works. |

E2E scenarios call them as `GM.PetAdd`, `GM.PetRemove`, `GM.PetTame` and `GM.PetMake`, and read pet
state with `GetPet`, `GetPetName`, `GetOwner`, `CountUnitsOwnedBy` and friends (see
[e2e/README.md](../e2e/README.md)).

## Content and tests

- `tools/pets/author_pet_content.py` authors the first pet content and is the reference for the
  ids: Raise Skeletons (spell 300) of Ilsa Mourne <Gravecaller> (unit 121, Barrowfield) raising
  Risen Skeletons (unit 120); the acolyte's Umbral Shade (pet entry 1, unit 122) with Bind Shadow
  (301, at acolyte trainer 4), Summon Shadow (302), Dismiss Shadow (303), its spells 304-307 and
  Shadow Lore: Murkbind (308, `LearnPetSpell`); and a taming test kit (Tame Beast 309, Call Pet
  310, Revive Pet 311, pet entry 2 "Wolf", Young Forest Wolf unit 44 tamable). It validates by
  default and writes the editor data and ClientDB with `--apply`.
- E2E: `e2e/scenarios/pet_temporary_swarm.lua`, `pet_persistent.lua`, `gm_pet_commands.lua`.
- Unit tests: the `[pets]` tag in `game_server_tests`, `realm_server_tests`, `game_protocol_tests`,
  `game_client_tests` and `client_ui_tests`.

## Wire

Client to realm: `PetCommand`, `PetCastSpell`, `PetRename`, `PetNameQuery` and the four
`CheatPet*` opcodes. Realm to client: `PetSpells`, `PetLearnedSpell`, `PetXp`, `PetNameQueryResult`,
`PetResult`. Payloads and their serializers are in `src/shared/game/pet_packets.h`. Changing them
needs a protocol version bump (see CLAUDE.md).
