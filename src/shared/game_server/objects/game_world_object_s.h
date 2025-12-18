// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_object_s.h"
#include "game_server/loot_instance.h"

namespace mmo
{
	namespace world_object_flags
	{
		enum Type : uint32
		{
			/// No special flags.
			None = 0x00,
			/// Object can only be used when a specific quest is active.
			RequiresQuest = 0x01,
			/// Object is temporarily disabled (e.g., by server script).
			Disabled = 0x02,
		};
	}

	class GameWorldObjectS : public GameObjectS
	{
	public:
		explicit GameWorldObjectS(const proto::Project& project, const proto::ObjectEntry& entry);
		virtual ~GameWorldObjectS() override = default;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Object; }

	public:
		virtual void Initialize() override;

	public:
		GameWorldObjectType GetType() const { return static_cast<GameWorldObjectType>(Get<uint32>(object_fields::ObjectTypeId)); }

		/// @brief Checks if this object can be used by the given player.
		/// @param player The player attempting to use the object.
		/// @return true if the object is usable, false otherwise.
		bool IsUsable(const GamePlayerS& player) const;

		/// @brief Sets whether this object is currently enabled.
		/// @param enabled true to enable, false to disable.
		void SetEnabled(bool enabled);

		/// @brief Sets the quest requirement for using this object.
		/// @param questId The quest ID required to use this object, or 0 to remove requirement.
		void SetRequiredQuest(uint32 questId);

		void Use(GamePlayerS& player);

		const String& GetName() const override;

		bool HasMovementInfo() const override { return true; }

	public:
		/// @brief Gets the quest ID required to use this object.
		/// @return Quest ID from proto data or dynamically set value.
		uint32 GetRequiredQuestId() const
		{
			return m_requiredQuestId;
		}

	protected:
		void PrepareFieldMap() override;

		void OnLootClosed(uint64 lootGuid);

		void OnLootCleared();

	protected:
		const proto::ObjectEntry& m_entry;
		scoped_connection_container m_lootSignals;
		uint32 m_requiredQuestId = 0;
	};
}
