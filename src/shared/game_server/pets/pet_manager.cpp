// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_manager.h"

#include "base/utilities.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/units.pb.h"

#include <algorithm>
#include <random>

namespace mmo
{
	PetManager::PetManager(GameUnitS& owner)
		: m_owner(owner)
	{
	}

	PetManager::~PetManager() = default;

	pet_result::Type PetManager::CanAddToGroup(const uint32 group, const uint32 maxCount, const pet_limit_policy::Type policy) const
	{
		if (policy == pet_limit_policy::FailAtCap && GetGroupCount(group) >= maxCount)
		{
			return pet_result::TooManyPets;
		}

		return pet_result::Ok;
	}

	void PetManager::AddTemporaryPet(GameCreatureS& pet, const uint32 group, const uint32 maxCount, const pet_limit_policy::Type policy)
	{
		PurgeGone();

		if (policy == pet_limit_policy::ReplaceOldest)
		{
			// Make room for the new pet: the oldest pets of the group go first.
			const uint32 cap = std::max<uint32>(1, maxCount);
			while (GetGroupCount(group) >= cap)
			{
				const auto oldest = std::find_if(m_temporaryPets.begin(), m_temporaryPets.end(), [group](const std::unique_ptr<TemporaryPet>& entry)
					{
						return entry->group == group && !entry->pet.expired();
					});
				ASSERT(oldest != m_temporaryPets.end());

				const std::shared_ptr<GameCreatureS> oldestPet = (*oldest)->pet.lock();
				m_temporaryPets.erase(oldest);
				Despawn(*oldestPet);
			}
		}

		auto entry = std::make_unique<TemporaryPet>();
		entry->pet = std::static_pointer_cast<GameCreatureS>(pet.shared_from_this());
		entry->group = group;

		// The callbacks only mark the entry: disconnecting a slot from inside its own invocation
		// would destroy the running callback. Marked entries are purged on the next change.
		TemporaryPet* entryPtr = entry.get();
		entry->despawned = pet.despawned.connect([entryPtr](GameObjectS&)
			{
				entryPtr->pet.reset();
			});

		// A dead pet no longer counts, and its corpse goes away shortly.
		std::weak_ptr<GameCreatureS> weakPet = entry->pet;
		entry->killed = pet.killed.connect([entryPtr, weakPet](GameUnitS*)
			{
				entryPtr->pet.reset();
				if (const auto corpse = weakPet.lock())
				{
					DespawnPetCorpse(*corpse);
				}
			});

		m_temporaryPets.push_back(std::move(entry));
	}

	uint32 PetManager::GetGroupCount(const uint32 group) const
	{
		return static_cast<uint32>(std::count_if(m_temporaryPets.begin(), m_temporaryPets.end(), [group](const std::unique_ptr<TemporaryPet>& entry)
			{
				return entry->group == group && !entry->pet.expired();
			}));
	}

	void PetManager::DespawnTemporaryPets()
	{
		// Move the list out first: despawning must not touch the list while it is iterated.
		std::vector<std::unique_ptr<TemporaryPet>> pets;
		pets.swap(m_temporaryPets);

		for (const auto& entry : pets)
		{
			if (const auto pet = entry->pet.lock())
			{
				Despawn(*pet);
			}
		}
	}

	void PetManager::ForEachPet(const std::function<void(GameCreatureS&)>& callback)
	{
		// Copy the strong references: the callback may despawn pets and thereby change the list.
		std::vector<std::shared_ptr<GameCreatureS>> pets;
		pets.reserve(m_temporaryPets.size() + 1);
		for (const auto& entry : m_temporaryPets)
		{
			if (auto pet = entry->pet.lock())
			{
				pets.push_back(std::move(pet));
			}
		}

		if (auto persistent = m_persistentPet.lock())
		{
			pets.push_back(std::move(persistent));
		}

		for (const auto& pet : pets)
		{
			callback(*pet);
		}
	}

	void PetManager::Command(const pet_command::Type command, const uint64 targetGuid, const uint32 param)
	{
		if (command == pet_command::Dismiss)
		{
			DespawnTemporaryPets();
			DismissPersistentPet();
			return;
		}

		ForEachPet([command, targetGuid, param](GameCreatureS& pet)
			{
				if (CreatureAI* ai = pet.GetAI())
				{
					ai->OnPetCommand(command, targetGuid, param);
				}
			});
	}

