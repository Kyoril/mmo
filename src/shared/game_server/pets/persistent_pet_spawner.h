// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_server/pets/persistent_pet.h"

#include <memory>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class Project;
		class PetEntry;
		class UnitEntry;
		class UnitSpellEntry;
		class ClassEntry;
	}

	class GameCreatureS;
	class GamePlayerS;
	class GameUnitS;
	class WorldInstance;

	/// @return The creature entry a stored pet is made of: the creature it was tamed from, or the
	///	        pet entry's unit_entry. nullptr if that entry does not exist.
	/// @param project The project holding the creature entries.
	/// @param data The stored pet.
	/// @param entry The stored pet's pet entry.
	const proto::UnitEntry* GetPersistentPetUnitEntry(const proto::Project& project, const PersistentPetData& data, const proto::PetEntry& entry);

	/// @return The master's level that applies to a pet entry: the level of a MasterLevel pet and
	///	        the cap of an OwnLevel pet. CharacterLevel: the owner's level; ClassLevel: the owner's
	///	        active class level (a persistent pet is only summoned while its class is active).
	/// @param owner The pet's owner.
	/// @param entry The pet entry.
	uint32 GetPetMasterLevel(const GamePlayerS& owner, const proto::PetEntry& entry);

	/// @return The level a stored pet is summoned at: the master's level for MasterLevel pets,
	///	        the stored level capped by the master's level for OwnLevel pets. At least 1.
	/// @param owner The pet's owner.
	/// @param data The stored pet.
	/// @param entry The stored pet's pet entry.
	uint32 ResolvePersistentPetLevel(const GamePlayerS& owner, const PersistentPetData& data, const proto::PetEntry& entry);

	/// @return The spells a stored pet knows at a level: the entry's default spells, the spells
	///	        it learned, and the entry's level spells up to the level. No duplicates.
	/// @param entry The stored pet's pet entry.
	/// @param data The stored pet.
	/// @param level The pet's level.
	std::vector<uint32> GetKnownPetSpells(const proto::PetEntry& entry, const PersistentPetData& data, uint32 level);

	/// Builds the combat spells of a pet for its AI: the entry's ai_spells tuning for spells
	/// that have an entry there, the UnitSpellEntry defaults for all others.
	/// @param entry The pet entry.
	/// @param spells The spells the pet knows.
	std::vector<proto::UnitSpellEntry> BuildPetCombatSpells(const proto::PetEntry& entry, const std::vector<uint32>& spells);

	/// Chooses the name of a new stored pet. RandomFromList picks from random_names (falling back
	/// to the entry's name if the list is empty). Fixed uses the tamed creature's name for a tamed
	/// pet and the entry's name otherwise.
	/// The name is clamped to MaxStoredPetNameBytes (ClampStoredPetName).
	/// @param entry The pet entry.
	/// @param tamedCreatureName Name of the tamed creature, or an empty string for a granted pet.
	String ChoosePetName(const proto::PetEntry& entry, const String& tamedCreatureName);

	/// @return The experience an OwnLevel pet needs for its next level: the same curve the master's
	///	        class uses for character levels.
	/// @param classEntry The master's class, or nullptr.
	/// @param level The pet's current level.
	uint32 GetPetXpToNextLevel(const proto::ClassEntry* classEntry, uint32 level);

	/// Spawns a stored pet into its owner's world as the owner's persistent pet, with its stable
	/// guid, name, level, stats, health and power, and starts its pet AI. Must not be called during
	/// the world update.
	/// @param owner The pet's owner, in the world.
	/// @param world The owner's world.
	/// @param data The stored pet.
	/// @param entry The stored pet's pet entry.
	/// @param unitEntry The creature entry the pet is made of (GetPersistentPetUnitEntry).
	/// @param healthPercent If not 0, the pet's health in percent of its max health (a revive).
	/// @return The spawned pet.
	std::shared_ptr<GameCreatureS> SpawnPersistentPet(GamePlayerS& owner, WorldInstance& world, const PersistentPetData& data,
		const proto::PetEntry& entry, const proto::UnitEntry& unitEntry, uint32 healthPercent);

	/// Gives a persistent pet its known spells: the creature learns them and the AI uses them.
	/// @param pet The pet.
	/// @param entry The pet's pet entry.
	/// @param spells The spells the pet knows (GetKnownPetSpells).
	void ApplyPersistentPetSpells(GameCreatureS& pet, const proto::PetEntry& entry, const std::vector<uint32>& spells);

	/// Gives a persistent pet the stat multipliers of its owner's ModPetStatsPct auras and
	/// refreshes its stats. A living pet keeps its health ratio (at least 1 health).
	/// @param pet The persistent pet.
	/// @param owner The pet's owner.
	void RefreshPersistentPetOwnerStats(GameCreatureS& pet, const GameUnitS& owner);

	/// Makes a persistent pet cast a passive spell (PetPassive) on itself, unless it already has
	/// the spell's aura from itself.
	/// @param pet The persistent pet, in the world.
	/// @param spellId The passive spell.
	void ApplyPetPassiveSpell(GameCreatureS& pet, uint32 spellId);

	/// Removes the aura of a passive spell (PetPassive) the pet cast on itself.
	/// @param pet The persistent pet.
	/// @param spellId The passive spell.
	void RemovePetPassiveSpell(GameCreatureS& pet, uint32 spellId);

	/// Writes the live state of a summoned pet (health, power and, for MasterLevel pets, level)
	/// into its stored data.
	/// @param pet The summoned pet.
	/// @param entry The pet's pet entry.
	/// @param data The stored pet.
	void StorePersistentPetState(const GameCreatureS& pet, const proto::PetEntry& entry, PersistentPetData& data);
}
