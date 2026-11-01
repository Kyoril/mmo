// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_manager.h"

#include "base/utilities.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
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

		// A dead pet no longer counts: its corpse decays on its own.
		entry->killed = pet.killed.connect([entryPtr](GameUnitS*)
			{
				entryPtr->pet.reset();
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

	void PetManager::Command(pet_command::Type /*command*/, uint64 /*targetGuid*/, uint32 /*param*/)
	{
		// Intentionally empty: pet commands are executed by the pet mode of CreatureAI, which is
		// forwarded to from here once it exists. Until then pets simply follow their owner.
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
}
