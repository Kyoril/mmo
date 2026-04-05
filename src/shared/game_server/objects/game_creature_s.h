// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_unit_s.h"
#include "game_server/loot_instance.h"
#include "game_server/world/world_instance.h"
#include "base/linear_set.h"
#include "game/quest.h"
#include <vector>

namespace mmo
{
	namespace proto
	{
		class UnitEntry;
		class ItemEntry;
	}

	namespace creature_movement
	{
		enum Type
		{
			None = 0,
			Random = 1,
			Waypoints = 2,

			Invalid,
			Count_ = Invalid
		};
	}

	typedef creature_movement::Type CreatureMovement;

	/// Represents an AI controlled creature unit in the game.
	class GameCreatureS final : public GameUnitS
	{
	public:
		struct PatrolWaypoint final
		{
			Vector3 position;
			uint32 waitTime = 0;
		};

		typedef LinearSet<uint64> LootRecipients;
		typedef std::function<const Vector3& ()> RandomPointProc;

	public:

		/// Executed when the unit entry was changed after this creature has spawned. This
		/// can happen if the unit transforms.
		signal<void()> entryChanged;

	public:

		/// Creates a new instance of the GameCreature class.
		explicit GameCreatureS(
			const proto::Project& project,
			TimerQueue& timers,
			const proto::UnitEntry& entry);

		/// Destructor - properly cleans up AI and connections
		virtual ~GameCreatureS() override;

		virtual void Initialize() override;

		ObjectTypeId GetTypeId() const override {
			return ObjectTypeId::Unit;
		}

		/// Gets the original unit entry (the one, this creature was spawned with).
		/// This is useful for restoring the original creature state.
		const proto::UnitEntry& GetOriginalEntry() const {
			return m_originalEntry;
		}

		/// Gets the unit entry on which base this creature has been created.
		const proto::UnitEntry& GetEntry() const {
			return *m_entry;
		}

		void Relocate(const Vector3& position, const Radian& facing) override;

		/// Changes the creatures entry index. Remember, that the creature always has to
		/// have a valid base entry.
		void SetEntry(const proto::UnitEntry& entry);

		/// Updates the creatures loot recipient. Values of 0 mean no recipient.
		void AddLootRecipient(uint64 guid);

		/// Removes all loot recipients.
		void RemoveLootRecipients();

		/// Determines whether a specific character is allowed to loot this creature.
		bool IsLootRecipient(const GamePlayerS& character) const;

		/// Determines whether this creature is tagged by a player or group.
		bool IsTagged() const { return !m_lootRecipients.empty(); }

		void SetHealthPercent(float percent);

		void SetUnitLoot(std::unique_ptr<LootInstance> unitLoot);

		/// Gets the number of loot recipients.
		uint32 GetLootRecipientCount() const { return m_lootRecipients.size(); }

		QuestgiverStatus GetQuestGiverStatus(const GamePlayerS& player) const;

		bool ProvidesQuest(uint32 questId) const override;

		bool EndsQuest(uint32 questId) const override;

		///
		virtual void RaiseTrigger(trigger_event::Type e, GameUnitS* triggeringUnit = nullptr);

		///
		virtual void RaiseTrigger(trigger_event::Type e, const std::vector<uint32>& data, GameUnitS* triggeringUnit = nullptr);

		/// Executes a callback function for every valid loot recipient.
		template<typename OnRecipient>
		void ForEachLootRecipient(OnRecipient callback)
		{
			if (!GetWorldInstance()) 
			{
				return;
			}

			for (auto& guid : m_lootRecipients)
			{
				if (auto object = GetWorldInstance()->FindObjectByGuid(guid))
				{
					if (std::shared_ptr<GamePlayerS> character = std::dynamic_pointer_cast<GamePlayerS>(object->shared_from_this()))
					{
						callback(character);
					}
				}
			}
		}

		void AddCombatParticipant(const GameUnitS& unit);

		void RemoveCombatParticipant(uint64 unitGuid);

		bool HasCombatParticipants() const { return !m_combatParticipantGuids.empty(); }

		void RemoveAllCombatParticipants()
		{
			m_combatParticipantGuids.clear();
		}

		CreatureMovement GetMovementType() const { return m_movement; }

		void SetMovementType(CreatureMovement movementType);

		/// @brief Replaces the creature's patrol waypoint list.
		/// @param patrolWaypoints Ordered list of patrol waypoints to follow while idle.
		void SetPatrolWaypoints(std::vector<PatrolWaypoint> patrolWaypoints);

		/// @brief Returns the patrol waypoint list.
		/// @return Ordered list of patrol waypoints configured for this creature.
		[[nodiscard]] const std::vector<PatrolWaypoint>& GetPatrolWaypoints() const { return m_patrolWaypoints; }

		/// @brief Returns whether this creature has patrol waypoints configured.
		/// @return True if at least one patrol waypoint is available.
		[[nodiscard]] bool HasPatrolWaypoints() const { return !m_patrolWaypoints.empty(); }

		/// @brief Returns whether combat movement is enabled.
		/// @return True if combat movement is enabled, false otherwise.
		bool IsCombatMovementEnabled() const { return m_combatMovementEnabled; }

		/// @brief Sets whether combat movement is enabled.
		/// @param enabled True to enable combat movement, false to disable.
		void SetCombatMovement(bool enabled) { m_combatMovementEnabled = enabled; }

		/// @brief Returns the creature's AI controller.
		/// @return Pointer to the AI, or nullptr if not initialized.
		CreatureAI* GetAI() const { return m_ai.get(); }

		/// @brief Sets the creature's AI combat phase via the active combat script.
		/// @param phase The phase number to set.
		/// @return True if the phase was set successfully, false if the creature has no combat script.
		bool SetCombatPhase(uint32 phase);

		void RefreshStats() override;

		/// Executes a callback function for every valid combat participant.
		template<typename OnParticipant>
		void ForEachCombatParticipant(OnParticipant callback)
		{
			if (!GetWorldInstance()) 
			{
				return;
			}

			for (auto& guid : m_combatParticipantGuids)
			{
				if (auto object = GetWorldInstance()->FindObjectByGuid(guid))
				{
					if (auto character = dynamic_cast<GamePlayerS*>(object))
					{
						callback(*character);
					}
				}
			}
		}

		const String& GetName() const override;

	protected:
		/// @brief Returns the auto-attack spell configured for this creature, if any.
		/// @return Pointer to the auto-attack spell entry, or nullptr if not configured.
		const proto::SpellEntry* GetAutoAttackSpell() const override;

	private:

		/// Calculates and applies stats using the new stat-based system
		void CalculateStatBasedStats();

		std::unique_ptr<CreatureAI> m_ai;
		const proto::UnitEntry& m_originalEntry;
		const proto::UnitEntry* m_entry;
		scoped_connection m_onSpawned;
		std::set<uint64> m_combatParticipantGuids;
		CreatureMovement m_movement = creature_movement::None;
		std::vector<PatrolWaypoint> m_patrolWaypoints;
		LootRecipients m_lootRecipients;
		float m_healthPercent = 1.0f;
		bool m_combatMovementEnabled = true;
	};
}
