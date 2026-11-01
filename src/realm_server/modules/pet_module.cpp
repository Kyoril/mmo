// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_module.h"

#include "world.h"

#include "base/utilities.h"
#include "game/guid_utils.h"
#include "game/pet_defines.h"
#include "log/default_log_levels.h"

#include <algorithm>

namespace mmo
{
	PetModule::PetModule(AsyncPetDatabase& database)
		: m_database(database)
	{
	}

	void PetModule::OnCharacterLoading(Player& player, const uint64 characterGuid, std::function<void()> loaded)
	{
		LoadPets(characterGuid, std::move(loaded));
	}

	void PetModule::OnJoiningWorld(Player& player, World& world)
	{
		const uint64 characterGuid = player.GetCharacterGuid();
		if (const auto pets = GetPetsForJoin(characterGuid))
		{
			world.SendPlayerPetData(characterGuid, *pets);
		}
	}

	void PetModule::OnLeftWorld(Player& player)
	{
		ForgetPets(player.GetCharacterGuid());
	}

	void PetModule::OnCharacterDeleting(Player& player, const uint64 characterGuid)
	{
		OnCharacterSoftDeleted(characterGuid);
	}

	void PetModule::OnCharacterSoftDeleted(const uint64 characterGuid)
	{
		// Character deletion only marks the character deleted (it can be restored), so its pets stay
		// in the database with it. Only the cached list goes.
		ForgetPets(characterGuid);
	}

	void PetModule::LoadPets(const uint64 characterGuid, std::function<void()> loaded)
	{
		// A previous session of the character may have left its pets behind.
		m_characters.erase(characterGuid);

		auto handler = [this, characterGuid, loaded = std::move(loaded)](const std::optional<std::vector<PersistentPetData>>& pets)
		{
			if (pets)
			{
				CharacterPets& known = m_characters[characterGuid];
				known.generation = ++m_nextGeneration;
				known.pets = *pets;
				for (const auto& pet : *pets)
				{
					known.confirmed[pet.dbId] = pet;
				}
			}
			else
			{
				ELOG("Failed to load the stored pets of character " << log_hex_digit(characterGuid) << "; it enters the world without them");
			}

			loaded();
		};

		m_database.asyncRequestKeyed(characterGuid, std::move(handler), &IPetDatabase::LoadCharacterPets, characterGuid);
	}

	const std::vector<PersistentPetData>* PetModule::FindPets(const uint64 characterGuid) const
	{
		const auto it = m_characters.find(characterGuid);
		return it != m_characters.end() ? &it->second.pets : nullptr;
	}

	std::optional<std::vector<PersistentPetData>> PetModule::GetPetsForJoin(const uint64 characterGuid)
	{
		const auto it = m_characters.find(characterGuid);
		if (it == m_characters.end())
		{
			return std::nullopt;
		}

		it->second.unknownToWorld.clear();
		return it->second.pets;
	}

	void PetModule::ForgetPets(const uint64 characterGuid)
	{
		m_characters.erase(characterGuid);
	}

