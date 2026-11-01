// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The persistent pet part of GamePlayerS: the stored pets, summoning and dismissing them, their
// levels and experience, and creating new ones by granting and taming.

#include "game_server/objects/game_player_s.h"

#include "base/utilities.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/pets/persistent_pet_spawner.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include "shared/proto_data/project.h"

#include <algorithm>
#include <utility>

namespace mmo
{
	void GamePlayerS::SetStoredPets(std::vector<PersistentPetData> pets)
	{
		m_storedPets = std::move(pets);

		// A summoned pet that is no longer stored has nothing to be stored into.
		if (m_summonedPetDbId != 0 && !FindStoredPetByDbId(m_summonedPetDbId))
		{
			const auto pet = m_summonedPet.lock();
			m_summonedPetDbId = 0;
			m_summonedPet.reset();
			++m_petSummonToken;
			if (pet)
			{
				GetPets().SetPersistentPet(nullptr);
				if (pet->GetWorldInstance())
				{
					pet->TriggerDespawnTimer(0);
				}
			}
		}

		ResummonPersistentPet();
	}

	PersistentPetData* GamePlayerS::FindStoredPet(const uint32 petEntry)
	{
		return const_cast<PersistentPetData*>(std::as_const(*this).FindStoredPet(petEntry));
	}

	const PersistentPetData* GamePlayerS::FindStoredPet(const uint32 petEntry) const
	{
		const auto it = std::find_if(m_storedPets.begin(), m_storedPets.end(), [petEntry](const PersistentPetData& pet)
			{
				return pet.petEntry == petEntry;
			});
		return it != m_storedPets.end() ? &*it : nullptr;
	}

	PersistentPetData* GamePlayerS::FindStoredPetByDbId(const uint64 dbId)
	{
		return const_cast<PersistentPetData*>(std::as_const(*this).FindStoredPetByDbId(dbId));
	}

	const PersistentPetData* GamePlayerS::FindStoredPetByDbId(const uint64 dbId) const
	{
		const auto it = std::find_if(m_storedPets.begin(), m_storedPets.end(), [dbId](const PersistentPetData& pet)
			{
				return pet.dbId == dbId;
			});
		return it != m_storedPets.end() ? &*it : nullptr;
	}

	PersistentPetData* GamePlayerS::SelectStoredPet(const bool deadOnly)
	{
		return const_cast<PersistentPetData*>(std::as_const(*this).SelectStoredPet(deadOnly));
	}

	const PersistentPetData* GamePlayerS::SelectStoredPet(const bool deadOnly) const
	{
		const uint32 activeClass = Get<uint32>(object_fields::Class);
		const auto eligible = [deadOnly](const PersistentPetData& pet)
		{
			return !deadOnly || pet.isDead;
		};

		const PersistentPetData* last = FindStoredPetByDbId(m_lastSummonedPetDbId);
		if (last && last->classId == activeClass && eligible(*last))
		{
			return last;
		}

		const PersistentPetData* firstOfClass = nullptr;
		const PersistentPetData* first = nullptr;
		for (const auto& pet : m_storedPets)
		{
			if (!eligible(pet))
			{
				continue;
			}

			if (pet.classId == activeClass)
			{
				if (pet.wasSummoned)
				{
					return &pet;
				}

				if (!firstOfClass)
				{
					firstOfClass = &pet;
				}
			}

			if (!first)
			{
				first = &pet;
			}
		}

		return firstOfClass ? firstOfClass : first;
	}

