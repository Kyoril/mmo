// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_world_object_s.h"

#include "game_player_s.h"
#include "shared/proto_data/objects.pb.h"
#include "game/quest.h"

namespace mmo
{
	GameWorldObjectS::GameWorldObjectS(const proto::Project& project, const proto::ObjectEntry& entry)
		: GameObjectS(project)
		, m_entry(entry)
	{
	}

	void GameWorldObjectS::Initialize()
	{
		GameObjectS::Initialize();

		Set<uint32>(object_fields::Entry, m_entry.id());
		Set<uint32>(object_fields::ObjectDisplayId, m_entry.displayid());
		Set<uint32>(object_fields::ObjectTypeId, static_cast<uint32>(GameWorldObjectType::Chest));	// TODO

		// Apply quest requirement from proto data
		if (m_entry.has_requiredquest() && m_entry.requiredquest() != 0)
		{
			SetRequiredQuest(m_entry.requiredquest());
		}
	}

	bool GameWorldObjectS::IsUsable(const GamePlayerS& player) const
	{
		// Check if object is disabled by server
		const uint32 flags = Get<uint32>(object_fields::ObjectFlags);
		if (flags & world_object_flags::Disabled)
		{
			return false;
		}

		// Check if object requires a specific quest
		if (flags & world_object_flags::RequiresQuest)
		{
			const uint32 requiredQuestId = GetRequiredQuestId();
			if (requiredQuestId != 0)
			{
				// Player must have this quest active (incomplete status)
				const QuestStatus status = player.GetQuestStatus(requiredQuestId);
				if (status != quest_status::Incomplete)
				{
					return false;
				}
			}
		}

		// Type-specific checks could be added here if needed
		// For example, checking if it's a chest type, door type, etc.

		return true;
	}

	void GameWorldObjectS::Use(GamePlayerS& player)
	{
		// Validate that the player can use this object
		if (!IsUsable(player))
		{
			WLOG("Player tried to use object that is not usable");
			return;
		}

		if (!m_loot)
		{
			ASSERT(m_worldInstance);

			const auto weakPlayer = std::weak_ptr(std::dynamic_pointer_cast<GamePlayerS>(player.shared_from_this()));
			m_loot = std::make_shared<LootInstance>(m_project.items, m_worldInstance->GetConditionMgr(), GetGuid(), m_project.unitLoot.getById(m_entry.objectlootentry()), 0, 0, std::vector{ weakPlayer });

			auto weakThis = std::weak_ptr(shared_from_this());
			m_lootSignals += m_loot->closed.connect(this, &GameWorldObjectS::OnLootClosed);
			m_lootSignals += m_loot->cleared.connect(this, &GameWorldObjectS::OnLootCleared);
		}

		player.LootObject(shared_from_this());
	}

	const String& GameWorldObjectS::GetName() const
	{
		return m_entry.name();
	}

	void GameWorldObjectS::PrepareFieldMap()
	{
		m_fields.Initialize(object_fields::WorldObjectFieldCount);
	}

	void GameWorldObjectS::OnLootClosed(uint64 lootGuid)
	{
		if (m_loot->IsEmpty())
		{
			Despawn();
		}
	}

	void GameWorldObjectS::OnLootCleared()
	{
		// TODO: should we really despawn the entire object?
		Despawn();
	}

	void GameWorldObjectS::SetEnabled(bool enabled)
	{
		uint32 flags = Get<uint32>(object_fields::ObjectFlags);

		if (enabled)
		{
			flags &= ~world_object_flags::Disabled;
		}
		else
		{
			flags |= world_object_flags::Disabled;
		}

		Set<uint32>(object_fields::ObjectFlags, flags);
	}

	void GameWorldObjectS::SetRequiredQuest(uint32 questId)
	{
		m_requiredQuestId = questId;

		uint32 flags = Get<uint32>(object_fields::ObjectFlags);

		if (questId != 0)
		{
			flags |= world_object_flags::RequiresQuest;
		}
		else
		{
			flags &= ~world_object_flags::RequiresQuest;
		}

		Set<uint32>(object_fields::ObjectFlags, flags);
	}

	uint32 GameWorldObjectS::GetDynamicFlags(const GamePlayerS& player) const
	{
		uint32 dynamicFlags = dynamic_world_object_flags::None;

		// Determine if object is interactable for this specific player
		if (IsUsable(player))
		{
			dynamicFlags |= dynamic_world_object_flags::Interactable;
		}

		return dynamicFlags;
	}

	void GameWorldObjectS::PrepareDynamicFieldsFor(const GamePlayerS& player)
	{
		// Compute and set dynamic flags for this specific player
		const uint32 dynamicFlags = GetDynamicFlags(player);
		Set<uint32>(object_fields::DynamicObjectFlags, dynamicFlags);
	}

	void GameWorldObjectS::ClearDynamicFields()
	{
		// Reset dynamic flags to default value
		Set<uint32>(object_fields::DynamicObjectFlags, dynamic_world_object_flags::None);
	}
}