	void PetModule::SavePets(const uint64 characterGuid, std::vector<PersistentPetData> pets)
	{
		std::vector<PersistentPetData> changed;
		std::vector<uint64> removed;
		std::optional<uint32> generation;

		const auto it = m_characters.find(characterGuid);
		if (it == m_characters.end())
		{
			// Not in the world any more (the node's last save after a disconnect): nothing tells
			// which pets are gone, so only the reported ones are written.
			changed = std::move(pets);
		}
		else
		{
			CharacterPets& known = it->second;
			generation = known.generation;
			const auto findIn = [](const std::vector<PersistentPetData>& list, const uint64 dbId)
			{
				return std::find_if(list.begin(), list.end(), [dbId](const PersistentPetData& pet) { return pet.dbId == dbId; });
			};

			// The new latest state: the reported pets, plus those the node may not know yet. A
			// reported pet is known to the node; the others it no longer reports were removed there.
			std::vector<PersistentPetData> latest = std::move(pets);
			for (const auto& pet : latest)
			{
				known.unknownToWorld.erase(pet.dbId);
			}
			for (auto& pet : known.pets)
			{
				if (findIn(latest, pet.dbId) == latest.end() && known.unknownToWorld.count(pet.dbId) != 0)
				{
					latest.push_back(std::move(pet));
				}
			}
			known.pets = std::move(latest);

			// Written is what the database does not hold yet, deleted is what it holds and should not.
			for (const auto& pet : known.pets)
			{
				const auto confirmed = known.confirmed.find(pet.dbId);
				if (confirmed == known.confirmed.end() || confirmed->second != pet)
				{
					changed.push_back(pet);
				}
			}
			for (const auto& [dbId, pet] : known.confirmed)
			{
				if (findIn(known.pets, dbId) == known.pets.end())
				{
					removed.push_back(dbId);
				}
			}
		}

		if (changed.empty() && removed.empty())
		{
			return;
		}

		// Only a write that succeeded is recorded as what the database holds; a failed one is
		// repeated by the next save, the logout save included.
		auto handler = [this, characterGuid, generation, written = changed, deleted = removed](const bool success)
		{
			if (!success)
			{
				ELOG("Failed to save the stored pets of character " << log_hex_digit(characterGuid) << "; the next save writes them again");
				return;
			}

			const auto known = m_characters.find(characterGuid);
			if (!generation || known == m_characters.end() || known->second.generation != *generation)
			{
				// Saved for a session that is over; the current one loaded after this write.
				return;
			}

			for (const auto& pet : written)
			{
				known->second.confirmed[pet.dbId] = pet;
			}
			for (const uint64 dbId : deleted)
			{
				known->second.confirmed.erase(dbId);
			}
		};
		m_database.asyncRequestKeyed(characterGuid, std::move(handler), &IPetDatabase::SaveCharacterPets, characterGuid, std::move(changed), std::move(removed));
	}

	void PetModule::CreatePet(const uint64 characterGuid, const PersistentPetData& request, CreatedCallback created)
	{
		// The world node checks the slots too, but it is not the one that counts them in the database.
		if (const auto* pets = FindPets(characterGuid); pets && pets->size() >= MaxStoredPets)
		{
			WLOG("Refused to create a pet for character " << log_hex_digit(characterGuid) << ": all " << MaxStoredPets << " stored pet slots are taken");
			PersistentPetData refused = request;
			refused.dbId = 0;
			created(refused);
			return;
		}

		auto handler = [this, characterGuid, request, created = std::move(created)](const uint64 petId)
		{
			PersistentPetData pet = request;
			pet.dbId = petId;

			if (petId == 0)
			{
				ELOG("Failed to create a stored pet (entry " << request.petEntry << ") for character " << log_hex_digit(characterGuid));
			}
			else
			{
				if (petId > MaxPetGuidDbId)
				{
					ELOG("Stored pet id " << petId << " of character " << log_hex_digit(characterGuid) << " does not fit a pet guid; pet guids will collide");
				}

				const auto it = m_characters.find(characterGuid);
				if (it != m_characters.end())
				{
					// As the world node will store it: not summoned yet (it summons a tamed pet itself).
					PersistentPetData stored = pet;
					stored.wasSummoned = false;
					it->second.confirmed[petId] = stored;
					it->second.pets.push_back(std::move(stored));
					it->second.unknownToWorld.insert(petId);
				}
			}

			created(pet);
		};

		m_database.asyncRequestKeyed(characterGuid, std::move(handler), &IPetDatabase::CreateCharacterPet, characterGuid, request);
	}

	void PetModule::DeletePets(const uint64 characterGuid)
	{
		m_characters.erase(characterGuid);

		auto handler = [characterGuid](const bool success)
		{
			if (!success)
			{
				ELOG("Failed to delete the stored pets of character " << log_hex_digit(characterGuid));
			}
		};
		m_database.asyncRequestKeyed(characterGuid, std::move(handler), &IPetDatabase::DeleteCharacterPets, characterGuid);
	}
}
