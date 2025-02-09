// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"

#include "player_manager.h"
#include "base/utilities.h"
#include "game_server/game_creature_s.h"
#include "game_server/game_object_s.h"
#include "game_server/game_player_s.h"
#include "proto_data/project.h"
#include "game/loot.h"

namespace mmo
{

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatCreateMonster(uint16 opCode, uint32 size, io::Reader& contentReader) const
	{
		uint32 entry;
		if (!(contentReader >> io::read<uint32>(entry)))
		{
			ELOG("Missing entry id to create a monster");
			return;
		}

		DLOG("Creating monster with entry " << entry);

		const auto* creatureEntry = m_project.units.getById(entry);

		// Spawn a new creature
		ASSERT(m_worldInstance);
		const auto spawned = m_worldInstance->CreateTemporaryCreature(*creatureEntry, m_character->GetPosition(), 0.0f, 50.0f);
		spawned->ClearFieldChanges();
		m_worldInstance->AddGameObject(*spawned);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatDestroyMonster(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 guid;
		if (!(contentReader >> io::read<uint64>(guid)))
		{
			ELOG("Missing guid to destroy a monster");
			return;
		}

		DLOG("Destroying monster with guid " << log_hex_digit(guid));

		// Find creature with guid
		GameObjectS* object = m_worldInstance->FindObjectByGuid(guid);
		if (object == nullptr)
		{
			ELOG("Unable to find object with guid " << log_hex_digit(guid) << " to destroy");
			return;
		}

		if (object->GetTypeId() != ObjectTypeId::Unit)
		{
			ELOG("Object with guid " << log_hex_digit(guid) << " is not a creature");
			return;
		}

		m_worldInstance->RemoveGameObject(*object);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatLearnSpell(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 spellId;
		if (!(contentReader >> io::read<uint32>(spellId)))
		{
			ELOG("Missing spell id to learn a spell");
			return;
		}

		// Find spell with entry
		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			ELOG("Unable to learn spell: Unknown spell " << spellId);
			return;
		}

		DLOG("Learning spell " << spellId << " (" << spell->name() << " [" << spell->rank() << "])");

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GameObjectS* targetObject = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!targetObject || targetObject->GetTypeId() != ObjectTypeId::Player)
		{
			targetObject = m_character.get();
		}

		auto playerCharacter = reinterpret_cast<GamePlayerS*>(targetObject);
		playerCharacter->AddSpell(spellId);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatFollowMe(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 guid;
		if (!(contentReader >> io::read<uint64>(guid)))
		{
			ELOG("Missing guid");
			return;
		}

		DLOG("Making Monster with guid " << log_hex_digit(guid) << " follow player");

		// Find creature with guid
		GameObjectS* object = m_worldInstance->FindObjectByGuid(guid);
		if (object == nullptr)
		{
			ELOG("Unable to find object with guid " << log_hex_digit(guid));
			return;
		}

		if (object->GetTypeId() != ObjectTypeId::Unit)
		{
			ELOG("Object with guid " << log_hex_digit(guid) << " is not a creature");
			return;
		}

		// Stop movement immediately
		GameUnitS* unit = reinterpret_cast<GameUnitS*>(object);
		unit->GetMover().StopMovement();

		// TODO
		DLOG("TODO");
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatFaceMe(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 guid;
		if (!(contentReader >> io::read<uint64>(guid)))
		{
			ELOG("Missing guid");
			return;
		}

		DLOG("Making Monster with guid " << log_hex_digit(guid) << " face player");

		// Find creature with guid
		GameObjectS* object = m_worldInstance->FindObjectByGuid(guid);
		if (object == nullptr)
		{
			ELOG("Unable to find object with guid " << log_hex_digit(guid));
			return;
		}

		if (object->GetTypeId() != ObjectTypeId::Unit)
		{
			ELOG("Object with guid " << log_hex_digit(guid) << " is not a creature");
			return;
		}

		// Stop movement immediately
		GameUnitS* unit = reinterpret_cast<GameUnitS*>(object);
		unit->GetMover().StopMovement();

		// TODO
		DLOG("TODO");
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatLevelUp(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 level;
		if (!(contentReader >> io::read<uint8>(level)))
		{
			ELOG("Missing level parameter!");
			return;
		}

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GamePlayerS* targetUnit = m_worldInstance->FindByGuid<GamePlayerS>(targetGuid);
		if (!targetUnit || targetUnit->GetTypeId() != ObjectTypeId::Player)
		{
			targetUnit = m_character.get();
		}

		if (!targetUnit)
		{
			ELOG("Unable to find target character!");
			return;
		}

		uint32 characterLevel = targetUnit->GetLevel();
		uint32 targetLevel = characterLevel + level;

		// Check for overflow
		if (characterLevel + level >= targetUnit->Get<uint32>(object_fields::MaxLevel))
		{
			targetLevel = targetUnit->Get<uint32>(object_fields::MaxLevel);
		}

		if (targetLevel == targetUnit->GetLevel())
		{
			ELOG("Character level is unchanged");
			return;
		}

		ASSERT(targetUnit->GetClassEntry());
		DLOG("Setting level of target to " << targetLevel);

		// Grant experience while level is not reached
		while (characterLevel < targetLevel)
		{
			targetUnit->RewardExperience(targetUnit->Get<uint32>(object_fields::NextLevelXp) - targetUnit->Get<uint32>(object_fields::Xp));
			ASSERT(targetUnit->GetLevel() > characterLevel);

			characterLevel = targetUnit->GetLevel();
		}

	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatGiveMoney(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 amount;
		if (!(contentReader >> io::read<uint32>(amount)))
		{
			ELOG("Missing amount parameter!");
			return;
		}

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GamePlayerS* targetPlayer = m_worldInstance->FindByGuid<GamePlayerS>(targetGuid);
		if (!targetPlayer)
		{
			targetPlayer = m_character.get();
		}

		if (!targetPlayer)
		{
			ELOG("Unable to find target character!");
			return;
		}

		uint32 money = targetPlayer->Get<uint32>(object_fields::Money);

		// Check for overflow
		if (money + amount <= money)
		{
			money = std::numeric_limits<uint32>::max();
		}
		else
		{
			money += amount;
		}

		DLOG("Setting money of target to " << money);
		targetPlayer->Set<uint32>(object_fields::Money, money);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatAddItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 itemId;
		uint8 count;
		if (!(contentReader >> io::read<uint32>(itemId) >> io::read<uint8>(count)))
		{
			ELOG("Failed to read CheatAddItem packet!");
			return;
		}

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GamePlayerS* targetPlayer = m_worldInstance->FindByGuid<GamePlayerS>(targetGuid);
		if (!targetPlayer)
		{
			targetPlayer = m_character.get();
		}

		if (!targetPlayer)
		{
			ELOG("Unable to find target character!");
			return;
		}

		if (count == 0)
		{
			count = 1;
		}

		const auto* itemEntry = m_project.items.getById(itemId);
		if (!itemEntry)
		{
			ELOG("Item with item id " << itemId << " does not exist!");
			return;
		}

		targetPlayer->GetInventory().CreateItems(*itemEntry, count);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatWorldPort(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 mapId;
		Vector3 position;
		float facingRadianVal;
		if (!(contentReader >> io::read<uint32>(mapId) >> io::read<float>(position.x) >> io::read<float>(position.y) >> io::read<float>(position.z) >> io::read<float>(facingRadianVal)))
		{
			ELOG("Failed to read CheatWorldPort packet!");
			return;
		}

		// Teleport the player
		m_character->Teleport(mapId, position, Radian(facingRadianVal));
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatSpeed(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		float speed;
		if (!(contentReader >> io::read<float>(speed)))
		{
			ELOG("Failed to read CheatSpeed packet!");
			return;
		}

		if (speed <= 0.0f || speed > 50.0f)
		{
			ELOG("Invalid speed value " << speed);
			return;
		}

		// TODO: Different movement types as well?
		DLOG("Setting base movement speed of player " << m_characterData.name << " to " << speed);
		m_character->SetBaseSpeed(movement_type::Run, speed);
	}
#endif
}
