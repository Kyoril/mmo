// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The pet packets of a world node player: commands, pet casts, renaming and pet name queries from
// the client, and the PetSpells / PetXp / PetLearnedSpell / PetResult notifications to it.

#include "player.h"

#include "game/guid_utils.h"
#include "game/pet_packets.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/world/world_instance.h"
#include "base/utilities.h"
#include "log/default_log_levels.h"
#include "shared/proto_data/project.h"

namespace mmo
{
	namespace
	{
		/// Sends a SpellFailure for a spell of a pet to its owner's client.
		void SendPetSpellFailure(Player& player, const uint64 petGuid, const uint32 spellId, const SpellCastResult result)
		{
			player.SendPacket([petGuid, spellId, result](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::SpellFailure);
					packet
						<< io::write_packed_guid(petGuid)
						<< io::write<uint32>(spellId)
						<< io::write<GameTime>(GetAsyncTimeMs())
						<< io::write<uint8>(result);
					packet.Finish();
				});
		}
	}

	void Player::SendPetResult(const pet_result::Type result)
	{
		game::PetResultData data;
		data.result = static_cast<uint8>(result);
		SendPacket([&data](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::PetResult);
				packet << data;
				packet.Finish();
			});
	}

	void Player::OnPetSpellsChanged()
	{
		const game::PetSpellsData data = m_character->GetPetSpellsData();
		SendPacket([&data](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::PetSpells);
				packet << data;
				packet.Finish();
			});
	}

	void Player::OnPetXpChanged(const uint32 xp, const uint32 nextLevelXp)
	{
		game::PetXpData data;
		data.xp = xp;
		data.nextLevelXp = nextLevelXp;
		SendPacket([&data](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::PetXp);
				packet << data;
				packet.Finish();
			});
	}

	void Player::OnPetLearnedSpell(const uint32 spellId)
	{
		game::PetLearnedSpellData data;
		data.spellId = spellId;
		SendPacket([&data](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::PetLearnedSpell);
				packet << data;
				packet.Finish();
			});
	}

	void Player::OnPetResult(const pet_result::Type result)
	{
		SendPetResult(result);
	}

	void Player::OnPetCommand(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		game::PetCommandRequest request;
		if (!(contentReader >> request))
		{
			ELOG("Failed to read PetCommand packet");
			return;
		}

		if (request.command > pet_command::Dismiss)
		{
			WLOG("Player " << log_hex_digit(m_character->GetGuid()) << " sent unknown pet command " << static_cast<uint32>(request.command));
			return;
		}

		const auto command = static_cast<pet_command::Type>(request.command);
		if (command == pet_command::SetStrategy && request.param > pet_strategy::Passive)
		{
			WLOG("Player " << log_hex_digit(m_character->GetGuid()) << " sent unknown pet strategy " << request.param);
			return;
		}

		// Switched off, the node dismissed every pet; a command that races the switch is refused.
		if (!ArePetsAvailable())
		{
			SendPetResult(pet_result::Disabled);
			return;
		}

		PetManager* pets = m_character->FindPets();
		bool hasPet = false;
		if (pets)
		{
			pets->ForEachPet([&hasPet](GameCreatureS&)
				{
					hasPet = true;
				});
		}

		if (!hasPet)
		{
			SendPetResult(pet_result::NoPet);
			return;
		}

		// The owner's target rules: a pet only attacks what its owner's client was sent. The pet AI
		// checks that the target is alive and an enemy.
		if (command == pet_command::Attack && (request.target == 0 || !IsObjectKnown(request.target)))
		{
			return;
		}

		const bool hadPersistentPet = pets->GetPersistentPet() != nullptr;
		pets->Command(command, command == pet_command::Attack ? request.target : 0, command == pet_command::SetStrategy ? request.param : 0);

		// PetSpells carries the strategy. A dismissed persistent pet already sent an empty PetSpells.
		if (command == pet_command::SetStrategy && hadPersistentPet)
		{
			OnPetSpellsChanged();
		}
	}

	void Player::OnPetCastSpell(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		game::PetCastSpellRequest request;
		if (!(contentReader >> request))
		{
			ELOG("Failed to read PetCastSpell packet");
			return;
		}

		if (!ArePetsAvailable())
		{
			SendPetResult(pet_result::Disabled);
			return;
		}

		PetManager* pets = m_character->FindPets();
		GameCreatureS* pet = pets ? pets->GetPersistentPet() : nullptr;
		if (!pet)
		{
			SendPetResult(pet_result::NoPet);
			return;
		}

		if (!pet->IsAlive())
		{
			SendPetResult(pet_result::PetDead);
			return;
		}

		const proto::SpellEntry* spell = m_project.spells.getById(request.spellId);
		if (!spell)
		{
			WLOG("Player " << log_hex_digit(m_character->GetGuid()) << " made its pet cast unknown spell " << request.spellId);
			return;
		}

		if (spell->attributes(0) & spell_attributes::Passive)
		{
			WLOG("Player " << log_hex_digit(m_character->GetGuid()) << " made its pet cast passive spell " << request.spellId);
			return;
		}

		const uint64 petGuid = pet->GetGuid();
		if (!pet->HasSpell(request.spellId))
		{
			SendPetSpellFailure(*this, petGuid, request.spellId, spell_cast_result::FailedNotKnown);
			return;
		}

		// The owner's target rules: the pet may only aim at units the owner's client was sent (or at
		// itself and its owner).
		if (request.targetMap.HasUnitTarget())
		{
			const uint64 targetGuid = request.targetMap.GetUnitTarget();
			if (targetGuid != petGuid && targetGuid != m_character->GetGuid() && !IsObjectKnown(targetGuid))
			{
				SendPetSpellFailure(*this, petGuid, request.spellId, spell_cast_result::FailedBadTargets);
				return;
			}
		}

		const SpellCastResult result = pet->CastSpell(request.targetMap, *spell, spell->casttime());
		if (result != spell_cast_result::CastOkay)
		{
			SendPetSpellFailure(*this, petGuid, request.spellId, result);
		}
	}

	void Player::OnPetRename(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		game::PetRenameRequest request;
		if (!(contentReader >> request))
		{
			ELOG("Failed to read PetRename packet");
			return;
		}

		// Validation, the stored name and the PetSpells / PetResult answer are GamePlayerS's.
		bool changed = false;
		if (m_character->RenamePersistentPet(request.name, &changed) != pet_result::Ok || !changed)
		{
			return;
		}

		// Clients that already know the pet's name learn the new one.
		const PetManager* pets = m_character->FindPets();
		GameCreatureS* pet = pets ? pets->GetPersistentPet() : nullptr;
		if (!pet)
		{
			return;
		}

		game::PetNameQueryResultData data;
		data.guid = pet->GetGuid();
		data.found = true;
		data.name = pet->GetName();

		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::OutgoingPacket packet(sink);
		packet.Start(game::realm_client_packet::PetNameQueryResult);
		packet << data;
		packet.Finish();

		pet->ForEachSubscriberInSight([&packet, &buffer](TileSubscriber& subscriber)
			{
				subscriber.SendPacket(packet, buffer);
			});
	}

	void Player::OnPetNameQuery(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		game::PetNameQueryRequest request;
		if (!(contentReader >> request))
		{
			ELOG("Failed to read PetNameQuery packet");
			return;
		}

		game::PetNameQueryResultData data;
		data.guid = request.guid;

		// Persistent pets only: a temporary pet keeps its creature entry's name.
		if (m_worldInstance && IsPetGUID(request.guid))
		{
			if (const auto* pet = m_worldInstance->FindByGuid<GameCreatureS>(request.guid); pet && pet->IsPet())
			{
				data.found = true;
				data.name = pet->GetName();
			}
		}

		SendPacket([&data](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::PetNameQueryResult);
				packet << data;
				packet.Finish();
			});
	}
}