	void PetManager::OnOwnerAttack(GameUnitS& victim)
	{
		m_ownerTarget = std::static_pointer_cast<GameUnitS>(victim.shared_from_this());

		if (m_temporaryPets.empty() && m_persistentPet.expired())
		{
			return;
		}

		WorldInstance* world = m_owner.GetWorldInstance();
		if (!world)
		{
			return;
		}

		// Posted: this runs inside the owner's attack and spell code, and engaging changes the pets'
		// AI states, which in turn touch other units' threat lists.
		std::weak_ptr<GameUnitS> weakOwner = std::static_pointer_cast<GameUnitS>(m_owner.shared_from_this());
		std::weak_ptr<GameUnitS> weakVictim = std::static_pointer_cast<GameUnitS>(victim.shared_from_this());
		world->GetUniverse().Post([weakOwner, weakVictim]()
			{
				const auto owner = weakOwner.lock();
				const auto victim = weakVictim.lock();
				if (!owner || !victim || !owner->FindPets() || victim->GetWorldInstance() != owner->GetWorldInstance())
				{
					return;
				}

				owner->FindPets()->ForEachPet([&victim](GameCreatureS& pet)
					{
						if (CreatureAI* ai = pet.GetAI())
						{
							ai->OnOwnerAttack(*victim);
						}
					});
			});
	}

	void PetManager::OnOwnerAttacked(const uint64 attackerGuid)
	{
		if (m_temporaryPets.empty() && m_persistentPet.expired())
		{
			return;
		}

		WorldInstance* world = m_owner.GetWorldInstance();
		if (!world)
		{
			return;
		}

		// Posted for the same reason as OnOwnerAttack: this runs inside the attacker's threat code.
		std::weak_ptr<GameUnitS> weakOwner = std::static_pointer_cast<GameUnitS>(m_owner.shared_from_this());
		world->GetUniverse().Post([weakOwner, attackerGuid]()
			{
				const auto owner = weakOwner.lock();
				WorldInstance* ownerWorld = owner ? owner->GetWorldInstance() : nullptr;
				GameUnitS* attacker = ownerWorld ? ownerWorld->FindByGuid<GameUnitS>(attackerGuid) : nullptr;
				if (!attacker || !owner->FindPets())
				{
					return;
				}

				const auto strongAttacker = std::static_pointer_cast<GameUnitS>(attacker->shared_from_this());
				owner->FindPets()->ForEachPet([&strongAttacker](GameCreatureS& pet)
					{
						if (CreatureAI* ai = pet.GetAI())
						{
							ai->OnOwnerAttacked(*strongAttacker);
						}
					});
			});
	}

	GameUnitS* PetManager::GetOwnerTarget() const
	{
		GameUnitS* target = m_ownerTarget.lock().get();
		if (!target || !target->IsAlive() || !m_owner.GetWorldInstance() || target->GetWorldInstance() != m_owner.GetWorldInstance())
		{
			return nullptr;
		}

		return target;
	}

	bool PetManager::HasPet(const GameUnitS& unit) const
	{
		for (const auto& entry : m_temporaryPets)
		{
			if (const auto pet = entry->pet.lock(); pet.get() == &unit)
			{
				return true;
			}
		}

		const auto persistent = m_persistentPet.lock();
		return persistent && persistent.get() == &unit;
	}

	bool PetManager::IsAnyPetInCombat()
	{
		// A pet that left the world (despawning) no longer keeps its owner in combat.
		bool inCombat = false;
		ForEachPet([&inCombat](GameCreatureS& pet)
			{
				inCombat = inCombat || (pet.IsAlive() && pet.IsInCombat() && pet.GetWorldInstance() != nullptr);
			});
		return inCombat;
	}

	void PetManager::GetPetSlot(const GameCreatureS& pet, uint32& slotIndex, uint32& slotCount)
	{
		slotIndex = 0;
		slotCount = 0;
		ForEachPet([&](GameCreatureS& each)
			{
				if (&each == &pet)
				{
					slotIndex = slotCount;
				}
				++slotCount;
			});
		slotCount = std::max<uint32>(1, slotCount);
	}

	void PetManager::DismissPersistentPet()
	{
		// A player's persistent pet is stored with its health and power before it leaves.
		if (auto* player = dynamic_cast<GamePlayerS*>(&m_owner))
		{
			player->DismissPersistentPet(false);
			return;
		}

		if (GameCreatureS* pet = GetPersistentPet())
		{
			SetPersistentPet(nullptr);
			Despawn(*pet);
		}
	}

	GameCreatureS* PetManager::GetPersistentPet() const
	{
		return m_persistentPet.lock().get();
	}

	void PetManager::SetPersistentPet(GameCreatureS* pet)
	{
		m_persistentPetDespawned.disconnect();
		m_persistentPet.reset();

		if (!pet)
		{
			return;
		}

		m_persistentPet = std::static_pointer_cast<GameCreatureS>(pet->shared_from_this());
		m_persistentPetDespawned = pet->despawned.connect([this](GameObjectS&)
			{
				m_persistentPet.reset();
			});
	}

