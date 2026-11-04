// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>
#include <vector>
#include <map>
#include <memory>

#include "game_unit_s.h"
#include "game_server/inventory.h"
#include "game_server/character_data.h"
#include "game_server/trade_skill_book.h"
#include "game_server/quest_status_data.h"
#include "game_server/pets/persistent_pet.h"
#include "game/pet_defines.h"
#include "game/pet_packets.h"
#include "game/quest.h"
#include "game/group.h"
#include "game/character_customization/customizable_avatar_definition.h"

namespace mmo
{
	struct QuestStatusData;
	class GameItemS;
	class GameCreatureS;
		class PlayerGroup;

	namespace proto
	{
		class QuestEntry;
		class RaceEntry;
		class ClassEntry;
		class PetEntry;
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

		/// Called when the player used a mailbox world object. Implementations should start a
		/// mailbox interaction session and make the client show the mail window.
		virtual void OnMailboxUsed(uint64 mailboxGuid)
		{
		}

		/// Called when an inventory operation initiated by the player's own action (e.g. a quest turn-in)
		/// failed and the player should be told why.
		virtual void OnInventoryError(InventoryChangeFailure error)
		{
		}

		/// Called when the character gained class XP for the active class. xpToNextLevel is 0 when
		/// the class has reached its class max level. talentPointsGained is the number of talent
		/// points the reached class rank(s) granted and is 0 unless leveledUp is true.
		virtual void OnClassXpGained(uint32 classId, uint32 xpGained, uint8 classLevel, uint32 classXp, uint32 xpToNextLevel, bool leveledUp, uint32 talentPointsGained)
		{
		}

		/// Called when the summoned persistent pet changed as a whole: it was summoned or
		/// dismissed (or died), leveled up, learned a spell or was renamed. Implementations send the
		/// pet's state (or that there is no persistent pet) to the client.
		virtual void OnPetSpellsChanged()
		{
		}

		/// Called when the experience of the summoned persistent pet changed (OwnLevel pets only).
		/// @param xp The pet's experience towards its next level.
		/// @param nextLevelXp The experience its next level needs.
		virtual void OnPetXpChanged(uint32 xp, uint32 nextLevelXp)
		{
		}

		/// Called when the summoned persistent pet learned a spell (LearnPetSpell or a level spell).
		/// @param spellId The learned spell.
		virtual void OnPetLearnedSpell(uint32 spellId)
		{
		}

		/// Called when a pet action of the player failed, so the player can be told why.
		/// @param result Why the action failed.
		virtual void OnPetResult(pet_result::Type result)
		{
		}

		/// Whether persistent pets are available (the PETS subsystem of the world node). While
		/// they are not, summoning, granting, taming, reviving and teaching pets fail with
		/// pet_result::Disabled and no stored pet changes; dismissing still works.
		[[nodiscard]] virtual bool ArePetsAvailable() const
		{
			return true;
		}

		/// Whether the realm's list of the character's stored pets has arrived. Until it has,
		/// persistent pet actions fail with pet_result::Disabled like with the subsystem off, so
		/// a grant or tame cannot duplicate a stored pet the world node does not know about yet.
		[[nodiscard]] virtual bool AreStoredPetsLoaded() const
		{
			return true;
		}
	};