	void GamePlayerS::NotifyPetResult(const pet_result::Type result)
	{
		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetResult(result);
		}
	}

	pet_result::Type GamePlayerS::PetActionFailed(const pet_result::Type result)
	{
		NotifyPetResult(result);
		return result;
	}

	bool GamePlayerS::ArePetsAvailable() const
	{
		return !m_netPlayerWatcher || m_netPlayerWatcher->ArePetsAvailable();
	}

	pet_result::Type GamePlayerS::CanSummonStoredPet(const uint32 petEntry) const
	{
		if (!ArePetsAvailable())
		{
			return pet_result::Disabled;
		}

		const PersistentPetData* pet = petEntry != 0 ? FindStoredPet(petEntry) : SelectStoredPet(false);
		if (!pet)
		{
			return pet_result::NoPet;
		}

		return CheckSummonPet(*pet);
	}

	pet_result::Type GamePlayerS::SummonStoredPet(const uint32 petEntry)
	{
		if (const pet_result::Type check = CanSummonStoredPet(petEntry); check != pet_result::Ok)
		{
			return PetActionFailed(check);
		}

		PersistentPetData* pet = petEntry != 0 ? FindStoredPet(petEntry) : SelectStoredPet(false);
		ASSERT(pet);

		const pet_result::Type result = SummonPet(*pet);
		if (result != pet_result::Ok)
		{
			return PetActionFailed(result);
		}

		return result;
	}

	pet_result::Type GamePlayerS::CheckSummonPet(const PersistentPetData& pet) const
	{
		// Also covers the summons nobody asked for: the return with the owner and a tamed pet the
		// realm created. Such a pet stays wasSummoned and comes back once pets are available again.
		if (!ArePetsAvailable())
		{
			return pet_result::Disabled;
		}

		// Class lock: a pet belongs to the class that granted or tamed it.
		if (pet.classId != Get<uint32>(object_fields::Class))
		{
			return pet_result::WrongClass;
		}

		// A tamed pet that died needs a revive; a summon-style pet is recreated.
		if (pet.isDead && pet.unitEntry != 0)
		{
			return pet_result::PetDead;
		}

		const proto::PetEntry* entry = m_project.pets.getById(pet.petEntry);
		if (!entry || !GetPersistentPetUnitEntry(m_project, pet, *entry))
		{
			return pet_result::NoPet;
		}

		if (m_classEntry && m_classEntry->max_summoned_persistent_pets() == 0)
		{
			return pet_result::TooManyPets;
		}

		return pet_result::Ok;
	}

	pet_result::Type GamePlayerS::SummonPet(PersistentPetData& pet)
	{
		if (const pet_result::Type check = CheckSummonPet(pet); check != pet_result::Ok)
		{
			if (check == pet_result::NoPet)
			{
				ELOG("Stored pet " << pet.dbId << " of character " << log_hex_digit(GetGuid()) << " uses unknown pet entry " << pet.petEntry);
			}

			return check;
		}

		// Already summoned (or about to be).
		if (m_summonedPetDbId == pet.dbId)
		{
			return pet_result::Ok;
		}

		// A character has one summoned persistent pet: at the class cap the current one goes first.
		const uint64 dbId = pet.dbId;
		DismissPersistentPet(false);

		// DismissPersistentPet does not change the list, but look the pet up again to be safe.
		PersistentPetData* stored = FindStoredPetByDbId(dbId);
		ASSERT(stored);

		if (stored->isDead)
		{
			// A summon-style pet comes back at full health.
			stored->isDead = false;
			stored->health = 0;
			stored->power = 0;
		}

		stored->wasSummoned = true;
		m_lastSummonedPetDbId = dbId;

		// Outside of a world the pet comes back when the character enters one (wasSummoned).
		if (WorldInstance* world = GetWorldInstance())
		{
			m_summonedPetDbId = dbId;
			const uint32 token = ++m_petSummonToken;

			// Deferred: summons happen inside spell effects and world object callbacks, where
			// objects must not enter the world.
			std::weak_ptr<GamePlayerS> weakThis = std::static_pointer_cast<GamePlayerS>(shared_from_this());
			world->GetUniverse().Post([weakThis, token]()
				{
					if (const auto strongThis = weakThis.lock())
					{
						strongThis->SpawnSummonedPet(token);
					}
				});
		}

		storedPetsChanged();
		return pet_result::Ok;
	}

	void GamePlayerS::SpawnSummonedPet(const uint32 token)
	{
		if (token != m_petSummonToken || m_summonedPetDbId == 0)
		{
			return;
		}

		WorldInstance* world = GetWorldInstance();
		PersistentPetData* data = FindStoredPetByDbId(m_summonedPetDbId);
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		const proto::UnitEntry* unitEntry = entry ? GetPersistentPetUnitEntry(m_project, *data, *entry) : nullptr;
		if (!world || !IsAlive() || !unitEntry || data->classId != Get<uint32>(object_fields::Class))
		{
			// Still wasSummoned: the pet comes back with its owner (resurrection, next world).
			m_summonedPetDbId = 0;
			return;
		}

		// A previous incarnation of this pet - its corpse, or the pet dismissed a moment ago - may
		// still be in the world with the same guid. Posted work runs outside the world update, so
		// it can be removed right away.
		if (GameObjectS* previous = world->FindObjectByGuid(MakePetGuid(data->dbId, data->petEntry)))
		{
			world->RemoveGameObject(*previous);
		}

		const uint32 healthPercent = m_reviveHealthPetDbId == data->dbId ? m_reviveHealthPercent : 0;
		m_reviveHealthPetDbId = 0;
		m_reviveHealthPercent = 0;

		const auto pet = SpawnPersistentPet(*this, *world, *data, *entry, *unitEntry, healthPercent);
		if (!pet)
		{
			m_summonedPetDbId = 0;
			return;
		}

		m_summonedPet = pet;
		GameCreatureS* petPtr = pet.get();
		m_summonedPetKilled = pet->killed.connect([this, petPtr](GameUnitS*)
			{
				OnPersistentPetKilled(*petPtr);
			});
		m_summonedPetDespawned = pet->despawned.connect([this, petPtr](GameObjectS&)
			{
				OnPersistentPetDespawned(*petPtr);
			});

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetSpellsChanged();
			if (static_cast<pet_level_mode::Type>(entry->level_mode()) == pet_level_mode::OwnLevel)
			{
				m_netPlayerWatcher->OnPetXpChanged(data->xp, GetPetXpToNextLevel(m_classEntry, data->level));
			}
		}
	}

	void GamePlayerS::DismissPersistentPet(const bool keepWasSummoned)
	{
		if (m_summonedPetDbId == 0)
		{
			return;
		}

		// Cancels a spawn that has not happened yet.
		++m_petSummonToken;

		PersistentPetData* data = FindStoredPetByDbId(m_summonedPetDbId);
		const std::shared_ptr<GameCreatureS> pet = m_summonedPet.lock();
		m_summonedPetDbId = 0;
		m_summonedPet.reset();

		if (pet)
		{
			const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
			if (data && entry)
			{
				StorePersistentPetState(*pet, *entry, *data);
			}

			if (FindPets() && FindPets()->GetPersistentPet() == pet.get())
			{
				FindPets()->SetPersistentPet(nullptr);
			}

			// Removal from the world is not allowed during the world update, which is where an owner
			// can die. A zero despawn timer removes the pet on the next timer tick instead.
			if (pet->GetWorldInstance())
			{
				pet->TriggerDespawnTimer(0);
			}
		}

		if (data)
		{
			data->wasSummoned = keepWasSummoned;
		}

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetSpellsChanged();
		}

		storedPetsChanged();
	}

	void GamePlayerS::ResummonPersistentPet()
	{
		if (m_summonedPetDbId != 0 || !GetWorldInstance() || !IsAlive())
		{
			return;
		}

		const uint32 activeClass = Get<uint32>(object_fields::Class);
		for (auto& pet : m_storedPets)
		{
			if (pet.wasSummoned && !pet.isDead && pet.classId == activeClass)
			{
				// Quietly: nobody asked for this summon, so a failure is not reported.
				SummonPet(pet);
				return;
			}
		}
	}

	void GamePlayerS::OnPersistentPetKilled(GameCreatureS& pet)
	{
		if (m_summonedPet.lock().get() != &pet)
		{
			return;
		}

		// Stored dead: a tamed pet needs a revive, a summon-style pet comes back at full health.
		if (PersistentPetData* data = FindStoredPetByDbId(m_summonedPetDbId))
		{
			data->isDead = true;
			data->health = 0;
			data->power = 0;
			data->wasSummoned = false;
		}

		m_summonedPetDbId = 0;
		m_summonedPet.reset();
		++m_petSummonToken;

		// Called from the pet's killed signal: the connections to it stay until the next summon.
		if (FindPets())
		{
			FindPets()->SetPersistentPet(nullptr);
		}

		// The corpse does not stay around like a creature's.
		DespawnPetCorpse(pet);

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetSpellsChanged();
		}

		storedPetsChanged();
	}

	void GamePlayerS::OnPersistentPetDespawned(GameCreatureS& pet)
	{
		if (m_summonedPet.lock().get() != &pet)
		{
			return;
		}

		// Taken out of the world by something else (a GM, the instance closing): it is stored as it
		// is and comes back with its owner.
		PersistentPetData* data = FindStoredPetByDbId(m_summonedPetDbId);
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		if (data && entry)
		{
			StorePersistentPetState(pet, *entry, *data);
		}

		m_summonedPetDbId = 0;
		m_summonedPet.reset();
		++m_petSummonToken;

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetSpellsChanged();
		}

		storedPetsChanged();
	}

	void GamePlayerS::AddPetExperience(const uint32 xp)
	{
		const auto pet = m_summonedPet.lock();
		PersistentPetData* data = pet ? FindStoredPetByDbId(m_summonedPetDbId) : nullptr;
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		if (xp == 0 || !entry || !pet->IsAlive() ||
			static_cast<pet_level_mode::Type>(entry->level_mode()) != pet_level_mode::OwnLevel)
		{
			return;
		}

		// Pets stop at their master's level and resume when it rises.
		const uint32 cap = GetPetMasterLevel(*this, *entry);
		const uint32 oldLevel = data->level;
		if (oldLevel >= cap)
		{
			return;
		}

		uint32 level = oldLevel;
		uint32 nextLevelXp = GetPetXpToNextLevel(m_classEntry, level);
		data->xp += xp;
		while (level < cap && data->xp >= nextLevelXp)
		{
			data->xp -= nextLevelXp;
			++level;
			nextLevelXp = GetPetXpToNextLevel(m_classEntry, level);
		}

		// A single grant can overshoot the cap; keep the leftover below the next level.
		if (level >= cap && data->xp >= nextLevelXp)
		{
			data->xp = nextLevelXp - 1;
		}

		if (level != oldLevel)
		{
			data->level = level;
			ApplyPersistentPetLevel(*pet, *entry, *data, oldLevel, level);
		}

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetXpChanged(data->xp, nextLevelXp);
		}

		storedPetsChanged();
	}

	void GamePlayerS::RefreshPersistentPetLevel()
	{
		const auto pet = m_summonedPet.lock();
		PersistentPetData* data = pet ? FindStoredPetByDbId(m_summonedPetDbId) : nullptr;
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		if (!entry || !pet->IsAlive() ||
			static_cast<pet_level_mode::Type>(entry->level_mode()) != pet_level_mode::MasterLevel)
		{
			return;
		}

		const uint32 oldLevel = pet->GetLevel();
		const uint32 newLevel = GetPetMasterLevel(*this, *entry);
		if (newLevel == oldLevel)
		{
			return;
		}

		data->level = newLevel;
		ApplyPersistentPetLevel(*pet, *entry, *data, oldLevel, newLevel);
		storedPetsChanged();
	}

	void GamePlayerS::ApplyPersistentPetLevel(GameCreatureS& pet, const proto::PetEntry& entry, PersistentPetData& data, const uint32 oldLevel, const uint32 newLevel)
	{
		// Refreshes the stats from the creature's formula (times the entry's multipliers).
		pet.SetLevel(newLevel);

		if (m_netPlayerWatcher)
		{
			for (const auto& levelSpell : entry.level_spells())
			{
				if (levelSpell.level() > oldLevel && levelSpell.level() <= newLevel)
				{
					m_netPlayerWatcher->OnPetLearnedSpell(levelSpell.spell());
				}
			}
		}

		ApplyPersistentPetSpells(pet, entry, GetKnownPetSpells(entry, data, newLevel));

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetSpellsChanged();
		}
	}

	bool GamePlayerS::HasFreePetSlot() const
	{
		return m_storedPets.size() + m_pendingPetCreations.size() < MaxStoredPets;
	}

	void GamePlayerS::RequestPetCreation(const PersistentPetData& pet)
	{
		m_pendingPetCreations.push_back(pet.petEntry);
		petCreationRequested(pet);
	}

	void GamePlayerS::OnPetCreated(const PersistentPetData& pet)
	{
		const auto pending = std::find(m_pendingPetCreations.begin(), m_pendingPetCreations.end(), pet.petEntry);
		if (pending != m_pendingPetCreations.end())
		{
			m_pendingPetCreations.erase(pending);
		}

		if (pet.dbId == 0)
		{
			ELOG("Created pet of character " << log_hex_digit(GetGuid()) << " has no database id");
			return;
		}

		if (FindStoredPetByDbId(pet.dbId))
		{
			WLOG("Pet " << pet.dbId << " of character " << log_hex_digit(GetGuid()) << " was created twice");
			return;
		}

		// wasSummoned on a new pet means "summon it once it exists" (a tamed pet).
		const bool summon = pet.wasSummoned;
		m_storedPets.push_back(pet);
		m_storedPets.back().wasSummoned = false;
		storedPetsChanged();

		if (summon)
		{
			const pet_result::Type result = SummonPet(m_storedPets.back());
			if (result != pet_result::Ok)
			{
				PetActionFailed(result);
			}
		}
	}

	pet_result::Type GamePlayerS::CanTameCreature(const GameCreatureS& target) const
	{
		if (!ArePetsAvailable())
		{
			return pet_result::Disabled;
		}

		if (!target.IsAlive() || target.IsPet() || target.GetOwnerGuid() != 0)
		{
			return pet_result::NotTamable;
		}

		if (target.GetEntry().tame_pet_entry() == 0 || !m_project.pets.getById(target.GetEntry().tame_pet_entry()))
		{
			return pet_result::NotTamable;
		}

		if (target.GetLevel() > GetLevel())
		{
			return pet_result::TooHighLevel;
		}

		if (!HasFreePetSlot())
		{
			return pet_result::NoFreeSlot;
		}

		return pet_result::Ok;
	}

	pet_result::Type GamePlayerS::TameCreature(GameCreatureS& target)
	{
		if (const pet_result::Type check = CanTameCreature(target); check != pet_result::Ok)
		{
			return PetActionFailed(check);
		}

		const proto::PetEntry* entry = m_project.pets.getById(target.GetEntry().tame_pet_entry());
		ASSERT(entry);

		PersistentPetData pet;
		pet.classId = Get<uint32>(object_fields::Class);
		pet.petEntry = entry->id();
		pet.unitEntry = target.GetEntry().id();
		pet.name = ChoosePetName(*entry, target.GetName());
		pet.level = std::clamp<uint32>(target.GetLevel(), 1, GetPetMasterLevel(*this, *entry));

		// Summoned as soon as the realm created it (see OnPetCreated).
		pet.wasSummoned = true;

		// The creature leaves the world now; the pet that replaces it is a new object.
		if (target.GetWorldInstance())
		{
			target.TriggerDespawnTimer(0);
		}

		RequestPetCreation(pet);
		return pet_result::Ok;
	}

	pet_result::Type GamePlayerS::CanGrantPersistentPet(const uint32 petEntry) const
	{
		if (!ArePetsAvailable())
		{
			return pet_result::Disabled;
		}

		if (!m_project.pets.getById(petEntry))
		{
			return pet_result::NoPet;
		}

		if (FindStoredPet(petEntry) ||
			std::find(m_pendingPetCreations.begin(), m_pendingPetCreations.end(), petEntry) != m_pendingPetCreations.end())
		{
			return pet_result::AlreadyHavePet;
		}

		if (!HasFreePetSlot())
		{
			return pet_result::NoFreeSlot;
		}

		return pet_result::Ok;
	}

	pet_result::Type GamePlayerS::GrantPersistentPet(const uint32 petEntry)
	{
		if (const pet_result::Type check = CanGrantPersistentPet(petEntry); check != pet_result::Ok)
		{
			if (check == pet_result::NoPet)
			{
				ELOG("GrantPersistentPet: unknown pet entry " << petEntry);
			}

			return PetActionFailed(check);
		}

		const proto::PetEntry* entry = m_project.pets.getById(petEntry);
		ASSERT(entry);

		PersistentPetData pet;
		pet.classId = Get<uint32>(object_fields::Class);
		pet.petEntry = petEntry;
		pet.name = ChoosePetName(*entry, String());

		// A granted pet starts at its master's level: an OwnLevel pet of a high level master would be
		// useless at level 1.
		pet.level = GetPetMasterLevel(*this, *entry);
		RequestPetCreation(pet);
		return pet_result::Ok;
	}

	pet_result::Type GamePlayerS::CanRevivePet(const uint32 petEntry) const
	{
		if (!ArePetsAvailable())
		{
			return pet_result::Disabled;
		}

		const PersistentPetData* pet = petEntry != 0 ? FindStoredPet(petEntry) : SelectStoredPet(true);
		if (!pet || !pet->isDead)
		{
			return pet_result::NoPet;
		}

		if (pet->classId != Get<uint32>(object_fields::Class))
		{
			return pet_result::WrongClass;
		}

		// The revived pet is summoned right away.
		PersistentPetData revived = *pet;
		revived.isDead = false;
		return CheckSummonPet(revived);
	}

	pet_result::Type GamePlayerS::RevivePet(const uint32 petEntry, const uint32 healthPercent)
	{
		if (!ArePetsAvailable())
		{
			return PetActionFailed(pet_result::Disabled);
		}

		PersistentPetData* pet = petEntry != 0 ? FindStoredPet(petEntry) : SelectStoredPet(true);
		if (!pet || !pet->isDead)
		{
			return PetActionFailed(pet_result::NoPet);
		}

		if (pet->classId != Get<uint32>(object_fields::Class))
		{
			return PetActionFailed(pet_result::WrongClass);
		}

		pet->isDead = false;
		pet->health = 0;
		pet->power = 0;
		m_reviveHealthPetDbId = pet->dbId;
		m_reviveHealthPercent = std::min<uint32>(healthPercent, 100);

		const pet_result::Type result = SummonPet(*pet);
		if (result != pet_result::Ok)
		{
			// Revived, but it stays stored.
			m_reviveHealthPetDbId = 0;
			m_reviveHealthPercent = 0;
			storedPetsChanged();
			return PetActionFailed(result);
		}

		return result;
	}

	pet_result::Type GamePlayerS::LearnPetSpell(const uint32 spellId)
	{
		if (!ArePetsAvailable())
		{
			return PetActionFailed(pet_result::Disabled);
		}

		const auto pet = m_summonedPet.lock();
		PersistentPetData* data = pet ? FindStoredPetByDbId(m_summonedPetDbId) : nullptr;
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		if (!entry)
		{
			return PetActionFailed(pet_result::NoPet);
		}

		if (!m_project.spells.getById(spellId))
		{
			ELOG("LearnPetSpell: unknown spell " << spellId);
			return PetActionFailed(pet_result::NoPet);
		}

		const std::vector<uint32> known = GetKnownPetSpells(*entry, *data, pet->GetLevel());
		if (std::find(known.begin(), known.end(), spellId) != known.end())
		{
			return pet_result::Ok;
		}

		data->spells.push_back(spellId);
		ApplyPersistentPetSpells(*pet, *entry, GetKnownPetSpells(*entry, *data, pet->GetLevel()));

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetLearnedSpell(spellId);
			m_netPlayerWatcher->OnPetSpellsChanged();
		}

		storedPetsChanged();
		return pet_result::Ok;
	}

	pet_result::Type GamePlayerS::RenamePersistentPet(const String& name)
	{
		if (!ArePetsAvailable())
		{
			return PetActionFailed(pet_result::Disabled);
		}

		const auto pet = m_summonedPet.lock();
		PersistentPetData* data = pet ? FindStoredPetByDbId(m_summonedPetDbId) : nullptr;
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		if (!entry)
		{
			return PetActionFailed(pet_result::NoPet);
		}

		if (!entry->allow_player_naming())
		{
			return PetActionFailed(pet_result::NamingNotAllowed);
		}

		if (!IsValidPetName(name))
		{
			return PetActionFailed(pet_result::InvalidName);
		}

		const String normalized = NormalizePetName(name);
		if (normalized == data->name)
		{
			return pet_result::Ok;
		}

		data->name = normalized;
		pet->SetCustomName(normalized);

		if (m_netPlayerWatcher)
		{
			m_netPlayerWatcher->OnPetSpellsChanged();
		}

		storedPetsChanged();
		return pet_result::Ok;
	}

	game::PetSpellsData GamePlayerS::GetPetSpellsData() const
	{
		game::PetSpellsData result;

		const auto pet = m_summonedPet.lock();
		const PersistentPetData* data = pet ? FindStoredPetByDbId(m_summonedPetDbId) : nullptr;
		const proto::PetEntry* entry = data ? m_project.pets.getById(data->petEntry) : nullptr;
		if (!entry)
		{
			return result;
		}

		result.petGuid = pet->GetGuid();
		result.petEntry = data->petEntry;
		result.name = data->name;
		result.levelMode = static_cast<uint8>(entry->level_mode());
		result.canRename = entry->allow_player_naming();
		if (const CreatureAI* ai = pet->GetAI())
		{
			result.strategy = static_cast<uint8>(ai->GetPetStrategy());
		}

		result.spells = GetKnownPetSpells(*entry, *data, pet->GetLevel());
		return result;
	}

	void GamePlayerS::OnResurrected()
	{
		ResummonPersistentPet();
	}

	void GamePlayerS::OnDespawn()
	{
		// Logout or map change: the pet is stored with its current state before the character's
		// own despawn signal triggers the save, and comes back when the character enters a world.
		DismissPersistentPet(true);

		GameUnitS::OnDespawn();
	}
}
