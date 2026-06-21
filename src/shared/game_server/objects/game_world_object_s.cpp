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
		Set<uint32>(object_fields::ObjectTypeId, static_cast<uint32>(m_entry.type()));

		// Apply quest requirement from proto data
		if (m_entry.has_requiredquest() && m_entry.requiredquest() != 0)
		{
			SetRequiredQuest(m_entry.requiredquest());
		}

		// Initialize lock type from data[0] (0 = no lock)
		Set<uint32>(object_fields::LockEntry, m_entry.data_size() > 0 ? static_cast<uint32>(m_entry.data(0)) : 0u);
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
				const QuestStatus status = player.GetQuestStatus(requiredQuestId);

				// Quest is complete (all objectives done, not yet turned in) or already
				// rewarded — player no longer needs to interact with these objects.
				if (status == quest_status::Complete || status == quest_status::Rewarded)
				{
					return false;
				}

				// Quest not active at all — object is hidden.
				if (status != quest_status::Incomplete)
				{
					return false;
				}

				// Quest is in progress. Check if the specific object-use requirement is
				// already satisfied (player collected enough of this object entry).
				// If so, the object becomes non-interactable even though the overall quest
				// is still incomplete due to other open objectives.
				if (player.IsQuestObjectRequirementMet(requiredQuestId, m_entry.id()))
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

		switch (GetType())
		{
		case GameWorldObjectType::Chest:
			{
				// Guard against stub chests with no configured loot entry.
				// Use the per-spawn override if set, otherwise fall back to the base ObjectEntry's loot
				// tables. Loot tables act as reusable modules and multiple can be assigned to an object.
				// For backwards compatibility, an object that still uses the legacy single
				// 'objectlootentry' field is treated as having a single loot table.
				std::vector<const proto::LootEntry*> lootEntries;
				if (m_lootEntryOverride != 0)
				{
					if (const auto* loot = m_project.unitLoot.getById(m_lootEntryOverride))
					{
						lootEntries.push_back(loot);
					}
				}
				else if (m_entry.objectlootentries_size() > 0)
				{
					lootEntries.reserve(m_entry.objectlootentries_size());
					for (int i = 0; i < m_entry.objectlootentries_size(); ++i)
					{
						if (const auto* loot = m_project.unitLoot.getById(m_entry.objectlootentries(i)))
						{
							lootEntries.push_back(loot);
						}
					}
				}
				else if (const auto* legacyLoot = m_project.unitLoot.getById(m_entry.objectlootentry()))
				{
					lootEntries.push_back(legacyLoot);
				}

				if (lootEntries.empty())
				{
					DLOG("Chest has no loot entry configured - no loot window opened");
					return;
				}

				if (!m_loot)
				{
					ASSERT(m_worldInstance);

					const auto weakPlayer = std::weak_ptr(std::dynamic_pointer_cast<GamePlayerS>(player.shared_from_this()));
					m_loot = std::make_shared<LootInstance>(
						m_project.items, m_worldInstance->GetConditionMgr(), GetGuid(),
						lootEntries,
						std::vector{ weakPlayer });

					// TODO: wire trigger_id — fire a specific trigger on chest open when trigger_id != 0

					auto weakThis = std::weak_ptr(shared_from_this());
					m_lootSignals += m_loot->closed.connect(this, &GameWorldObjectS::OnLootClosed);
					m_lootSignals += m_loot->cleared.connect(this, &GameWorldObjectS::OnLootCleared);
				}

				player.LootObject(shared_from_this());
			}
			break;

		case GameWorldObjectType::Door:
			{
				// Toggle door open/closed using the State field (0=closed, 1=open)
				// Set<>() auto-broadcasts the change to all nearby players via AddObjectUpdate()
				const uint32 currentState = Get<uint32>(object_fields::State);
				const bool isOpen = (currentState == 1u);

				if (isOpen)
				{
					// Close the door
					Set<uint32>(object_fields::State, 0u);
				}
				else
				{
					// Open the door
					Set<uint32>(object_fields::State, 1u);
				}
			}
			break;

		default:
			WLOG("Player tried to use world object with unhandled type " << static_cast<uint32>(GetType()));
			break;
		}
	}

	uint32 GameWorldObjectS::GetPostUnlockLockType() const
	{
		if (GetType() == GameWorldObjectType::Door && m_entry.data_size() > 1)
			return static_cast<uint32>(m_entry.data(1));
		return 0u;
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