	/// @brief Represents a playable character in the game world.
	class GamePlayerS final
		: public GameUnitS
	{
	public:
		signal<void(GameUnitS &, const proto::SpellEntry &)> spellLearned;
		signal<void(GameUnitS &, const proto::SpellEntry &)> spellUnlearned;

		/// Fired when the character unlocks a new emote (not fired for default-known emotes).
		signal<void(uint32)> emoteLearned;

		/// Fired when a trade skill is learned or its cap changes. Parameter: skill id.
		signal<void(uint32 skillId)> tradeSkillLearned;
		/// Fired when the value of a trade skill changes. Parameter: skill id.
		signal<void(uint32 skillId)> tradeSkillChanged;

		/// Fired when the character's spent talent points have been refunded (talents reset).
		signal<void()> talentsReset;
		/// Fired after the player's active class has been switched (ChangeClass succeeded).
		signal<void()> classChanged;
		/// Fired when the set of known classes changes without an active-class switch — e.g. learning a
		/// class-change spell registers that class at level 1. Used to refresh the client's class list.
		signal<void()> knownClassesChanged;
		/// Fired when the character's spent attribute points have been refunded.
		signal<void()> attributePointsReset;

		/// Fired on every change of the stored pets that has to be saved (GetStoredPets is the new
		/// state): summon, dismiss, death, experience, levels, learned spells, created pets.
		signal<void()> storedPetsChanged;

		/// Fired when a new stored pet has to be created (granted or tamed). The realm creates it
		/// with a database id and answers with OnPetCreated. The request has dbId 0. A pet that is
		/// to be summoned as soon as it exists (a tamed pet) has wasSummoned set.
		signal<void(const PersistentPetData&)> petCreationRequested;

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

		/// Returns the class experience accumulated towards the next class level of the currently active class.
		[[nodiscard]] uint32 GetActiveClassXp() const;

		/// Returns true if the character has unlocked the given class (the active class is always
		/// known). Class ids are 0-based, so do not treat 0 as "no class".
		[[nodiscard]] bool IsClassKnown(uint32 classId) const;

		/// Resolves the spell that switches the active class to the given class. Prefers the class's
		/// configured `class_change_spell`; falls back to any known spell carrying a ChangeClass effect
		/// that targets this class. Returns 0 if none is found.
		[[nodiscard]] uint32 GetClassChangeSpellId(uint32 classId) const;

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

		/// Gets the number of purchased bank bag slots.
		uint8 GetBankBagSlotCount() const noexcept override
		{
			return static_cast<uint8>(Get<uint32>(object_fields::BankBagSlotCount));
		}

		/// Notifies the network layer that the player used a mailbox world object.
		void NotifyMailboxUsed(const uint64 mailboxGuid) const
		{
			if (m_netPlayerWatcher)
			{
				m_netPlayerWatcher->OnMailboxUsed(mailboxGuid);
			}
		}

		const proto::ClassEntry *GetClassEntry() const { return m_classEntry; }

		const proto::RaceEntry *GetRaceEntry() const { return m_raceEntry; }

		bool AddAttributePoint(uint32 attribute);

		void ResetAttributePoints();

		/// Applies a known class's saved attribute-point spending by replaying point allocations from a
		///	clean slate. Spending made under a different attribute layout (see GetAttributeLayout), or
		///	that no longer fits the point budget, is discarded entirely via ResetAttributePoints.
		/// @returns false if the saved spending was discarded and all points are unspent again.
		bool ApplyAttributeSpending(const CharacterClassData& classData);

		/// Gets the hash of the currently defined attribute set (see ComputeAttributeLayoutHash). Saved
		///	attribute spending carries the layout it was made under.
		[[nodiscard]] uint32 GetAttributeLayout() const;

		void ResetTalents();

		static uint8 CalculateAttributeCost(uint32 pointsSpent);

		bool HasMoney(uint32 amount) const;

		bool ConsumeMoney(uint32 amount);

		/// @return true if the amount can be added without exceeding the money cap.
		bool CanAddMoney(uint32 amount) const;

		/// Adds money, capped at the maximum (never wraps). Check CanAddMoney first where the
		/// excess must not be lost.
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
		void WriteObjectUpdateBlock(io::Writer &writer, bool creation = true, bool spawned = false) const override;

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

		/// Unlocks an emote for this character. Returns true if the emote was newly unlocked
		/// (unknown emote ids and already unlocked emotes return false).
		bool AddEmote(uint32 emoteId);

		/// Removes a previously unlocked emote from this character.
		void RemoveEmote(uint32 emoteId) { m_knownEmoteIds.erase(emoteId); }

		/// Replaces the set of unlocked emotes. Used when loading the character.
		void SetKnownEmotes(const std::vector<uint32>& emoteIds);

		/// Unlocks every default-known emote this character does not know yet, as if it had
		/// always been learned. Called on login so existing characters pick up emotes that
		/// were flagged as always-granted after their creation.
		void GrantDefaultEmotes();

		/// Returns the learned trade skills.
		[[nodiscard]] TradeSkillBook& GetTradeSkills() { return m_tradeSkills; }
		/// Returns the learned trade skills.
		[[nodiscard]] const TradeSkillBook& GetTradeSkills() const { return m_tradeSkills; }
		/// Replaces the learned trade skills (e.g. when loading a character). Fires no signals.
		void SetTradeSkills(const std::vector<TradeSkillData>& skills) { m_tradeSkills.SetAll(skills); }

		/// Returns the set of unlocked emote ids (the persistence set).
		[[nodiscard]] const std::set<uint32>& GetKnownEmoteIds() const { return m_knownEmoteIds; }

		/// Returns true if the character can use the given emote (default-known or unlocked).
		[[nodiscard]] bool KnowsEmote(uint32 emoteId) const;

	public:
		/// Gets the current status of a given quest by its id.
		/// @returns Quest status.
		QuestStatus GetQuestStatus(uint32 quest) const;

		/// Gets the tracked progress of a quest in the quest log.
		/// @returns The quest data, or nullptr if the quest is not tracked for this player.
		const QuestStatusData* GetQuestData(uint32 quest) const
		{
			const auto it = m_quests.find(quest);
			return it != m_quests.end() ? &it->second : nullptr;
		}

		/// Returns true if the quest's required-classes mask permits the currently ACTIVE class.
		/// Quests already in the log stay there when the player switches to a non-matching class,
		/// but are frozen: no objective progress and no turn-in until a matching class is active.
		[[nodiscard]] bool IsQuestClassAllowed(const proto::QuestEntry& entry) const;

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

		/// Determines whether the given quest counts as rewarded (including interval-repeatable
		/// quests that are still locked until their next reset). Unlike GetQuestStatus this never
		/// evaluates availability rules, so it is safe to use from within GetQuestStatus itself.
		bool HasRewardedQuest(uint32 quest) const;

		/// Immediately rewards a completed quest flagged as AutoRewarded (no choice rewards).
		/// @return true if the quest was rewarded.
		bool TryAutoRewardQuest(uint32 questId);

		/// Schedules a deferred auto-reward attempt for a quest that just completed, if it is
		/// flagged as AutoRewarded. Deferred to run outside the completing credit path. When the
		/// player is not in a world yet (login-time load), the check is queued and flushed on spawn.
		void ScheduleAutoRewardCheck(uint32 questId);

		/// Called when a quest-related creature was killed.
		void OnQuestKillCredit(uint64 unitGuid, const proto::UnitEntry &entry);

		/// Determines whether the character fulfulls all requirements of the given quests.
		bool FulfillsQuestRequirements(const proto::QuestEntry &entry) const;

		/// Determines whether the players questlog is full.
		bool IsQuestlogFull() const;

		/// Called when the exploration/event objective of the given quest was fulfilled (area
		/// trigger reached or QuestExplorationCredit trigger action fired). Marks the quest's
		/// exploration credit without force-completing its remaining objectives.
		void OnQuestExploration(uint32 questId);

		/// Called when the player successfully used a world object. Grants object-use credit for
		/// quest requirements that reference the object's entry id without a spellcast id.
		void OnQuestObjectUseCredit(uint64 objectGuid, uint32 objectEntryId);

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

		/// True unless the spell is a follow-up rank of a talent spell (see FindGatingTalent) whose
		/// talent is not learned. Such ranks are bought from trainers but only usable with the talent.
		[[nodiscard]] bool IsTalentGateSatisfied(const proto::SpellEntry& spell) const;

		/// Activates known follow-up ranks whose talent is learned and deactivates those whose talent is
		/// not. Deactivated ranks stay in the known-spell set, so learning the talent again restores them.
		void RefreshTalentGatedSpells();

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

		/// @brief Returns the equipped weapon's swing time for the given hand, before attack speed auras.
		uint32 GetBaseAutoAttackTime(WeaponAttack attackType) const override;

		float GetUnitMissChance() const override;

		bool HasOffhandWeapon() const override;

	public:
		// === Persistent pets ===

		/// @return The character's stored (persistent) pets, including the summoned one.
		[[nodiscard]] const std::vector<PersistentPetData>& GetStoredPets() const { return m_storedPets; }

		/// Replaces the stored pets (loaded by the realm). A pet that was summoned when it was stored
		/// is summoned again if the character is in a world and alive.
		/// @param pets The stored pets.
		void SetStoredPets(std::vector<PersistentPetData> pets);

		/// @return The stored pet of a pet entry, or nullptr.
		/// @param petEntry Id of the pet entry.
		[[nodiscard]] PersistentPetData* FindStoredPet(uint32 petEntry);

		/// @copydoc FindStoredPet
		[[nodiscard]] const PersistentPetData* FindStoredPet(uint32 petEntry) const;

		/// Checks whether SummonStoredPet would succeed, without changing anything or telling the
		/// client (spell cast validation).
		/// @param petEntry As for SummonStoredPet.
		/// @return Ok or the result SummonStoredPet would fail with.
		[[nodiscard]] pet_result::Type CanSummonStoredPet(uint32 petEntry) const;

		/// Checks whether TameCreature would succeed, without changing anything or telling the
		/// client (spell cast validation).
		/// @param target The creature to tame.
		/// @param ignoreLevel Whether the creature may be above the character's level (GM command).
		/// @return Ok or the result TameCreature would fail with.
		[[nodiscard]] pet_result::Type CanTameCreature(const GameCreatureS& target, bool ignoreLevel = false) const;

		/// Checks whether GrantPersistentPet would succeed, without changing anything or telling the
		/// client (spell cast validation).
		/// @param petEntry Id of the pet entry.
		/// @return Ok or the result GrantPersistentPet would fail with.
		[[nodiscard]] pet_result::Type CanGrantPersistentPet(uint32 petEntry) const;

		/// Checks whether RevivePet would succeed, without changing anything or telling the client
		/// (spell cast validation).
		/// @param petEntry As for RevivePet.
		/// @return Ok or the result RevivePet would fail with.
		[[nodiscard]] pet_result::Type CanRevivePet(uint32 petEntry) const;

		/// Renames the summoned persistent pet. The name must be valid (IsValidPetName) and the
		/// pet's entry must allow player naming; it is normalized (NormalizePetName). On success the
		/// stored and the live name change, the client is told (OnPetSpellsChanged) and the change
		/// is saved (storedPetsChanged). On failure nothing changes and the client is told why.
		/// @param name The new name.
		/// @param changed If not null, set to whether the name changed (false for a rename to the
		///	       current name, which also returns Ok).
		/// @return Ok, Disabled, NoPet, NamingNotAllowed or InvalidName.
		pet_result::Type RenamePersistentPet(const String& name, bool* changed = nullptr);

		/// @return The state of the summoned persistent pet as the PetSpells packet carries it; a
		///	        petGuid of 0 if no persistent pet is summoned (or it has not spawned yet).
		[[nodiscard]] game::PetSpellsData GetPetSpellsData() const;

		/// Summons a stored pet. The currently summoned persistent pet is dismissed first. The pet
		/// enters the world shortly after (the spawn is posted).
		/// @param petEntry Pet entry of the stored pet, or 0 for the most recently summoned one of
		///	       the active class.
		/// @return Ok, NoPet, WrongClass (the pet belongs to another class), PetDead (a dead tamed
		///	        pet) or TooManyPets (the class may not summon persistent pets).
		pet_result::Type SummonStoredPet(uint32 petEntry);

		/// Dismisses the summoned persistent pet: its state is written into the stored list and it
		/// leaves the world. Does nothing if no persistent pet is summoned.
		/// @param keepWasSummoned Whether the pet comes back by itself (logout, map change, owner
		///	       death) or stays stored (dismiss, class switch).
		void DismissPersistentPet(bool keepWasSummoned);

		/// Gives experience to the summoned persistent pet if it levels on its own (OwnLevel). It
		/// levels up to its master's level (its level source) and keeps gaining when that rises.
		/// @param xp The experience.
		void AddPetExperience(uint32 xp);

		/// Called when the PETS subsystem of the world node is switched off or on. Switched off, the
		/// summoned persistent pet is dismissed (it stays wasSummoned) and the temporary pets are
		/// despawned; switched on, the dismissed pet comes back.
		/// @param available Whether pets are available now.
		void OnPetsAvailabilityChanged(bool available);

		/// Called with the realm's answer to petCreationRequested: the pet now exists with a
		/// database id and is added to the stored pets (and summoned if wasSummoned is set).
		/// @param pet The created pet.
		void OnPetCreated(const PersistentPetData& pet);

		/// Tames a creature: it leaves the world and becomes a stored pet of the active class, which
		/// is summoned once the realm created it.
		/// @param target The creature to tame.
		/// @param ignoreLevel Whether the creature may be above the character's level (GM command).
		/// @return Ok, NotTamable, TooHighLevel or NoFreeSlot.
		pet_result::Type TameCreature(GameCreatureS& target, bool ignoreLevel = false);

		/// Removes the stored pet of a pet entry for good (GM command): a summoned pet is dismissed
		/// first, then the pet leaves the stored list and the change is saved, which makes the realm
		/// delete it. Not reported to the client.
		/// @param petEntry Id of the pet entry.
		/// @return Ok, or NoPet if the character has no stored pet of that entry.
		pet_result::Type RemoveStoredPet(uint32 petEntry);

		/// Grants the character a stored pet of a pet entry for the active class (not summoned).
		/// @param petEntry Id of the pet entry.
		/// @return Ok, NoPet (unknown entry), AlreadyHavePet or NoFreeSlot.
		pet_result::Type GrantPersistentPet(uint32 petEntry);

		/// Revives a dead stored pet and summons it.
		/// @param petEntry Pet entry of the stored pet, or 0 for the most recently summoned one.
		/// @param healthPercent The health the pet comes back with, in percent of its max health.
		/// @return Ok, NoPet (no such dead pet), or a SummonStoredPet failure.
		pet_result::Type RevivePet(uint32 petEntry, uint32 healthPercent);

		/// Teaches a spell to the summoned persistent pet.
		/// @param spellId The spell.
		/// @return Ok or NoPet.
		pet_result::Type LearnPetSpell(uint32 spellId);

		/// Called when the character came back to life. A persistent pet that was summoned when it
		/// died comes back.
		void OnResurrected();

		/// Tells the player's client that a pet action failed (NetPlayerWatcher::OnPetResult).
		/// @param result Why it failed.
		void NotifyPetResult(pet_result::Type result);

		/// Leaving the world (logout, map change) dismisses the persistent pet, keeping wasSummoned
		/// so it comes back in the next world.
		void OnDespawn() override;

		void RewardExperience(const uint32 xp);

		/// Grants class XP to the ACTIVE class and levels it up against the class's classlevels
		/// curve. No-ops when the class has no curve configured (class level stays frozen at 1) or
		/// the class already reached its class max level. Independent of the character level cap.
		void RewardClassExperience(uint32 xp);

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

	protected:
		/// Melee crit chance from the active class's meleeCritSources.
		float GetMeleeCritChanceFromAttributes() const override;

		/// Dodge chance from the active class's dodgeSources.
		float GetDodgeChanceFromAttributes() const override;

	public:

		void UpdateAttributePoints();

		void UpdateTalentPoints();

		/// Recomputes the total attribute points available at the current character level for the
		/// active class. Character-level driven (character-wide total).
		void UpdateTotalAttributePoints();

		/// Recomputes the total talent points available for the active class from its class level.
		void UpdateTotalTalentPoints();

		uint32 GetAttributePointsByAttribute(const uint32 attribute) const
		{
			ASSERT(attribute < MaxAttributes);
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

		/// Writes the live talents and attribute spending back into the active class's known-class entry.
		void SyncActiveClassData();

		/// Returns the known-class entry for the given class, creating a level-1 entry if it is new.
		CharacterClassData& GetOrCreateKnownClass(uint32 classId);

		/// Returns true if the character's race is permitted to be the given class (mirrors the data
		/// the character-creation screen uses: the race must have class-specific initial data).
		[[nodiscard]] bool IsClassAllowedForRace(const proto::ClassEntry& classEntry) const;

		/// Returns true if the given spell is usable by the character's active race and class.
		[[nodiscard]] bool IsSpellActiveForCurrentClass(const proto::SpellEntry& spell) const;

		/// Returns true if the active class's ClassEntry.spells lists this spell. Used to determine the
		/// owning class of proficiency spells, which are class-bound despite carrying no class mask.
		[[nodiscard]] bool IsSpellGrantedByActiveClass(const proto::SpellEntry& spell) const;

		/// Rebuilds the live spellbook for the active class: deactivates class-bound spells the current
		/// class can't use and activates known spells it can. Persistent spells (no class mask) are kept.
		void ActivateKnownSpellsForCurrentClass();

		/// Recomputes the active proficiency set from the currently-active passive spells that carry a
		/// Proficiency effect. The active-spell set is the single source of truth, so proficiencies
		/// granted by a now-deactivated class's spells are dropped and the current class's are added.
		/// Only the difference against the existing set is applied so client notifications are minimal.
		void RefreshProficiencies();

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

		// === Persistent pets (game_player_s_pets.cpp) ===

		/// @return The stored pet with a database id, or nullptr.
		PersistentPetData* FindStoredPetByDbId(uint64 dbId);

		/// @copydoc FindStoredPetByDbId
		const PersistentPetData* FindStoredPetByDbId(uint64 dbId) const;

		/// @return The stored pet a request without a pet entry means: the most recently summoned
		///	        pet of the active class, else one that was summoned, else the first of the active
		///	        class, else the first one. With deadOnly, only dead pets are considered.
		PersistentPetData* SelectStoredPet(bool deadOnly);

		/// @copydoc SelectStoredPet
		const PersistentPetData* SelectStoredPet(bool deadOnly) const;

		/// The checks of SummonPet that can fail: Disabled, WrongClass, PetDead, NoPet (unknown pet
		/// or creature entry) and TooManyPets.
		/// @param pet The stored pet to summon.
		/// @return Ok or why the summon fails.
		[[nodiscard]] pet_result::Type CheckSummonPet(const PersistentPetData& pet) const;

		/// Summons a stored pet (see SummonStoredPet).
		pet_result::Type SummonPet(PersistentPetData& pet);

		/// Spawns the pet SummonPet asked for, unless the request was cancelled since (token).
		void SpawnSummonedPet(uint32 token);

		/// Summons the stored pet that was summoned when it was stored, if the character is alive
		/// in a world and has no persistent pet summoned.
		void ResummonPersistentPet();

		/// Called when the summoned persistent pet died: it is stored dead.
		void OnPersistentPetKilled(GameCreatureS& pet);

		/// Called when the summoned persistent pet left the world without being dismissed.
		void OnPersistentPetDespawned(GameCreatureS& pet);

		/// Brings a MasterLevel pet to its master's level after the master's level changed.
		void RefreshPersistentPetLevel();

		/// Changes the summoned pet's level, teaching it the level spells it reached.
		void ApplyPersistentPetLevel(GameCreatureS& pet, const proto::PetEntry& entry, PersistentPetData& data, uint32 oldLevel, uint32 newLevel);

		/// @return Whether another stored pet can be created (stored plus pending creations).
		[[nodiscard]] bool HasFreePetSlot() const;

		/// @return Whether persistent pets are available (NetPlayerWatcher::ArePetsAvailable and
		///	        NetPlayerWatcher::AreStoredPetsLoaded).
		[[nodiscard]] bool ArePetsAvailable() const;

		/// Asks the realm to create a stored pet (petCreationRequested).
		void RequestPetCreation(const PersistentPetData& pet);

		/// Tells the client that a pet action failed, and returns the result.
		pet_result::Type PetActionFailed(pet_result::Type result);

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
		/// Every emote the character has unlocked, including default-known emotes (which are
		/// materialized into this set on login via GrantDefaultEmotes).
		std::set<uint32> m_knownEmoteIds;
		/// Learned trade skills (professions).
		TradeSkillBook m_tradeSkills;
		/// True while ChangeClass is rebuilding the character, so client-bound per-spell notifications
		/// are suppressed (a single refresh is sent afterwards instead).
		bool m_classSwitchInProgress = false;
		const proto::RaceEntry *m_raceEntry;
		std::array<uint32, MaxAttributes> m_attributePointEnhancements;
		std::array<uint32, MaxAttributes> m_attributePointsSpent;
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

		/// Auto-reward checks requested before the player entered a world (login-time quest data
		/// load); flushed once the player spawns.
		std::set<uint32> m_pendingAutoRewardChecks;
		scoped_connection m_onSpawnedAutoReward;

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

		/// The character's stored pets (authoritative while the character is in the world).
		std::vector<PersistentPetData> m_storedPets;
		/// Pet entries of pets the realm was asked to create and has not answered yet.
		std::vector<uint32> m_pendingPetCreations;
		/// Database id of the summoned persistent pet (or the one about to be spawned), 0 if none.
		uint64 m_summonedPetDbId = 0;
		/// The summoned persistent pet creature, once spawned.
		std::weak_ptr<GameCreatureS> m_summonedPet;
		/// Database id of the persistent pet summoned last, for summons without a pet entry.
		uint64 m_lastSummonedPetDbId = 0;
		/// Increased on every summon and dismiss, so a posted spawn can tell it was cancelled.
		uint32 m_petSummonToken = 0;
		/// A revived pet's health in percent, applied when it spawns (m_reviveHealthPetDbId).
		uint32 m_reviveHealthPercent = 0;
		uint64 m_reviveHealthPetDbId = 0;
		scoped_connection m_summonedPetKilled;
		scoped_connection m_summonedPetDespawned;
		scoped_connection m_onSpawnedPets;

	private:
		friend io::Writer &operator<<(io::Writer &w, GamePlayerS const &object);
		friend io::Reader &operator>>(io::Reader &r, GamePlayerS &object);
	};

	io::Writer &operator<<(io::Writer &w, GamePlayerS const &object);

	io::Reader &operator>>(io::Reader &r, GamePlayerS &object);
}
