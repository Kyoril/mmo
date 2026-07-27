// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"

#include "player_manager.h"
#include "base/utilities.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/objects/game_object_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "math/quaternion.h"
#include "game_server/world/world_instance.h"
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
	void Player::OnCheatCreateObject(uint16 opCode, uint32 size, io::Reader& contentReader) const
	{
		uint32 entry, state;
		if (!(contentReader >> io::read<uint32>(entry) >> io::read<uint32>(state)))
		{
			ELOG("Missing entry id or state to create a world object");
			return;
		}

		const auto* objectEntry = m_project.objects.getById(entry);
		if (!objectEntry)
		{
			ELOG("Unable to create object: Unknown object entry " << entry);
			return;
		}

		DLOG("Creating world object with entry " << entry << " and state " << state);

		ASSERT(m_worldInstance);
		const auto spawned = m_worldInstance->CreateTemporaryObject(*objectEntry, m_character->GetPosition());

		// Face the same direction as the spawning player (rotation lives in the fields, not
		// in the movement info).
		const Quaternion rotation(m_character->GetFacing(), Vector3::UnitY);
		spawned->Set<float>(object_fields::Scale, objectEntry->scale() > 0.0f ? objectEntry->scale() : 1.0f);
		spawned->Set<float>(object_fields::RotationW, rotation.w);
		spawned->Set<float>(object_fields::RotationX, rotation.x);
		spawned->Set<float>(object_fields::RotationY, rotation.y);
		spawned->Set<float>(object_fields::RotationZ, rotation.z);
		spawned->Set<uint32>(object_fields::State, state);

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

		auto* playerCharacter = dynamic_cast<GamePlayerS*>(targetObject);
		ASSERT(playerCharacter);
		playerCharacter->AddSpell(spellId);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatLearnEmote(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 emoteId;
		if (!(contentReader >> io::read<uint32>(emoteId)))
		{
			ELOG("Missing emote id to learn an emote");
			return;
		}

		// Find emote with entry
		const auto* emote = m_project.emotes.getById(emoteId);
		if (!emote)
		{
			ELOG("Unable to learn emote: Unknown emote " << emoteId);
			return;
		}

		DLOG("Learning emote " << emoteId << " (" << emote->name() << ")");

		// Learn the emote for the targeted player character (or ourself if no player is targeted)
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		GameObjectS* targetObject = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!targetObject || targetObject->GetTypeId() != ObjectTypeId::Player)
		{
			targetObject = m_character.get();
		}

		auto* playerCharacter = dynamic_cast<GamePlayerS*>(targetObject);
		ASSERT(playerCharacter);
		if (!playerCharacter->AddEmote(emoteId))
		{
			ELOG("Failed to learn emote " << emoteId);
		}
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

		GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
		ASSERT(unit);
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

		GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
		ASSERT(unit);
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
	void Player::OnCheatClassLevelUp(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 levels;
		if (!(contentReader >> io::read<uint8>(levels)))
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

		const proto::ClassEntry* classEntry = targetUnit->GetClassEntry();
		if (!classEntry)
		{
			ELOG("Target character has no active class!");
			return;
		}

		// A class without a class-level curve is frozen at class level 1 and can not be leveled.
		if (classEntry->classlevels_size() == 0)
		{
			ELOG("Class " << classEntry->name() << " has no class level curve and can not gain class levels");
			return;
		}

		const uint32 maxClassLevel = std::min<uint32>(classEntry->classlevels_size(), 255);

		uint32 classLevel = targetUnit->GetActiveClassLevel();
		uint32 targetLevel = classLevel + levels;

		// Check for overflow
		if (targetLevel >= maxClassLevel)
		{
			targetLevel = maxClassLevel;
		}

		if (targetLevel == classLevel)
		{
			ELOG("Class level is unchanged");
			return;
		}

		DLOG("Setting class level of target to " << targetLevel);

		// Grant class experience while the target class level is not reached
		while (classLevel < targetLevel)
		{
			targetUnit->RewardClassExperience(classEntry->classlevels(classLevel - 1).xptonextlevel() - targetUnit->GetActiveClassXp());
			ASSERT(targetUnit->GetActiveClassLevel() > classLevel);

			classLevel = targetUnit->GetActiveClassLevel();
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

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatMorph(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 displayId;
		if (!(contentReader >> io::read<uint32>(displayId)))
		{
			ELOG("Failed to read CheatMorph packet!");
			return;
		}

		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		GameObjectS* object = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!object)
		{
			ELOG("CheatMorph: target not found in world");
			return;
		}

		GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
		if (!unit)
		{
			ELOG("CheatMorph: target is not a unit");
			return;
		}

		DLOG("Morphing unit " << log_hex_digit(targetGuid) << " to display id " << displayId);
		unit->Set<uint32>(object_fields::DisplayId, displayId);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatKill(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		GameObjectS* object = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!object)
		{
			ELOG("CheatKill: target not found in world");
			return;
		}

		GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
		if (!unit)
		{
			ELOG("CheatKill: target is not a unit");
			return;
		}

		if (!unit->IsAlive())
		{
			ELOG("CheatKill: target is already dead");
			return;
		}

		DLOG("GM kill on unit " << log_hex_digit(targetGuid));

		// Tag untagged creatures for the GM character so the kill grants xp and quest kill credit,
		// just as if the character had opened combat before the kill.
		if (GameCreatureS* creature = dynamic_cast<GameCreatureS*>(unit); creature != nullptr && !creature->IsTagged())
		{
			creature->AddLootRecipient(m_character->GetGuid());
		}

		unit->Kill(m_character.get());
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatRevive(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		GameObjectS* object = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!object)
		{
			ELOG("CheatRevive: target not found in world");
			return;
		}

		GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
		if (!unit)
		{
			ELOG("CheatRevive: target is not a unit");
			return;
		}

		if (unit->IsAlive())
		{
			ELOG("CheatRevive: target is not dead");
			return;
		}

		DLOG("GM revive on unit " << log_hex_digit(targetGuid));
		unit->Set<uint32>(object_fields::Health, unit->Get<uint32>(object_fields::MaxHealth));
		if (unit->Get<uint32>(object_fields::MaxMana) > 1)
		{
			unit->Set<uint32>(object_fields::Mana, unit->Get<uint32>(object_fields::MaxMana));
		}
		unit->StartRegeneration();
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatCheckLineOfSight(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 targetGuid = 0;
		if (!(contentReader >> io::read<uint64>(targetGuid)))
		{
			ELOG("Failed to read CheatCheckLineOfSight packet!");
			return;
		}

		if (!m_worldInstance)
		{
			return;
		}

		const Vector3 from = m_character->GetPosition();
		Vector3 to = from;
		Vector3 hitPoint = from;

		const GameObjectS* target = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!target)
		{
			ELOG("CheatCheckLineOfSight: target guid " << log_hex_digit(targetGuid) << " not found in world");
			return;
		}

		to = target->GetPosition();
		hitPoint = to;

		bool hasLos = true;
		MapData* mapData = m_worldInstance->GetMapData();
		if (mapData)
		{
			hasLos = mapData->IsInLineOfSightEx(from, to, hitPoint);
		}

		DLOG("LOS check from " << m_characterData.name << " to guid " << log_hex_digit(targetGuid)
			<< ": " << (hasLos ? "CLEAR" : "BLOCKED"));

		SendPacket([&](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::DebugLineOfSightResult);
			packet
				<< io::write<uint8>(hasLos ? 1 : 0)
				<< io::write<float>(from.x)     << io::write<float>(from.y)     << io::write<float>(from.z)
				<< io::write<float>(to.x)       << io::write<float>(to.y)       << io::write<float>(to.z)
				<< io::write<float>(hitPoint.x) << io::write<float>(hitPoint.y) << io::write<float>(hitPoint.z);
			packet.Finish();
		});
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatAcceptQuest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 questId = 0;
		if (!(contentReader >> io::read<uint32>(questId)))
		{
			ELOG("Failed to read CheatAcceptQuest packet!");
			return;
		}

		const proto::QuestEntry* quest = m_project.quests.getById(questId);
		if (!quest)
		{
			ELOG("CheatAcceptQuest: unknown quest id " << questId);
			return;
		}

		if (!AcceptQuestAndNotify(questId, *quest))
		{
			return;
		}

		DLOG("GM accepted quest " << questId << " for player " << m_characterData.name);

		RefreshQuestObjectInteractability(questId);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatTurnInQuest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 questId = 0;
		uint8 rewardChoice = 0;
		if (!(contentReader >> io::read<uint32>(questId) >> io::read<uint8>(rewardChoice)))
		{
			ELOG("Failed to read CheatTurnInQuest packet!");
			return;
		}

		const proto::QuestEntry* quest = m_project.quests.getById(questId);
		if (!quest)
		{
			ELOG("CheatTurnInQuest: unknown quest id " << questId);
			return;
		}

		// The reward path requires the quest to be in the Complete state; there is no quest
		// ender involved, so the completion notification carries the players own guid.
		if (!m_character->RewardQuest(m_character->GetGuid(), questId, rewardChoice))
		{
			ELOG("CheatTurnInQuest: failed to turn in quest " << questId << " (status " <<
				static_cast<uint32>(m_character->GetQuestStatus(questId)) << ", reward choice " <<
				static_cast<uint32>(rewardChoice) << " of " << quest->rewarditemschoice_size() << ")");
			return;
		}

		DLOG("GM turned in quest " << questId << " for player " << m_characterData.name);

		RefreshQuestObjectInteractability(questId);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatClearInventory(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		auto& inventory = m_character->GetInventory();

		uint32 removed = 0;
		for (uint8 slot = player_inventory_pack_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			const uint16 absoluteSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot).GetAbsolute();
			if (!inventory.GetItemAtSlot(absoluteSlot))
			{
				continue;
			}

			if (inventory.RemoveItem(absoluteSlot) == inventory_change_failure::Okay)
			{
				++removed;
			}
		}

		DLOG("GM cleared inventory of player " << m_characterData.name << " (" << removed << " backpack stacks removed)");
	}
#endif
}
