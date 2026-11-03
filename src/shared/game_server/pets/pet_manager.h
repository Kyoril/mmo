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
	class GamePlayerS;

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

		/// Gives a command to every pet of the owner (the temporary pets and the persistent pet).
		/// Dismiss despawns the temporary pets; dismissing the persistent pet (storing it) is done by
		/// DismissPersistentPet. Everything else is executed by each pet's CreatureAI::OnPetCommand.
		/// @param command The command.
		/// @param targetGuid The target of the command (Attack), or 0.
		/// @param param A command parameter (SetStrategy: the pet_strategy), or 0.
		void Command(pet_command::Type command, uint64 targetGuid, uint32 param);

		/// Called when the owner starts attacking a unit (auto attack, or a harmful spell cast at it).
		/// Assisting pets join in.
		/// @param victim The unit the owner attacks.
		void OnOwnerAttack(GameUnitS& victim);

		/// @return The unit the owner attacked last (auto attack or harmful spell) while it is alive
		///         and in the owner's world, otherwise nullptr. Remembered even while the owner has no
		///         pets, so pets summoned into a fight know what their owner fights.
		[[nodiscard]] GameUnitS* GetOwnerTarget() const;

		/// Called when a unit starts attacking the owner. Assisting pets defend their owner.
		/// @param attackerGuid Guid of the attacking unit.
		void OnOwnerAttacked(uint64 attackerGuid);

		/// @return Whether a unit is one of the owner's pets (temporary or the persistent pet). Other
		///         units with an Owner field (Summon effect adds) are not.
		/// @param unit The unit to check.
		[[nodiscard]] bool HasPet(const GameUnitS& unit) const;

		/// @return Whether any pet of the owner (still in the world) is in combat.
		[[nodiscard]] bool IsAnyPetInCombat();

		/// Determines the follow slot of a pet: its position among the owner's pets (ForEachPet
		/// order) and the number of pets.
		/// @param pet The pet.
		/// @param slotIndex Receives the pet's index, 0 if it is not one of the owner's pets.
		/// @param slotCount Receives the number of pets, at least 1.
		void GetPetSlot(const GameCreatureS& pet, uint32& slotIndex, uint32& slotCount);

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

		/// Dismisses the summoned persistent pet (Dismiss command). Storing it with its health and
		/// power belongs to the persistent pet lifetime and is not wired up yet: until then this does
		/// nothing, so a persistent pet stays summoned.
		void DismissPersistentPet();

		/// Despawns a pet on the next timer tick.
		static void Despawn(GameCreatureS& pet);

	private:
		GameUnitS& m_owner;
		/// Temporary pets in the order they were added.
		std::vector<std::unique_ptr<TemporaryPet>> m_temporaryPets;
		std::weak_ptr<GameCreatureS> m_persistentPet;
		scoped_connection m_persistentPetDespawned;
		/// The unit the owner attacked last.
		std::weak_ptr<GameUnitS> m_ownerTarget;
	};

	/// How long the corpse of a pet stays in the world before it despawns, in milliseconds.
	constexpr uint32 PetCorpseDespawnDelay = 2000;

	/// Makes the corpse of a killed pet despawn after PetCorpseDespawnDelay, instead of decaying like
	/// a creature's corpse. Safe to call from inside the pet's killed signal.
	/// @param pet The killed pet.
	void DespawnPetCorpse(GameCreatureS& pet);

	/// Tells the owner of a pet that a unit started attacking the pet. A creature owner that is not
	/// fighting yet joins in (pet combat is the owner's combat); a player owner is put in combat by
	/// the pet's own combat flag instead.
	/// @param pet The attacked pet (a unit with an owner).
	/// @param attackerGuid Guid of the attacking unit.
	void NotifyPetOwnerOfAttacker(GameUnitS& pet, uint64 attackerGuid);

	/// The pet group (and group cap) of the temporary pets GMs make with MakeGmPet.
	constexpr uint32 GmPetGroup = 0xFFFFFFFFu;

	/// GM command: makes an existing creature a temporary pet of a unit, without a duration and
	/// without a group cap. The creature keeps its place in the world and its spawner (a creature of
	/// a spawner stays a pet until it dies and its corpse despawns, or it is removed; the spawner then
	/// respawns the original after its normal respawn delay). It becomes friendly to the owner and
	/// follows it (CreatureAI::EnterPetMode).
	/// @param owner The new owner.
	/// @param target The creature to make a pet.
	/// @return Ok, or NotTamable for a dead creature or a creature that already is somebody's pet.
	pet_result::Type MakeGmPet(GameUnitS& owner, GameCreatureS& target);

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

	/// Checks the effects of a spell that target the caster's pet (effect target Pet) before the
	/// cast starts: they need a summoned, living persistent pet.
	/// @param spell The spell to cast.
	/// @param caster The caster.
	/// @return FailedNoPet, FailedPetDead, or CastOkay.
	SpellCastResult CheckPetTargetEffects(const proto::SpellEntry& spell, const GameUnitS& caster);

	/// Maps the result of a persistent pet action to the spell cast result its spell fails with
	/// before the cast starts. Ok and Disabled pass (CastOkay): with the PETS subsystem off the
	/// cast goes through and its effect reports Disabled to the client with a PetResult.
	/// @param result The pet action result.
	/// @return The spell cast result.
	SpellCastResult PetResultToCastResult(pet_result::Type result);

	/// Checks the caster-side persistent pet effects of a spell before the cast starts:
	/// SummonPersistentPet, GrantPersistentPet and RevivePet that would fail (GamePlayerS's
	/// CanSummonStoredPet, CanGrantPersistentPet and CanRevivePet) fail the cast instead of
	/// spending it.
	/// @param spell The spell to cast.
	/// @param caster The casting player.
	/// @return CastOkay or the result the cast fails with (PetResultToCastResult).
	SpellCastResult CheckPersistentPetEffects(const proto::SpellEntry& spell, const GamePlayerS& caster);

	/// Checks the TameCreature effects of a spell against its unit target before the cast starts
	/// and again when it finishes: a target that cannot be tamed (GamePlayerS::CanTameCreature;
	/// anything but a creature is NotTamable) fails the cast instead of spending it.
	/// @param spell The spell to cast.
	/// @param caster The casting player.
	/// @param target The spell's unit target.
	/// @return CastOkay or the result the cast fails with (PetResultToCastResult).
	SpellCastResult CheckTameTarget(const proto::SpellEntry& spell, const GamePlayerS& caster, const GameUnitS& target);
}