	void PetManager::PurgeGone()
	{
		m_temporaryPets.erase(std::remove_if(m_temporaryPets.begin(), m_temporaryPets.end(), [](const std::unique_ptr<TemporaryPet>& entry)
			{
				return entry->pet.expired();
			}), m_temporaryPets.end());
	}

	void PetManager::Despawn(GameCreatureS& pet)
	{
		if (!pet.GetWorldInstance())
		{
			return;
		}

		// Removal from the world is not allowed during the world update, which is where an owner
		// can die or reset. A zero despawn timer removes the pet on the next timer tick instead.
		pet.TriggerDespawnTimer(0);
	}

	void DespawnPetCorpse(GameCreatureS& pet)
	{
		WorldInstance* world = pet.GetWorldInstance();
		if (!world)
		{
			return;
		}

		// Posted: the creature's death state sets its own (longer) corpse despawn delay when it is
		// killed, and this has to override it.
		std::weak_ptr<GameCreatureS> weakPet = std::static_pointer_cast<GameCreatureS>(pet.shared_from_this());
		world->GetUniverse().Post([weakPet]()
			{
				if (const auto corpse = weakPet.lock(); corpse && corpse->GetWorldInstance() && !corpse->IsAlive())
				{
					corpse->TriggerDespawnTimer(PetCorpseDespawnDelay);
				}
			});
	}

	pet_result::Type MakeGmPet(GameUnitS& owner, GameCreatureS& target)
	{
		if (!target.IsAlive() || target.IsPet() || target.GetOwnerGuid() != 0 || !owner.GetWorldInstance())
		{
			return pet_result::NotTamable;
		}

		// The creature stops whatever it was doing, its fight included; the pet state takes over.
		// Leaving its combat state (EnterPetMode) drops the threat table, but wipe it first so
		// nothing of the old fight can pick a victim in between.
		target.GetMover().StopMovement();
		target.ResetThreat();
		target.StopAttack();
		target.RemoveAllCombatParticipants();
		target.SetInCombat(false, false);

		target.SetGrantsKillRewards(false);
		target.SetSummonOwner(std::static_pointer_cast<GameUnitS>(owner.shared_from_this()));
		target.SetOwnerGuid(owner.GetGuid());
		target.SetPetGroup(GmPetGroup);

		owner.GetPets().AddTemporaryPet(target, GmPetGroup, GmPetGroup, pet_limit_policy::ReplaceOldest);

		if (CreatureAI* ai = target.GetAI())
		{
			ai->EnterPetMode(owner);
		}

		return pet_result::Ok;
	}

	uint32 ResolvePetLevel(const pet_level_source::Type source, const GameUnitS& owner, const proto::UnitEntry& entry)
	{
		switch (source)
		{
		case pet_level_source::CharacterLevel:
			return owner.GetLevel();

		case pet_level_source::ClassLevel:
			if (const auto* player = dynamic_cast<const GamePlayerS*>(&owner))
			{
				return player->GetActiveClassLevel();
			}
			return owner.GetLevel();

		case pet_level_source::Creature:
		default:
			{
				const uint32 minLevel = std::max<uint32>(1, entry.minlevel());
				const uint32 maxLevel = std::max(minLevel, entry.maxlevel());
				std::uniform_int_distribution<uint32> levelDist(minLevel, maxLevel);
				return levelDist(randomGenerator);
			}
		}
	}

	uint32 GetSummonPetGroup(const proto::SpellEffect& effect, const uint32 spellId)
	{
		return effect.pet_group() != 0 ? effect.pet_group() : spellId;
	}

	uint32 GetSummonPetMaxCount(const proto::SpellEffect& effect, const uint32 summonCount)
	{
		return effect.pet_max_count() != 0 ? effect.pet_max_count() : std::max<uint32>(1, summonCount);
	}

	SpellCastResult CheckSummonPetLimit(const proto::SpellEntry& spell, const GameUnitS& caster)
	{
		const PetManager* pets = caster.FindPets();
		if (!pets)
		{
			// No pets at all: every group is empty, and a cap is at least 1.
			return spell_cast_result::CastOkay;
		}

		for (const auto& effect : spell.effects())
		{
			if (effect.type() != spell_effects::SummonPet ||
				static_cast<pet_limit_policy::Type>(effect.pet_limit_policy()) != pet_limit_policy::FailAtCap)
			{
				continue;
			}

			// The exact count may involve dice rolled when the effect runs; the base points are the
			// count a FailAtCap summon is designed around.
			const uint32 count = static_cast<uint32>(std::max(1, effect.basepoints()));
			const uint32 group = GetSummonPetGroup(effect, spell.id());
			const uint32 maxCount = GetSummonPetMaxCount(effect, count);
			if (pets->CanAddToGroup(group, maxCount, pet_limit_policy::FailAtCap) != pet_result::Ok)
			{
				return spell_cast_result::FailedTooManyPets;
			}
		}

		return spell_cast_result::CastOkay;
	}

