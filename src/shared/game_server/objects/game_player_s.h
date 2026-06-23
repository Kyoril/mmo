// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>
#include <vector>
#include <map>
#include <memory>

#include "game_unit_s.h"
#include "game_server/inventory.h"
#include "game_server/character_data.h"
#include "game_server/quest_status_data.h"
#include "game/quest.h"
#include "game/group.h"
#include "game/character_customization/customizable_avatar_definition.h"

namespace mmo
{
	struct QuestStatusData;
	class GameItemS;
		class PlayerGroup;

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
		virtual void OnQuestKillCredit(const proto::QuestEntry &, uint64 guid, uint32 entry, uint32 count, uint32 maxCount) = 0;

		virtual void OnQuestItemCredit(const proto::QuestEntry &, uint32 entry, uint32 count, uint32 maxCount) = 0;

		/// Called when a specific object-use requirement of a quest has been fully satisfied.
		/// Implementations should refresh the interactability of nearby world objects gated on
		/// this quest so the client stops showing the use cursor.
		virtual void OnQuestObjectRequirementMet(uint32 questId) = 0;

		virtual void OnQuestDataChanged(uint32 questId, const QuestStatusData &data) = 0;

		virtual void OnQuestCompleted(uint64 questgiverGuid, uint32 questId, uint32 rewardedXp, uint32 rewardMoney) = 0;

		virtual void OnItemAdded(uint16 slot, uint16 amount, bool wasLooted, bool wasCreated) = 0;

