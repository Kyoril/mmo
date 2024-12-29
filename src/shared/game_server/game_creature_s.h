
#pragma once

#include "creature_ai.h"
#include "game_player_s.h"
#include "game_unit_s.h"
#include "loot_instance.h"
#include "world_instance.h"
#include "base/linear_set.h"
#include "game/quest.h"

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

		/// Get unit loot.
		std::shared_ptr<LootInstance> getUnitLoot() const { return m_unitLoot; }

		void SetUnitLoot(std::unique_ptr<LootInstance> unitLoot);

		/// Gets the number of loot recipients.
		uint32 GetLootRecipientCount() const { return m_lootRecipients.size(); }

		QuestgiverStatus GetQuestGiverStatus(const GamePlayerS& player) const;

		bool ProvidesQuest(uint32 questId) const override;

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
				if (std::shared_ptr<GamePlayerS> character = std::dynamic_pointer_cast<GamePlayerS>(GetWorldInstance()->FindObjectByGuid(guid)->shared_from_this()))
				{
					callback(character);
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

		void RefreshStats() override;

		/// Executes a callback function for every valid loot recipient.
		template<typename OnParticipant>
		void ForEachCombatParticipant(OnParticipant callback)
		{
			if (!GetWorldInstance()) 
			{
				return;
			}

			for (auto& guid : m_combatParticipantGuids)
			{
				if (auto character = dynamic_cast<GamePlayerS*>(GetWorldInstance()->FindObjectByGuid(guid)))
				{
					callback(*character);
				}
			}
		}

	private:

		std::unique_ptr<CreatureAI> m_ai;
		const proto::UnitEntry& m_originalEntry;
		const proto::UnitEntry* m_entry;
		scoped_connection m_onSpawned;
		std::set<uint64> m_combatParticipantGuids;
		CreatureMovement m_movement;
		std::shared_ptr<LootInstance> m_unitLoot;
		LootRecipients m_lootRecipients;
	};
}