	SpellCastResult CheckPetTargetEffects(const proto::SpellEntry& spell, const GameUnitS& caster)
	{
		for (const auto& effect : spell.effects())
		{
			if (effect.targeta() != spell_effect_targets::Pet)
			{
				continue;
			}

			const PetManager* pets = caster.FindPets();
			const GameCreatureS* pet = pets ? pets->GetPersistentPet() : nullptr;
			if (!pet)
			{
				return spell_cast_result::FailedNoPet;
			}

			if (!pet->IsAlive())
			{
				return spell_cast_result::FailedPetDead;
			}
		}

		return spell_cast_result::CastOkay;
	}

	SpellCastResult PetResultToCastResult(const pet_result::Type result)
	{
		switch (result)
		{
		case pet_result::Ok:
		case pet_result::Disabled:
			return spell_cast_result::CastOkay;
		case pet_result::TooManyPets:
			return spell_cast_result::FailedTooManyPets;
		case pet_result::NoPet:
			return spell_cast_result::FailedNoPet;
		case pet_result::PetDead:
			return spell_cast_result::FailedPetDead;
		case pet_result::NotTamable:
			return spell_cast_result::FailedNotTamable;
		case pet_result::TooHighLevel:
			return spell_cast_result::FailedTargetTooHighLevel;
		case pet_result::NoFreeSlot:
			return spell_cast_result::FailedNoFreePetSlot;
		case pet_result::AlreadyHavePet:
			return spell_cast_result::FailedAlreadyHavePet;
		case pet_result::WrongClass:
			return spell_cast_result::FailedPetWrongClass;
		default:
			return spell_cast_result::FailedError;
		}
	}

	SpellCastResult CheckPersistentPetEffects(const proto::SpellEntry& spell, const GamePlayerS& caster)
	{
		for (const auto& effect : spell.effects())
		{
			const uint32 petEntry = static_cast<uint32>(std::max(0, effect.miscvaluea()));

			pet_result::Type result = pet_result::Ok;
			switch (effect.type())
			{
			case spell_effects::SummonPersistentPet:
				result = caster.CanSummonStoredPet(petEntry);
				break;
			case spell_effects::GrantPersistentPet:
				result = caster.CanGrantPersistentPet(petEntry);
				break;
			case spell_effects::RevivePet:
				result = caster.CanRevivePet(petEntry);
				break;
			default:
				continue;
			}

			if (const SpellCastResult castResult = PetResultToCastResult(result); castResult != spell_cast_result::CastOkay)
			{
				return castResult;
			}
		}

		return spell_cast_result::CastOkay;
	}

	SpellCastResult CheckTameTarget(const proto::SpellEntry& spell, const GamePlayerS& caster, const GameUnitS& target)
	{
		for (const auto& effect : spell.effects())
		{
			if (effect.type() != spell_effects::TameCreature)
			{
				continue;
			}

			const auto* creature = dynamic_cast<const GameCreatureS*>(&target);
			const pet_result::Type result = creature ? caster.CanTameCreature(*creature) : pet_result::NotTamable;
			return PetResultToCastResult(result);
		}

		return spell_cast_result::CastOkay;
	}

	void NotifyPetOwnerOfAttacker(GameUnitS& pet, const uint64 attackerGuid)
	{
		WorldInstance* world = pet.GetWorldInstance();
		if (!world)
		{
			return;
		}

		// A player owner is put in combat through the pet's combat flag (GameUnitS::SetInCombat).
		// A creature owner's combat belongs to its AI: it joins the fight like an ally called for help.
		// Only real pets: other owned units (Summon effect adds) leave their owner alone.
		auto* owner = world->FindByGuid<GameCreatureS>(pet.GetOwnerGuid());
		if (!owner || owner == &pet || !owner->GetAI() || !owner->FindPets() || !owner->FindPets()->HasPet(pet))
		{
			return;
		}

		std::weak_ptr<GameCreatureS> weakOwner = std::static_pointer_cast<GameCreatureS>(owner->shared_from_this());
		world->GetUniverse().Post([weakOwner, attackerGuid]()
			{
				const auto strongOwner = weakOwner.lock();
				WorldInstance* ownerWorld = strongOwner ? strongOwner->GetWorldInstance() : nullptr;
				GameUnitS* attacker = ownerWorld ? ownerWorld->FindByGuid<GameUnitS>(attackerGuid) : nullptr;
				if (!attacker || !strongOwner->GetAI())
				{
					return;
				}

				strongOwner->GetAI()->OnCalledForHelp(*attacker);
			});
	}
}