		virtual void OnObjectLoot() = 0;
	};

	/// @brief Represents a playable character in the game world.
	class GamePlayerS final
		: public GameUnitS
	{
	public:
		signal<void(GameUnitS &, const proto::SpellEntry &)> spellLearned;
		signal<void(GameUnitS &, const proto::SpellEntry &)> spellUnlearned;

		/// Fired when the character's spent talent points have been refunded (talents reset).
		signal<void()> talentsReset;
		/// Fired after the player's active class has been switched (ChangeClass succeeded).
		signal<void()> classChanged;
		/// Fired when the set of known classes changes without an active-class switch — e.g. learning a
		/// class-change spell registers that class at level 1. Used to refresh the client's class list.
		signal<void()> knownClassesChanged;
		/// Fired when the character's spent attribute points have been refunded.
		signal<void()> attributePointsReset;

	public:
		GamePlayerS(const proto::Project &project, TimerQueue &timerQueue);

		~GamePlayerS() override = default;

		virtual void Initialize() override;

		void SetConfiguration(const AvatarConfiguration &configuration);

		void SetPlayerWatcher(NetPlayerWatcher *watcher);

		void SetClass(const proto::ClassEntry &classEntry);

		/// Replaces the full set of classes this character has learned. Each entry carries its own
		/// class level, attribute-point spending profile and talent ranks. Must be called before the
		/// active class is applied so per-class data (e.g. talent point budget) resolves correctly.
		void SetKnownClasses(const std::vector<CharacterClassData>& knownClasses) { m_knownClasses = knownClasses; }

		/// Returns a snapshot of all known classes. The active class entry is refreshed from the live
		/// talents and attribute spending so the result is safe to persist / transfer.
		[[nodiscard]] std::vector<CharacterClassData> GetKnownClasses() const;

		/// Returns the class level of the currently active class (defaults to 1).
		[[nodiscard]] uint32 GetActiveClassLevel() const;

		/// Switches the active class to the given class. If the class is not yet known it is added at
		/// class level 1 (subject to race legality). Swaps the class-bound spellbook, talents and the
		/// attribute-spending profile, then refreshes stats. Fails while in combat or if the class is
		/// unknown / not allowed for the character's race.
		/// @return true on a successful switch.
		bool ChangeClass(uint32 classId);

		/// Returns true while a class switch is being applied. Used to suppress the per-spell learn/
		/// unlearn and talent-reset notifications that would otherwise spam the client during a switch.
		[[nodiscard]] bool IsClassSwitchInProgress() const { return m_classSwitchInProgress; }

		void SetRace(const proto::RaceEntry &raceEntry);

		void SetGender(uint8 gender);

		uint8 GetGender() const;

		void SetAttributeCost(uint32 attribute, uint8 cost);

		uint8 GetAttributeCost(uint32 attribute) const;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Player; }

		/// Applies or removes item stats for this character.
		void ApplyItemStats(const GameItemS &item, bool apply);

		/// Gets a reference to the characters inventory component.
		Inventory &GetInventory() { return m_inventory; }

		const Inventory &GetInventory() const { return m_inventory; }

		const proto::ClassEntry *GetClassEntry() const { return m_classEntry; }

		const proto::RaceEntry *GetRaceEntry() const { return m_raceEntry; }

		bool AddAttributePoint(uint32 attribute);

		void ResetAttributePoints();

		void ResetTalents();

		static uint8 CalculateAttributeCost(uint32 pointsSpent);

		bool HasMoney(uint32 amount) const;

		bool ConsumeMoney(uint32 amount);

		void AddMoney(uint32 amount);

		/// Gets the characters group id.
		uint64 GetGroupId() const { return m_groupId; }

		/// Sets the characters group id.
		void SetGroupId(uint64 groupId) { m_groupId = groupId; }

		/// Gets the player's world-side group object.
		/// @returns The synchronized world-side group object, or nullptr if the player is not in a group.
		[[nodiscard]] PlayerGroup* GetGroup() const noexcept { return m_playerGroup; }

		/// Sets the player's world-side group object.
		/// @param group The synchronized world-side group object, or nullptr if the player left the group.
		void SetGroup(PlayerGroup* group) noexcept { m_playerGroup = group; }

		/// Gets the loot method for this player's group.
		[[nodiscard]] LootMethod GetLootMethod() const noexcept { return m_lootMethod; }

		/// Sets the loot method.
		void SetLootMethod(const LootMethod method) noexcept { m_lootMethod = method; }

		/// Gets the loot quality threshold mirrored onto the player.
		[[nodiscard]] uint8 GetLootThreshold() const noexcept { return m_lootThreshold; }

		/// Sets the loot quality threshold mirrored onto the player.
		void SetLootThreshold(const uint8 threshold) noexcept { m_lootThreshold = threshold; }

		/// Gets the loot master GUID for this player's group.
		[[nodiscard]] uint64 GetLootMasterGuid() const noexcept { return m_lootMasterGuid; }

		/// Sets the loot master GUID.
		void SetLootMasterGuid(const uint64 guid) noexcept { m_lootMasterGuid = guid; }
		void WriteObjectUpdateBlock(io::Writer &writer, bool creation = true) const override;

		void RaiseTrigger(trigger_event::Type e, const std::vector<uint32> &data, GameUnitS *triggeringUnit) override;

		void RaiseTrigger(trigger_event::Type e, GameUnitS *triggeringUnit) override;

		void OnItemAdded(uint16 slot, uint16 amount, bool wasLooted, bool wasCreated);

		void LootObject(std::weak_ptr<GameObjectS> lootObject);

		void InitializeTalents(const std::map<uint32, uint8> &talentRanks);

		/// Sets the full set of spells this character has learned across all classes. Only spells usable
		/// by the active race/class are activated (added to the live, castable spellbook); the rest are
		/// remembered so they are preserved and re-activated when switching back to their class. Talent
		/// spells are excluded (they are restored from talent data).
		void SetKnownSpells(const std::vector<uint32>& spellIds);

		/// Returns every spell the character has learned across all classes (the persistence set).
		[[nodiscard]] const std::set<uint32>& GetKnownSpellIds() const { return m_knownSpellIds; }

	public:
		/// Gets the current status of a given quest by its id.
		/// @returns Quest status.
		QuestStatus GetQuestStatus(uint32 quest) const;

		/// Returns true if the player has already met all object-use requirements for the given
		/// object entry in the given quest. Used by GameWorldObjectS::IsUsable to hide the
		/// interact cursor once the player no longer needs to use this particular object.
		bool IsQuestObjectRequirementMet(uint32 questId, uint32 objectEntryId) const;

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
		void OnQuestKillCredit(uint64 unitGuid, const proto::UnitEntry &entry);

		/// Determines whether the character fulfulls all requirements of the given quests.
		bool FulfillsQuestRequirements(const proto::QuestEntry &entry) const;

		/// Determines whether the players questlog is full.
		bool IsQuestlogFull() const;

		/// Called when an exploration area trigger was raised.
		void OnQuestExploration(uint32 questId);

		/// Called when a quest item was added to the inventory.
		void OnQuestItemAddedCredit(const proto::ItemEntry &entry, uint32 amount);

		/// Called when a quest item was removed from the inventory.
		void OnQuestItemRemovedCredit(const proto::ItemEntry &entry, uint32 amount);

		/// Called when a quest item was removed from the inventory.
		void OnQuestSpellCastCredit(uint32 spellId, GameObjectS &target);

		/// Determines if the player needs a specific item for a quest.
		bool NeedsQuestItem(uint32 itemId) const;

		void NotifyQuestRewarded(uint32 questId);

		/// Marks a daily/weekly quest as rewarded with a pending reset time. Called when character
		/// data is loaded so that interval-repeatable quests stay locked until their next reset.
		/// @param questId The quest id.
		/// @param resetTime The unix timestamp (seconds) at which the quest becomes available again.
		void NotifyRepeatableQuestReset(uint32 questId, GameTime resetTime);

		/// (Re-)arms the fail countdowns of all timed quests in the quest log. Quests whose deadline
		/// already passed (e.g. while the player was offline) are failed immediately. Must be called
		/// after the quest status data has been applied on login.
		void InitializeQuestTimers();

		void SetQuestData(uint32 questId, const QuestStatusData &data);

		/// Tries to learn a given rank of a talent.
		///
		/// This will fail if the talent is already known, a higher or even rank is already known or the talent
		/// does not exist or cant be learned by the current player class OR if any other requirement is not met,
		/// like having enough talent points available to spend.
		///
		/// @param talentId Id of the talent to learn.
		/// @param rank Rank to learn. May jump directly to a higher rank (e.g. when restoring saved
		///        talents); the talent point cost scales accordingly.
		/// @param enforceRequirements When true (gameplay), prerequisites and required-points-in-tab
		///        gates are validated. When false (restoring already-valid saved data on login), those
		///        gameplay gates are skipped while existence, rank validity and point budget still apply.
		/// @return true if the talent was learned, false otherwise.
		bool LearnTalent(uint32 talentId, uint32 rank, bool enforceRequirements = true);

		/// Determines if a rank of specific talent has been learned. If rank is 0 it basically checks if
		/// the given talent is learned at all.
		///
		/// @param talentId Id of the talent to search.
		/// @param rank Min rank to check against.
		/// @returns true if at least the given rank is learned. Returns false if not or the talent is not known at all.
		bool HasTalent(uint32 talentId, uint32 rank = 0) const;

		/// Returns the current rank of the given talent id.
		///
		/// CAREFUL: This returns 0 if the talent is not known. So you can NOT use this function to
		/// check if a talent is learned at all. For this check, use the HasTalent function instead!
		///
		/// @param talentId Id of the talent to look for.
		/// @returns rank of the talent. Also returns 0 if talent is not known or at first rank.
		uint32 GetTalentRank(uint32 talentId) const;

		/// Returns the amount of talent points spent in total.
		///
		/// @return Number of talent points spent in total.
		uint32 GetTalentPointsSpent() const;
		/// Gets the amount of available talent points to spend.
		uint32 GetAvailableTalentPoints() const;
		/// Gets the complete talent map for serialization.
		///
		/// @return Reference to the talent map containing talent IDs and their ranks.
		const std::unordered_map<uint32, uint32> &GetTalents() const { return m_talents; }

		/// Gets the persistent auras read from a serialized character (only populated when this
		/// object was reconstructed via operator>>, e.g. on the realm server).
		const std::vector<PersistentAuraData>& GetDeserializedAuras() const { return m_deserializedAuras; }

		/// Gets the persistent cooldowns read from a serialized character (only populated when this
		/// object was reconstructed via operator>>, e.g. on the realm server).
		const std::vector<PersistentCooldownData>& GetDeserializedCooldowns() const { return m_deserializedCooldowns; }

	protected:
		/// @brief Returns the auto-attack spell configured for this player's class and weapon hand, if any.
		/// @param attackType Which weapon hand the swing originates from.
		/// @return Pointer to the auto-attack spell entry, or nullptr if not configured.
		const proto::SpellEntry* GetAutoAttackSpell(WeaponAttack attackType) const override;

		/// @brief Sources the weapon damage range from the equipped weapon for the given hand.
		void GetAutoAttackDamageRange(WeaponAttack attackType, float& outMin, float& outMax) const override;

		/// @brief Returns the equipped weapon's swing time for the given hand.
		uint32 GetAutoAttackTime(WeaponAttack attackType) const override;

		float GetUnitMissChance() const override;

		bool HasOffhandWeapon() const override;

	public:
		void RewardExperience(const uint32 xp);

		void RefreshStats() override;

		virtual void SetLevel(uint32 newLevel) override;

		void UpdateDamage();

		void UpdateArmor();

		/// @copydoc GameUnitS::GetBlockValue
		/// Adds per-class attribute scaling (blockvaluestatsources) on top of item block value.
		float GetBlockValue() const override;

		/// @copydoc GameUnitS::CriticalBlockChance
		/// Adds per-class attribute scaling (critblockchancestatsources) on top of the base chance.
		float CriticalBlockChance() const override;

		void UpdateAttributePoints();

		void UpdateTalentPoints();

		/// Recomputes the total attribute points available at the current character level for the
		/// active class. Character-level driven (character-wide total).
		void UpdateTotalAttributePoints();

		/// Recomputes the total talent points available for the active class from its class level.
		void UpdateTotalTalentPoints();

		uint32 GetAttributePointsByAttribute(const uint32 attribute) const
		{
			ASSERT(attribute < 5);
			return m_attributePointEnhancements[attribute];
		}

		const String &GetName() const override;

		/// @brief Sets the player's name.
		/// @param name The name to set.
		void SetName(const String& name) { m_name = name; }

		std::shared_ptr<GameObjectS> GetLootObject() const { return m_lootObject.lock(); }

		bool IsGameMaster() const override;

		void SetIsGameMaster(bool isGameMaster);

		/// @brief Applies movement information and performs height safety checks.
		/// @param info The movement information to apply.
		void ApplyMovementInfo(const MovementInfo &info) override;

	protected:
		void UpdateStat(int32 stat);

		void RecalculateTotalAttributePointsConsumed(const uint32 attribute);

		/// Grants all of the active class's spells whose required level is met by the character level.
		void GrantClassSpells();

		/// Applies an attribute-point spending profile by replaying point allocations from a clean slate.
		void ApplyAttributeSpending(const std::array<uint32, 5>& enhancements);

		/// Writes the live talents and attribute spending back into the active class's known-class entry.
		void SyncActiveClassData();

		/// Returns the known-class entry for the given class, creating a level-1 entry if it is new.
		CharacterClassData& GetOrCreateKnownClass(uint32 classId);

		/// Returns true if the character's race is permitted to be the given class (mirrors the data
		/// the character-creation screen uses: the race must have class-specific initial data).
		[[nodiscard]] bool IsClassAllowedForRace(const proto::ClassEntry& classEntry) const;

		/// Returns true if the given spell is usable by the character's active race and class.
		[[nodiscard]] bool IsSpellActiveForCurrentClass(const proto::SpellEntry& spell) const;

		/// Rebuilds the live spellbook for the active class: deactivates class-bound spells the current
		/// class can't use and activates known spells it can. Persistent spells (no class mask) are kept.
		void ActivateKnownSpellsForCurrentClass();

	protected:
		void OnKilled(GameUnitS *killer) override;

		void OnSpellLearned(const proto::SpellEntry &spell) override;

		void OnSpellUnlearned(const proto::SpellEntry &spell) override
		{
			spellUnlearned(*this, spell);
		}

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::PlayerFieldCount);
		}

	private:
		/// Arms a fail-countdown for a timed quest based on its absolute expiration time.
		/// If the deadline has already passed, the quest is failed immediately instead.
		/// @param questId The quest id.
		/// @param expirationUnix The deadline as a unix timestamp in seconds.
		void ArmQuestTimer(uint32 questId, GameTime expirationUnix);

		/// Cancels and removes any running fail-countdown for the given quest.
		/// @param questId The quest id.
		void CancelQuestTimer(uint32 questId);

		/// Fails all quests in the quest log that have the StayAlive flag set. Called on death.
		void FailQuestsOnDeath();

	private:
		String m_name;
		Inventory m_inventory;
		const proto::ClassEntry *m_classEntry;
		/// All classes this character has learned (one is active at a time; the active id is the
		/// replicated Class field). Holds each class's level, attribute spending and talent ranks.
		std::vector<CharacterClassData> m_knownClasses;
		/// Every non-talent spell the character has learned across all classes. The live spellbook
		/// (m_spells) is the subset usable by the active class; this set is the persistence authority
		/// so spells of inactive classes are preserved (and re-activated when switching back).
		std::set<uint32> m_knownSpellIds;
		/// True while ChangeClass is rebuilding the character, so client-bound per-spell notifications
		/// are suppressed (a single refresh is sent afterwards instead).
		bool m_classSwitchInProgress = false;
		const proto::RaceEntry *m_raceEntry;
		std::array<uint32, 5> m_attributePointEnhancements;
		std::array<uint32, 5> m_attributePointsSpent;
		uint32 m_totalAvailablePointsAtLevel;
		uint32 m_totalTalentPointsAtLevel;
		std::map<uint32, QuestStatusData> m_quests;
		std::set<uint32> m_rewardedQuestIds;

		/// Daily/weekly quests that have been rewarded, mapped to the unix timestamp (seconds) at
		/// which they become available again.
		std::map<uint32, GameTime> m_repeatableResets;

		/// Running fail-countdowns for timed quests, keyed by quest id. Created on accept/login and
		/// destroyed on reward/abandon/fail.
		struct QuestTimer
		{
			std::unique_ptr<Countdown> countdown;
			scoped_connection onExpired;
		};
		std::map<uint32, QuestTimer> m_questTimers;
		NetPlayerWatcher *m_netPlayerWatcher = nullptr;
		uint64 m_groupId = 0;
		PlayerGroup* m_playerGroup = nullptr;
		LootMethod m_lootMethod = loot_method::FreeForAll;
		uint8 m_lootThreshold = item_quality::Uncommon;
		uint64 m_lootMasterGuid = 0;
		AvatarConfiguration m_configuration;
		std::weak_ptr<GameObjectS> m_lootObject;
		bool m_isGameMaster = false;
		std::unordered_map<uint32, uint32> m_talents;

		/// Persistent aura/cooldown snapshots populated by operator>> when reconstructing a
		/// character (realm side). Live auras/cooldowns are accessed via GameUnitS instead.
		std::vector<PersistentAuraData> m_deserializedAuras;
		std::vector<PersistentCooldownData> m_deserializedCooldowns;

	private:
		friend io::Writer &operator<<(io::Writer &w, GamePlayerS const &object);
		friend io::Reader &operator>>(io::Reader &r, GamePlayerS &object);
	};

	io::Writer &operator<<(io::Writer &w, GamePlayerS const &object);

	io::Reader &operator>>(io::Reader &r, GamePlayerS &object);
}
