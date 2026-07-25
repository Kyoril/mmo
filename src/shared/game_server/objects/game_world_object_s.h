// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_object_s.h"
#include "game_server/loot_instance.h"
#include "shared/proto_data/trigger_helper.h"

namespace mmo
{
	namespace proto
	{
		class TriggerEntry;
	}

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

	/// @brief Per-player dynamic flags computed by server.
	namespace dynamic_world_object_flags
	{
		enum Type : uint32
		{
			/// No special flags.
			None = 0x00,
			/// Object can be interacted with by this player.
			Interactable = 0x01,
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

		/// @brief Returns the lock type to apply after a one-time unlock succeeds.
		/// @return data[1] cast to uint32 if this is a Door with data_size > 1, otherwise 0.
		uint32 GetPostUnlockLockType() const;

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

		/// @brief Computes dynamic flags for a specific player.
		/// @param player The player to compute flags for.
		/// @return Dynamic flags value.
		uint32 GetDynamicFlags(const GamePlayerS& player) const;

		/// @brief Prepares per-player dynamic fields before serialization.
		/// @param player The player who will receive the field data.
		void PrepareDynamicFieldsFor(const GamePlayerS& player);

		/// @brief Clears temporary dynamic field values after serialization.
		void ClearDynamicFields();

		void Use(GamePlayerS& player);

		/// Fired when one of this object's triggers should be executed. Connected to the trigger
		/// handler by the world instance, mirroring GameUnitS::unitTrigger.
		signal<void(const proto::TriggerEntry&, GameWorldObjectS&, GameUnitS*)> objectTrigger;

		/// Raises all of this object's triggers (base entry triggers plus the per-spawn override)
		/// that listen to the given event.
		void RaiseTrigger(trigger_event::Type e, GameUnitS* triggeringUnit = nullptr);

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

		/// @brief Per-spawn loot entry override. 0 = use base ObjectEntry.objectlootentry.
		uint32 m_lootEntryOverride = 0;

		/// @brief Per-spawn additional trigger id. 0 = only the base ObjectEntry.triggers apply.
		uint32 m_triggerIdOverride = 0;

	public:
		/// @brief Sets a per-spawn loot entry override for this world object.
		/// @param lootEntry The loot entry ID to use, or 0 to fall back to the base entry.
		void SetLootEntryOverride(uint32 lootEntry) { m_lootEntryOverride = lootEntry; }

		/// @brief Sets a per-spawn additional trigger for this world object.
		/// @param triggerId The trigger ID to raise in addition to the base entry's triggers, or 0 for none.
		void SetTriggerIdOverride(uint32 triggerId) { m_triggerIdOverride = triggerId; }
	};
}
