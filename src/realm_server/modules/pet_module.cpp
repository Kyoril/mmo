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
		DeletePets(characterGuid);
	}

	void PetModule::LoadPets(const uint64 characterGuid, std::function<void()> loaded)
	{
		// A previous session of the character may have left its pets behind.
		m_characters.erase(characterGuid);

		auto handler = [this, characterGuid, loaded = std::move(loaded)](const std::optional<std::vector<PersistentPetData>>& pets)
		{
			if (pets)
			{
				m_characters[characterGuid].pets = *pets;
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
			const auto findIn = [](const std::vector<PersistentPetData>& list, const uint64 dbId)
			{
				return std::find_if(list.begin(), list.end(), [dbId](const PersistentPetData& pet) { return pet.dbId == dbId; });
			};

			// Pets the node reports: write those that changed. A reported pet is known to the node.
			for (const auto& pet : pets)
			{
				known.unknownToWorld.erase(pet.dbId);

				const auto previous = findIn(known.pets, pet.dbId);
				if (previous == known.pets.end() || *previous != pet)
				{
					changed.push_back(pet);
				}
			}

			// Pets the node no longer reports were removed there, unless it may not know them yet.
			std::vector<PersistentPetData> kept;
			for (auto& pet : known.pets)
			{
				if (findIn(pets, pet.dbId) != pets.end())
				{
					continue;
				}

				if (known.unknownToWorld.count(pet.dbId) != 0)
				{
					kept.push_back(std::move(pet));
				}
				else
				{
					removed.push_back(pet.dbId);
				}
			}

			known.pets = std::move(pets);
			std::move(kept.begin(), kept.end(), std::back_inserter(known.pets));
		}

		if (changed.empty() && removed.empty())
		{
			return;
		}

		auto handler = [characterGuid](const bool success)
		{
			if (!success)
			{
				ELOG("Failed to save the stored pets of character " << log_hex_digit(characterGuid));
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
