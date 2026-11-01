// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "game/pet_defines.h"
#include "game/spell.h"

#include <functional>
#include <memory>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class UnitEntry;
		class SpellEntry;
		class SpellEffect;
	}

	class GameUnitS;
	class GameCreatureS;

	/// Distance at which a temporary pet follows its owner until the pet AI takes over.
	constexpr float PetFollowDistance = 2.0f;

	/// Keeps track of the pets of one unit: the temporary pets it summoned (grouped and capped per
	/// summon effect) and its summoned persistent pet. Pets are held weakly; a pet that leaves the
	/// world on its own (duration over, killed and decayed, removed by a GM) drops out by itself.
	/// World thread only.
	class PetManager final : public NonCopyable
	{
	public:
		/// Creates the pet manager of a unit.
		/// @param owner The unit owning the pets. Must outlive this manager.
		explicit PetManager(GameUnitS& owner);

		~PetManager();

	public:
		/// Checks whether another temporary pet may join a group.
		/// @param group The pet group.
		/// @param maxCount The group's cap.
		/// @param policy What happens at the cap. ReplaceOldest always allows the pet.
		/// @return Ok, or TooManyPets if the group is full and the policy is FailAtCap.
		[[nodiscard]] pet_result::Type CanAddToGroup(uint32 group, uint32 maxCount, pet_limit_policy::Type policy) const;

		/// Registers a spawned creature as a temporary pet of the owner. With ReplaceOldest, the
		/// oldest pets of the group are despawned until the new pet fits under the cap.
		/// @param pet The pet, already spawned in the owner's world.
		/// @param group The pet group.
		/// @param maxCount The group's cap.
		/// @param policy What happens at the cap.
		void AddTemporaryPet(GameCreatureS& pet, uint32 group, uint32 maxCount, pet_limit_policy::Type policy);

		/// @return The number of living temporary pets in a group.
		[[nodiscard]] uint32 GetGroupCount(uint32 group) const;

		/// Despawns every temporary pet of the owner. The pets leave the list at once; they leave
		/// the world on the next timer tick, so this is safe to call from inside a world update.
		void DespawnTemporaryPets();

		/// Calls a function for every pet of the owner: the temporary pets, oldest first, then the
		/// persistent pet.
		/// @param callback The function to call.
		void ForEachPet(const std::function<void(GameCreatureS&)>& callback);

		/// Gives a command to every pet of the owner.
		/// Currently a no-op: the pet AI that executes commands is wired up separately.
		/// @param command The command.
		/// @param targetGuid The target of the command (Attack), or 0.
		/// @param param A command parameter (SetStrategy: the pet_strategy), or 0.
		void Command(pet_command::Type command, uint64 targetGuid, uint32 param);

		/// @return The summoned persistent pet, or nullptr if there is none.
		[[nodiscard]] GameCreatureS* GetPersistentPet() const;

		/// Sets the summoned persistent pet. It is forgotten when it despawns.
		/// @param pet The pet, or nullptr to forget the current one.
		void SetPersistentPet(GameCreatureS* pet);

	private:
		struct TemporaryPet
		{
			std::weak_ptr<GameCreatureS> pet;
			uint32 group = 0;
			scoped_connection despawned;
			scoped_connection killed;
		};

		/// Drops the entries of pets that are gone (despawned, dead or destroyed) from the list.
		void PurgeGone();

		/// Despawns a pet on the next timer tick.
		static void Despawn(GameCreatureS& pet);

	private:
		GameUnitS& m_owner;
		/// Temporary pets in the order they were added.
		std::vector<std::unique_ptr<TemporaryPet>> m_temporaryPets;
		std::weak_ptr<GameCreatureS> m_persistentPet;
		scoped_connection m_persistentPetDespawned;
	};

	/// Resolves the level of a pet.
	/// @param source Where the level comes from.
	/// @param owner The pet's owner.
	/// @param entry The pet's creature entry.
	/// @return Creature: a roll in the entry's level range. CharacterLevel: the owner's level.
	///	        ClassLevel: a player owner's active class level, or a creature owner's level.
	uint32 ResolvePetLevel(pet_level_source::Type source, const GameUnitS& owner, const proto::UnitEntry& entry);

	/// @return The pet group of a SummonPet effect: its pet_group, or the spell's id if that is 0.
	uint32 GetSummonPetGroup(const proto::SpellEffect& effect, uint32 spellId);

	/// @return The group cap of a SummonPet effect: its pet_max_count, or the summon count if that is 0.
	uint32 GetSummonPetMaxCount(const proto::SpellEffect& effect, uint32 summonCount);

	/// Checks the SummonPet effects of a spell against the caster's pet groups before the cast
	/// starts: an effect using FailAtCap whose group is full fails the cast.
	/// @param spell The spell to cast.
	/// @param caster The caster.
	/// @return FailedTooManyPets, or CastOkay.
	SpellCastResult CheckSummonPetLimit(const proto::SpellEntry& spell, const GameUnitS& caster);
}
