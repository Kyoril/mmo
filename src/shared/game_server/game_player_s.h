#pragma once

#include <set>
#include <vector>

#include "game_unit_s.h"
#include "inventory.h"
#include "game/quest.h"

namespace mmo
{
	struct QuestStatusData;
	class GameItemS;

	namespace proto
	{
		class QuestEntry;
		class RaceEntry;
		class ClassEntry;
	}

	class NetPlayerWatcher : public NonCopyable
	{
	public:
		virtual ~NetPlayerWatcher() = default;

	public:
		virtual void OnQuestKillCredit(const proto::QuestEntry&, uint64 guid, uint32 entry, uint32 count, uint32 maxCount) = 0;

		virtual void OnQuestDataChanged(uint32 questId, const QuestStatusData& data) = 0;

		virtual void OnQuestCompleted(uint64 questgiverGuid, uint32 questId, uint32 rewardedXp, uint32 rewardMoney) = 0;
	};

	/// @brief Represents a playable character in the game world.
	class GamePlayerS final : public GameUnitS
	{
	public:

		signal<void(GameUnitS&, const proto::SpellEntry&)> spellLearned;
		signal<void(GameUnitS&, const proto::SpellEntry&)> spellUnlearned;

	public:
		GamePlayerS(const proto::Project& project, TimerQueue& timerQueue);

		~GamePlayerS() override = default;

		virtual void Initialize() override;

		void SetPlayerWatcher(NetPlayerWatcher* watcher);

		void SetClass(const proto::ClassEntry& classEntry);

		void SetRace(const proto::RaceEntry& raceEntry);

		void SetGender(uint8 gender);

		uint8 GetGender() const;

		void SetAttributeCost(uint32 attribute, uint8 cost);

		uint8 GetAttributeCost(uint32 attribute) const;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Player; }

		/// Applies or removes item stats for this character.
		void ApplyItemStats(GameItemS& item, bool apply);

		/// Gets a reference to the characters inventory component.
		Inventory& GetInventory() { return m_inventory; }

		const proto::ClassEntry* GetClassEntry() const { return m_classEntry; }

		const proto::RaceEntry* GetRaceEntry() const { return m_raceEntry; }

		bool AddAttributePoint(uint32 attribute);

		void ResetAttributePoints();

		static uint8 CalculateAttributeCost(uint32 pointsSpent);

		bool HasMoney(uint32 amount) const;

		bool ConsumeMoney(uint32 amount);

	public:

		/// Gets the current status of a given quest by its id.
		/// @returns Quest status.
		QuestStatus GetQuestStatus(uint32 quest) const;

		/// Accepts a new quest.
		/// @returns false if this wasn't possible (maybe questlog was full or not all requirements are met).
		bool AcceptQuest(uint32 quest);

		/// Abandons the specified quest.
		/// @returns false if this wasn't possible (maybe because the quest wasn't in the players quest log).
		bool AbandonQuest(uint32 quest);

		/// Updates the status of a specified quest to "completed". This does not work for quests that require
		/// a certain item.
		bool CompleteQuest(uint32 quest);

		/// Makes a certain quest fail.
		bool FailQuest(uint32 quest);

		/// Rewards the given quest (gives items, xp and saves quest status).
		bool RewardQuest(uint64 questgiverGuid, uint32 quest, uint8 rewardChoice);

		/// Called when a quest-related creature was killed.
		void OnQuestKillCredit(uint64 unitGuid, const proto::UnitEntry& entry);

		/// Determines whether the character fulfulls all requirements of the given quests.
		bool FulfillsQuestRequirements(const proto::QuestEntry& entry) const;

		/// Determines whether the players questlog is full.
		bool IsQuestlogFull() const;

		/// Called when an exploration area trigger was raised.
		void OnQuestExploration(uint32 questId);

		/// Called when a quest item was added to the inventory.
		void OnQuestItemAddedCredit(const proto::ItemEntry& entry, uint32 amount);

		/// Called when a quest item was removed from the inventory.
		void OnQuestItemRemovedCredit(const proto::ItemEntry& entry, uint32 amount);

		/// Called when a quest item was removed from the inventory.
		void OnQuestSpellCastCredit(uint32 spellId, GameObjectS& target);

		/// Determines if the player needs a specific item for a quest.
		bool NeedsQuestItem(uint32 itemId) const;

		void NotifyQuestRewarded(uint32 questId);

		void SetQuestData(uint32 questId, const QuestStatusData& data);

	protected:

		float GetUnitMissChance() const override;

		bool HasOffhandWeapon() const override;

	public:
		void RewardExperience(const uint32 xp);

		void RefreshStats() override;

		virtual void SetLevel(uint32 newLevel) override;

		void UpdateDamage();

		void UpdateArmor();

		void UpdateAttributePoints();

		uint32 GetAttributePointsByAttribute(const uint32 attribute) const { ASSERT(attribute < 5); return m_attributePointEnhancements[attribute]; }

	protected:
		void UpdateStat(int32 stat);

		void RecalculateTotalAttributePointsConsumed(const uint32 attribute);

	protected:
		void OnSpellLearned(const proto::SpellEntry& spell) override
		{
			spellLearned(*this, spell);
		}

		void OnSpellUnlearned(const proto::SpellEntry& spell) override
		{
			spellUnlearned(*this, spell);
		}

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::PlayerFieldCount);
		}

	private:
		Inventory m_inventory;
		const proto::ClassEntry* m_classEntry;
		const proto::RaceEntry* m_raceEntry;
		std::array<uint32, 5> m_attributePointEnhancements;
		std::array<uint32, 5> m_attributePointsSpent;
		uint32 m_totalAvailablePointsAtLevel;
		std::map<uint32, QuestStatusData> m_quests;
		std::set<uint32> m_rewardedQuestIds;
		NetPlayerWatcher* m_netPlayerWatcher = nullptr;

	private:
		friend io::Writer& operator << (io::Writer& w, GamePlayerS const& object);
		friend io::Reader& operator >> (io::Reader& r, GamePlayerS& object);
	};


	io::Writer& operator<<(io::Writer& w, GamePlayerS const& object);

	io::Reader& operator>> (io::Reader& r, GamePlayerS& object);
}
