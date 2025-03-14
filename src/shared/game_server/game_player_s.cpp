// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_player_s.h"

#include "game_item_s.h"
#include "quest_status_data.h"
#include "proto_data/project.h"
#include "game/item.h"

namespace mmo
{
	io::Writer& operator<<(io::Writer& w, const QuestStatusData& object)
	{
		return w
			<< io::write<uint8>(object.status)
			<< io::write<uint8>(object.explored)
			<< io::write<uint32>(object.expiration)
			<< io::write_range(object.creatures);

	}

	io::Reader& operator>>(io::Reader& r, QuestStatusData& object)
	{
		return r
			>> io::read<uint8>(object.status)
			>> io::read<uint8>(object.explored)
			>> io::read<uint32>(object.expiration)
			>> io::read_range(object.creatures);
	}

	GamePlayerS::GamePlayerS(const proto::Project& project, TimerQueue& timerQueue)
		: GameUnitS(project, timerQueue)
		, m_inventory(*this)
	{
	}

	void GamePlayerS::Initialize()
	{
		GameUnitS::Initialize();

		// Initialize some default values
		Set<int32>(object_fields::MaxLevel, 5, false);
		Set<int32>(object_fields::Xp, 0, false);
		Set<int32>(object_fields::NextLevelXp, 400, false);
		Set<int32>(object_fields::Level, 1, false);

		m_attributePointEnhancements.fill(0);
		m_attributePointsSpent.fill(0);
		for (uint32 i = 0; i < 5; ++i)
		{
			SetAttributeCost(i, 1);
		}

		Set<uint32>(object_fields::AvailableAttributePoints, 0, false);
	}

	void GamePlayerS::WriteObjectUpdateBlock(io::Writer& writer, bool creation) const
	{
		GameUnitS::WriteObjectUpdateBlock(writer, creation);

		// Apply configuration
		if (creation)
		{
			writer << m_configuration;
		}
	}

	void GamePlayerS::SetConfiguration(const AvatarConfiguration& configuration)
	{
		m_configuration = configuration;
	}

	void GamePlayerS::SetPlayerWatcher(NetPlayerWatcher* watcher)
	{
		m_netPlayerWatcher = watcher;
	}

	void GamePlayerS::SetClass(const proto::ClassEntry& classEntry)
	{
		m_classEntry = &classEntry;

		Set<int32>(object_fields::MaxLevel, classEntry.levelbasevalues_size());
		Set<int32>(object_fields::PowerType, classEntry.powertype());

		RefreshStats();
	}

	void GamePlayerS::SetRace(const proto::RaceEntry& raceEntry)
	{
		m_raceEntry = &raceEntry;

		Set<uint32>(object_fields::Race, raceEntry.id());
		Set<uint32>(object_fields::FactionTemplate, raceEntry.factiontemplate());
	}

	void GamePlayerS::SetGender(const uint8 gender)
	{
		uint32 bytes = Get<uint32>(object_fields::Bytes);

		// Clear the first byte (gender) and then set the new gender
		bytes &= 0xffffff00; // Clear the first byte (mask with 0s in the gender byte and 1s elsewhere)
		bytes |= static_cast<uint32>(gender); // Set the new gender

		Set<uint32>(object_fields::Bytes, bytes);

		// Update visual
		if (m_raceEntry)
		{
			Set<uint32>(object_fields::DisplayId, gender == 0 ? m_raceEntry->malemodel() : m_raceEntry->femalemodel());
		}
	}

	uint8 GamePlayerS::GetGender() const
	{
		const uint32 bytes = Get<uint32>(object_fields::Bytes);
		return bytes & 0xff; // Return only the first byte (gender)
	}

	void GamePlayerS::SetAttributeCost(const uint32 attribute, const uint8 cost)
	{
		ASSERT(attribute < 5);

		uint64 attributeCostPacked = Get<uint64>(object_fields::AttributePointCost);

		// Clear the 8-bit segment at the specified index and set the new cost
		attributeCostPacked &= ~(static_cast<uint64>(0xFF) << (attribute * 8)); // Clear the 8-bit slot
		attributeCostPacked |= static_cast<uint64>(cost) << (attribute * 8);    // Set the 8-bit value

		Set<uint64>(object_fields::AttributePointCost, attributeCostPacked);
	}

	uint8 GamePlayerS::GetAttributeCost(uint32 attribute) const
	{
		ASSERT(attribute < 5);

		const uint64 attributeCostPacked = Get<uint64>(object_fields::AttributePointCost);
		return (attributeCostPacked >> (attribute * 8)) & 0xFF;
	}

	void GamePlayerS::ApplyItemStats(const GameItemS& item, const bool apply)
	{
		const auto& itemEntry = item.GetEntry();

		if (itemEntry.durability() == 0 || item.Get<uint32>(object_fields::Durability) > 0)
		{
			// Apply values
			for (int i = 0; i < itemEntry.stats_size(); ++i)
			{
				const auto& stat = itemEntry.stats(i);
				if (stat.value() != 0)
				{
					switch (stat.type())
					{
					case item_stat::Mana:
						UpdateModifierValue(unit_mods::Mana, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Health:
						UpdateModifierValue(unit_mods::Health, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Agility:
						UpdateModifierValue(unit_mods::StatAgility, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Strength:
						UpdateModifierValue(unit_mods::StatStrength, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Intellect:
						UpdateModifierValue(unit_mods::StatIntellect, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Spirit:
						UpdateModifierValue(unit_mods::StatSpirit, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Stamina:
						UpdateModifierValue(unit_mods::StatStamina, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					default:
						break;
					}
				}
			}

			std::array<bool, 6> shouldUpdateResi;
			shouldUpdateResi.fill(false);

			if (itemEntry.armor())
			{
				UpdateModifierValue(unit_mods::Armor, unit_mod_type::BaseValue, itemEntry.armor(), apply);
			}

			if (itemEntry.holyres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceHoly, unit_mod_type::TotalValue, itemEntry.holyres(), apply);
				shouldUpdateResi[0] = true;
			}
			if (itemEntry.fireres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceFire, unit_mod_type::TotalValue, itemEntry.fireres(), apply);
				shouldUpdateResi[1] = true;
			}
			if (itemEntry.natureres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceNature, unit_mod_type::TotalValue, itemEntry.natureres(), apply);
				shouldUpdateResi[2] = true;
			}
			if (itemEntry.frostres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceFrost, unit_mod_type::TotalValue, itemEntry.frostres(), apply);
				shouldUpdateResi[3] = true;
			}
			if (itemEntry.shadowres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceShadow, unit_mod_type::TotalValue, itemEntry.shadowres(), apply);
				shouldUpdateResi[4] = true;
			}
			if (itemEntry.arcaneres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceArcane, unit_mod_type::TotalValue, itemEntry.arcaneres(), apply);
				shouldUpdateResi[5] = true;
			}

			if (apply)
			{
				SpellTargetMap targetMap;
				targetMap.SetUnitTarget(GetGuid());
				targetMap.SetTargetMap(spell_cast_target_flags::Self);

				for (auto& spell : item.GetEntry().spells())
				{
					// Trigger == onEquip?
					if (spell.trigger() == item_spell_trigger::OnEquip)
					{
						if (const auto* spellEntry = m_project.spells.getById(spell.spell()))
						{
							CastSpell(targetMap, *spellEntry, 0, true, item.GetGuid());
						}
					}
				}
			}
			else
			{
				RemoveAllAurasDueToItem(item.GetGuid());
			}

			UpdateArmor();
			UpdateDamage();
		}
	}

	void GamePlayerS::ResetAttributePoints()
	{
		m_attributePointEnhancements.fill(0);
		m_attributePointsSpent.fill(0);
		RefreshStats();
	}

	uint8 GamePlayerS::CalculateAttributeCost(const uint32 pointsSpent)
	{
		const double a = 1.5;  // Initial growth multiplier
		const double b = 2.0;  // Offset for smoothing
		const double c = 0.02; // Exponential growth factor
		const double d = 1.5;  // Exponential growth exponent

		// Calculate cost with both square root and exponential components
		double cost = a * std::sqrt(pointsSpent + b) + c * std::pow(pointsSpent, d);

		// Cap the cost at 255
		return static_cast<uint8>(std::min(cost, 255.0));
	}

	bool GamePlayerS::HasMoney(const uint32 amount) const
	{
		return Get<uint32>(object_fields::Money) >= amount;
	}

	bool GamePlayerS::ConsumeMoney(const uint32 amount)
	{
		if (!HasMoney(amount))
		{
			return false;
		}

		Set<uint32>(object_fields::Money, Get<uint32>(object_fields::Money) - amount);
		return true;
	}

	QuestStatus GamePlayerS::GetQuestStatus(const uint32 quest) const
	{
		// Shortcut: rewarded quests are only stored with their id as we are not interested in anything else
		if (m_rewardedQuestIds.contains(quest))
		{
			return quest_status::Rewarded;
		}

		// Check if we have a cached quest state
		if (const auto it = m_quests.find(quest); it != m_quests.end())
		{
			if (it->second.status != quest_status::Available &&
				it->second.status != quest_status::Unavailable) 
			{
				return it->second.status;
			}
		}

		// We don't have that quest cached, make a lookup
		const auto* entry = GetProject().quests.getById(quest);
		if (!entry)
		{
			WLOG("Could not find quest " << quest);
			return quest_status::Unavailable;
		}

		// Check if the quest is available for us
		if (entry->minlevel() > 0 && GetLevel() < entry->minlevel())
		{
			if (GetLevel() == entry->minlevel() - 1)
			{
				return quest_status::AvailableNextLevel;
			}

			return quest_status::Unavailable;
		}

		if (entry->maxlevel() > 0 && GetLevel() > entry->maxlevel())
		{
			return quest_status::Unavailable;
		}

		// TODO: Check skill
		
		// Race/Class check
		ASSERT(m_raceEntry);
		ASSERT(m_classEntry);

		const uint32 charRaceBit = 1 << (m_raceEntry->id() - 1);
		const uint32 charClassBit = 1 << (m_classEntry->id() - 1);
		if (entry->requiredraces() && (entry->requiredraces() & charRaceBit) == 0)
		{
			return quest_status::Unavailable;
		}

		if (entry->requiredclasses() && (entry->requiredclasses() & charClassBit) == 0)
		{
			return quest_status::Unavailable;
		}

		// Quest chain checks
		if (entry->prevquestid())
		{
			if (GetQuestStatus(entry->prevquestid()) != quest_status::Rewarded)
			{
				return quest_status::Unavailable;
			}
		}

		return quest_status::Available;
	}

	bool GamePlayerS::AcceptQuest(const uint32 quest)
	{
		if (const QuestStatus status = GetQuestStatus(quest); status != quest_status::Available)
		{
			// We can't take that quest, maybe because we already completed it or already have it
			return false;
		}

		const auto* questEntry = GetProject().quests.getById(quest);
		if (!questEntry)
		{
			return false;
		}

		const proto::ItemEntry* srcItem = nullptr;
		if (questEntry->srcitemid())
		{
			if ((srcItem = GetProject().items.getById(questEntry->srcitemid())) == nullptr)
			{
				return false;
			}
		}

		// Find next free quest log
		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			auto questLogField = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (questLogField.questId == 0 || questLogField.questId == quest)
			{
				// Grant quest source item if possible
				if (srcItem)
				{
					std::map<uint16, uint16> addedBySlot;
					if (auto result = m_inventory.CreateItems(*srcItem, questEntry->srcitemcount(), &addedBySlot); result != inventory_change_failure::Okay)
					{
						//inventoryChangeFailure(result, nullptr, nullptr);
						return false;
					}

					// Notify the player about this
					for (auto& pair : addedBySlot)
					{
						//itemAdded(pair.first, pair.second, false, false);
					}
				}

				// Take that quest
				auto& data = m_quests[quest];
				data.status = quest_status::Incomplete;

				if (questEntry->srcspell())
				{
					if (const auto* spell = GetProject().spells.getById(questEntry->srcspell()))
					{
						// TODO: Maybe we should make the quest giver cast the spell, if it's a unit
						SpellTargetMap targetMap;
						targetMap.SetTargetMap(spell_cast_target_flags::Self | spell_cast_target_flags::Unit);
						targetMap.SetUnitTarget(GetGuid());
						CastSpell(targetMap, *spell, 0, true);
					}
				}

				// Quest timer
				uint32 questTimer = 0;
				if (questEntry->timelimit() > 0)
				{
					questTimer = GetAsyncTimeMs() + questEntry->timelimit();
					data.expiration = questTimer;
				}

				// Set quest log
				QuestField field;
				field.questId = quest;
				field.status = quest_status::Incomplete;
				field.questTimer = questTimer;

				// Complete if no requirements
				if (FulfillsQuestRequirements(*questEntry))
				{
					data.status = quest_status::Complete;
					field.status = data.status;
				}

				// Update quest log field value
				Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), field);
				if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(quest, data);

				return true;
			}
		}

		// No free quest slot found
		return false;
	}

	bool GamePlayerS::AbandonQuest(const uint32 quest)
	{
		// Find next free quest log
		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			auto questLogField = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (questLogField.questId == quest)
			{
				m_quests.erase(quest);

				// Reset quest log
				Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), QuestField());
				if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(quest, QuestStatusData());

				return true;
			}
		}

		return false;
	}

	bool GamePlayerS::CompleteQuest(uint32 quest)
	{
		// TODO

		return false;
	}

	bool GamePlayerS::FailQuest(uint32 quest)
	{
		// TODO

		return false;
	}

	bool GamePlayerS::RewardQuest(uint64 questgiverGuid, uint32 quest, uint8 rewardChoice)
	{
		// Reward experience
		const auto* entry = m_project.quests.getById(quest);
		if (!entry)
		{
			return false;
		}

		auto it = m_quests.find(quest);
		if (it == m_quests.end())
		{
			return false;
		}
		if (it->second.status != quest_status::Complete)
		{
			return false;
		}

		// Gather all rewarded items
		std::map<const proto::ItemEntry*, uint16> rewardedItems;
		{
			if (entry->rewarditemschoice_size() > 0)
			{
				// Validate reward index
				if (rewardChoice >= entry->rewarditemschoice_size())
				{
					return false;
				}

				const auto* item = GetProject().items.getById(
					entry->rewarditemschoice(rewardChoice).itemid());
				if (!item)
				{
					return false;
				}

				// Check if the player can store the item
				rewardedItems[item] += entry->rewarditemschoice(rewardChoice).count();
			}
			for (auto& rew : entry->rewarditems())
			{
				const auto* item = GetProject().items.getById(rew.itemid());
				if (!item)
				{
					return false;
				}

				rewardedItems[item] += rew.count();
			}
		}

		// First loop to check if the items can be stored
		for (auto& pair : rewardedItems)
		{
			const auto result = m_inventory.CanStoreItems(*pair.first, pair.second);
			if (result != inventory_change_failure::Okay)
			{
				//inventoryChangeFailure(result, nullptr, nullptr);
				return false;
			}
		}

		// Try to remove all required quest items
		for (const auto& req : entry->requirements())
		{
			if (req.itemid())
			{
				const auto* itemEntry = m_project.items.getById(req.itemid());
				if (!itemEntry)
				{
					return false;
				}

				const auto result = m_inventory.RemoveItems(*itemEntry, req.itemcount());
				if (result != inventory_change_failure::Okay)
				{
					//inventoryChangeFailure(result, nullptr, nullptr);
					return false;
				}
			}
		}

		// Second loop needed to actually create the items
		for (auto& pair : rewardedItems)
		{
			auto result = m_inventory.CreateItems(*pair.first, pair.second);
			if (result != inventory_change_failure::Okay)
			{
				//inventoryChangeFailure(result, nullptr, nullptr);
				return false;
			}
		}

		float xpFactor;
		const int32 playerLevel = static_cast<int32>(GetLevel());
		if (playerLevel <= entry->questlevel() + 5) {
			xpFactor = 1.0f;
		}
		else if (playerLevel == entry->questlevel() + 6) {
			xpFactor = 0.8f;
		}
		else if (playerLevel == entry->questlevel() + 7) {
			xpFactor = 0.6f;
		}
		else if (playerLevel == entry->questlevel() + 8) {
			xpFactor = 0.4f;
		}
		else if (playerLevel == entry->questlevel() + 9) {
			xpFactor = 0.2f;
		}
		else {
			xpFactor = 0.1f;
		}

		const uint32 rewardXp = static_cast<uint32>(static_cast<float>(entry->rewardxp()) * xpFactor);
		if (rewardXp > 0)
		{
			RewardExperience(rewardXp);
		}
		
		uint32 money = entry->rewardmoney();
		if (money > 0)
		{
			Set<uint32>(object_fields::Money, Get<uint32>(object_fields::Money) + money);
		}

		// Remove source items of this quest (if any)
		if (entry->srcitemid())
		{
			if (const auto* itemEntry = GetProject().items.getById(entry->srcitemid()))
			{
				// 0 means: remove ALL of this item
				m_inventory.RemoveItems(*itemEntry, 0);
			}
		}

		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			const QuestField field = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (field.questId == entry->id())
			{
				Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), QuestField());
				break;
			}
		}

		// Quest was rewarded
		it->second.status = quest_status::Rewarded;
		if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(quest, it->second);

		m_rewardedQuestIds.insert(entry->id());
		it = m_quests.erase(it);

		if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestCompleted(questgiverGuid, quest, rewardXp, money);

		return true;
	}

	void GamePlayerS::OnQuestKillCredit(const uint64 unitGuid, const proto::UnitEntry& entry)
	{
		const uint32 creditEntry = (entry.killcredit() != 0) ? entry.killcredit() : entry.id();

		// Check all quests in the quest log
		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			QuestField field = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (field.questId == 0) 
			{
				continue;
			}

			// Verify quest state
			auto it = m_quests.find(field.questId);
			if (it == m_quests.end()) 
			{
				continue;
			}

			if (it->second.status != quest_status::Incomplete) 
			{
				continue;
			}

			// Find quest
			const auto* quest = GetProject().quests.getById(field.questId);
			if (!quest) 
			{
				continue;
			}

			// Counter needed so that the right field is used
			uint8 reqIndex = 0;
			for (const auto& req : quest->requirements())
			{
				if (req.creatureid() == creditEntry)
				{
					// Get current counter
					uint8 counter = field.counters[reqIndex];
					if (counter < req.creaturecount())
					{
						// Increment and update counter
						field.counters[reqIndex] = ++counter;
						it->second.creatures[reqIndex]++;

						// Fire signal to update UI
						if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestKillCredit(*quest, unitGuid, creditEntry, it->second.creatures[reqIndex], req.creaturecount());

						// Check if this completed the quest
						if (FulfillsQuestRequirements(*quest))
						{
							// Complete quest
							it->second.status = quest_status::Complete;
							field.status = quest_status::Complete;
						}

						// Save quest progress
						Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), field);
						if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(field.questId, it->second);
					}

					// Continue with next quest, as multiple quests could require the same
					// creature kill credit
					continue;
				}

				reqIndex++;
			}
		}
	}

	bool GamePlayerS::FulfillsQuestRequirements(const proto::QuestEntry& entry) const
	{
		// Check if the character has this quest entry
		auto it = m_quests.find(entry.id());
		if (it == m_quests.end())
		{
			return false;
		}

		// Now check for quest requirements
		if (entry.requirements_size() == 0)
		{
			return true;
		}

		// Now check all available quest requirements
		uint32 counter = 0;
		for (const auto& req : entry.requirements())
		{
			// Creature kill / spell cast required
			if (req.creatureid() != 0)
			{
				if (it->second.creatures[counter] < req.creaturecount()) {
					return false;
				}
			}

			// Item required
			if (req.itemid() != 0)
			{
				// Not enough items? (Don't worry, getItemCount is fast as it uses a cached value)
				if (const auto itemCount = m_inventory.GetItemCount(req.itemid()); itemCount < req.itemcount())
				{
					return false;
				}
			}

			// Increase counter
			counter++;
		}

		return true;
	}

	bool GamePlayerS::IsQuestlogFull() const
	{
		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			// If this quest log slot is empty, we can stop as there is at least one
			// free quest slot available
			if (Get<uint32>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32))) == 0)
			{
				return false;
			}
		}

		// No free quest slots found
		return true;
	}

	void GamePlayerS::OnQuestExploration(uint32 questId)
	{
		// TODO
	}

	void GamePlayerS::OnQuestItemAddedCredit(const proto::ItemEntry& entry, uint32 amount)
	{
		// If this is set to true, all nearby objects will be updated
		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			QuestField field = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (field.questId == 0)
			{
				continue;
			}

			// Check if the player really has accepted that quest
			auto it = m_quests.find(field.questId);
			if (it == m_quests.end())
			{
				continue;
			}

			// Check if the quest was already completed
			if (it->second.status != quest_status::Incomplete) 
			{
				continue;
			}

			// Get quest template entry
			const auto* quest = GetProject().quests.getById(field.questId);
			if (!quest)
			{
				continue;
			}

			// If this is set to true, all requirements of this quest will be reevaluated, which costs
			// some time. So this variable is only updated, if a quest requirement status changed between
			// Completed and Uncomplete to save performance.
			bool validateQuest = false;

			// Check every quest entry requirement
			for (const auto& req : quest->requirements())
			{
				if (req.itemid() == entry.id())
				{
					if (m_inventory.GetItemCount(entry.id()) >= req.itemcount())
					{
						validateQuest = true;
					}
				}
				else if (req.sourceid() == entry.id())
				{
					if (m_inventory.GetItemCount(entry.id()) >= req.sourcecount())
					{
						validateQuest = true;
					}
				}
			}

			// Check if the quest requirements need to be reevaluated
			if (validateQuest)
			{
				// Quest is fulfilled now
				if (FulfillsQuestRequirements(*quest))
				{
					it->second.status = quest_status::Complete;
					field.status = quest_status::Complete;
					if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(field.questId, it->second);
				}

				Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), field);
			}
		}
	}

	void GamePlayerS::OnQuestItemRemovedCredit(const proto::ItemEntry& entry, uint32 amount)
	{
		// If this is set to true, all nearby objects will be updated
		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			QuestField field = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (field.questId == 0)
			{
				continue;
			}

			// Check if the player really has accepted that quest
			auto it = m_quests.find(field.questId);
			if (it == m_quests.end())
			{
				continue;
			}

			// Check if the quest was already completed
			if (it->second.status != quest_status::Incomplete)
			{
				continue;
			}

			// Get quest template entry
			const auto* quest = GetProject().quests.getById(field.questId);
			if (!quest)
			{
				continue;
			}

			// If this is set to true, all requirements of this quest will be reevaluated, which costs
			// some time. So this variable is only updated, if a quest requirement status changed between
			// Completed and Uncomplete to save performance.
			bool validateQuest = false;

			// Check every quest entry requirement
			for (const auto& req : quest->requirements())
			{
				if (req.itemid() == entry.id())
				{
					if (m_inventory.GetItemCount(entry.id()) < req.itemcount())
					{
						validateQuest = true;
					}
				}
				else if (req.sourceid() == entry.id())
				{
					if (m_inventory.GetItemCount(entry.id()) < req.sourcecount())
					{
						validateQuest = true;
					}
				}
			}

			// Check if the quest requirements need to be reevaluated
			if (validateQuest)
			{
				// Quest is fulfilled now
				if (!FulfillsQuestRequirements(*quest))
				{
					it->second.status = quest_status::Incomplete;
					field.status = quest_status::Incomplete;
					if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(field.questId, it->second);
				}

				Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), field);
			}
		}
	}

	void GamePlayerS::OnQuestSpellCastCredit(uint32 spellId, GameObjectS& target)
	{
		// TODO
	}

	bool GamePlayerS::NeedsQuestItem(uint32 itemId) const
	{
		// TODO

		return false;
	}

	float GamePlayerS::GetUnitMissChance() const
	{
		return GameUnitS::GetUnitMissChance();
	}

	bool GamePlayerS::HasOffhandWeapon() const
	{
		if (m_inventory.GetWeaponByAttackType(weapon_attack::OffhandAttack, false, true))
		{
			return true;
		}

		return GameUnitS::HasOffhandWeapon();
	}

	void GamePlayerS::NotifyQuestRewarded(const uint32 questId)
	{
		ASSERT(!m_rewardedQuestIds.contains(questId));

		// Add to the list of completed quest ids
		m_rewardedQuestIds.insert(questId);

		// Remove from quest log
		if (auto it = m_quests.find(questId); it != m_quests.end())
		{
			it = m_quests.erase(it);
		}

		// Persist in database
		QuestStatusData completed;
		completed.status = quest_status::Rewarded;
		if (m_netPlayerWatcher) m_netPlayerWatcher->OnQuestDataChanged(questId, completed);
	}

	void GamePlayerS::SetQuestData(uint32 questId, const QuestStatusData& data)
	{
		m_quests[questId] = data;

		for (uint8 i = 0; i < MaxQuestLogSize; ++i)
		{
			QuestField field = Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));
			if (field.questId == 0 || field.questId == questId)
			{
				field.questId = questId;
				field.status = data.status;
				field.questTimer = data.expiration;
				for (uint8 j = 0; j < 4; ++j)
				{
					field.counters[j] = data.creatures[j];
				}
				Set<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)), field, false);
				break;
			}
		}
	}

	bool GamePlayerS::AddAttributePoint(const uint32 attribute)
	{
		// First determine attribute point cost
		const uint8 cost = CalculateAttributeCost(m_attributePointEnhancements[attribute]);

		// Determine available attribute points
		const uint32 attributePoints = Get<uint32>(object_fields::AvailableAttributePoints);
		if (cost > attributePoints)
		{
			return false;
		}

		// Add point spent
		m_attributePointEnhancements[attribute]++;
		m_attributePointsSpent[attribute] += cost;
		RefreshStats();

		return true;
	}

	void GamePlayerS::RewardExperience(const uint32 xp)
	{
		// At max level we can't gain any more xp
		if (Get<uint32>(object_fields::Level) >= Get<uint32>(object_fields::MaxLevel))
		{
			return;
		}

		uint32 currentXp = Get<uint32>(object_fields::Xp);
		currentXp += xp;

		// Levelup as often as required
		while(currentXp >= Get<uint32>(object_fields::NextLevelXp))
		{
			if (GetLevel() < Get<uint32>(object_fields::MaxLevel))
			{
				if (m_netUnitWatcher)
				{
					const auto& levelStats = m_classEntry->levelbasevalues(GetLevel() - 1);
					const auto& nextLevelStats = m_classEntry->levelbasevalues(GetLevel());
					m_netUnitWatcher->OnLevelUp(GetLevel() + 1,
						static_cast<int32>(nextLevelStats.health()) - static_cast<int32>(levelStats.health()),
						static_cast<int32>(nextLevelStats.mana()) - static_cast<int32>(levelStats.mana()),
						static_cast<int32>(nextLevelStats.stamina()) - static_cast<int32>(levelStats.stamina()),
						static_cast<int32>(nextLevelStats.strength()) - static_cast<int32>(levelStats.strength()),
						static_cast<int32>(nextLevelStats.agility()) - static_cast<int32>(levelStats.agility()),
						static_cast<int32>(nextLevelStats.intellect()) - static_cast<int32>(levelStats.intellect()),
						static_cast<int32>(nextLevelStats.spirit()) - static_cast<int32>(levelStats.spirit()),
						nextLevelStats.talentpoints(),
						nextLevelStats.attributepoints()
					);
				}
				
				currentXp -= Get<uint32>(object_fields::NextLevelXp);
				SetLevel(GetLevel() + 1);
			}
		}

		// Store remaining xp after potential levelups
		Set<uint32>(object_fields::Xp, currentXp);

		// Send packet
		if (m_netUnitWatcher)
		{
			m_netUnitWatcher->OnXpLog(xp);
		}
	}

	void GamePlayerS::RefreshStats()
	{
		ASSERT(m_classEntry);

		GameUnitS::RefreshStats();

		const int32 level = Get<int32>(object_fields::Level);
		ASSERT(level > 0);
		ASSERT(level <= m_classEntry->levelbasevalues_size());

		// Adjust stats
		UpdateAttributePoints();

		const auto* levelStats = &m_classEntry->levelbasevalues(level - 1);
		SetModifierValue(GetUnitModByStat(0), unit_mod_type::BaseValue, levelStats->stamina() + m_attributePointEnhancements[0]);
		SetModifierValue(GetUnitModByStat(1), unit_mod_type::BaseValue, levelStats->strength() + m_attributePointEnhancements[1]);
		SetModifierValue(GetUnitModByStat(2), unit_mod_type::BaseValue, levelStats->agility() + m_attributePointEnhancements[2]);
		SetModifierValue(GetUnitModByStat(3), unit_mod_type::BaseValue, levelStats->intellect() + m_attributePointEnhancements[3]);
		SetModifierValue(GetUnitModByStat(4), unit_mod_type::BaseValue, levelStats->spirit() + m_attributePointEnhancements[4]);

		// Update all stats
		for (int i = 0; i < 5; ++i)
		{
			UpdateStat(i);
		}

		UpdateArmor();

		// TODO: Apply item stats

		uint32 maxHealth = 0;
		uint32 maxMana = 0;
		if (m_classEntry)
		{
			for (int i = 0; i < m_classEntry->healthstatsources_size(); ++i)
			{
				const auto& statSource = m_classEntry->healthstatsources(i);
				if (statSource.statid() < 5)
				{
					maxHealth += UnitStats::DeriveFromBaseWithFactor(Get<uint32>(object_fields::StatStamina + statSource.statid()), 20, statSource.factor());
				}
			}

			for (int i = 0; i < m_classEntry->manastatsources_size(); ++i)
			{
				const auto& statSource = m_classEntry->manastatsources(i);
				if (statSource.statid() < 5)
				{
					maxMana += UnitStats::DeriveFromBaseWithFactor(Get<uint32>(object_fields::StatStamina + statSource.statid()), 20, statSource.factor());
				}
			}
		}

		// Calculate max health from stats
		maxHealth += levelStats->health();
		Set<uint32>(object_fields::MaxHealth, maxHealth);

		// Ensure health is properly capped by max health
		if (Get<uint32>(object_fields::Health) > maxHealth)
		{
			Set<uint32>(object_fields::Health, maxHealth);
		}

		maxMana += levelStats->mana();
		Set<uint32>(object_fields::MaxMana, maxMana);

		// Ensure mana is properly capped by max health
		if (Get<uint32>(object_fields::Mana) > maxMana)
		{
			Set<uint32>(object_fields::Mana, maxMana);
		}

		if (m_classEntry->spiritperhealthregen() != 0.0f)
		{
			m_healthRegenPerTick = (static_cast<float>(Get<uint32>(object_fields::StatSpirit)) / m_classEntry->spiritperhealthregen());
		}
		m_healthRegenPerTick += m_classEntry->healthregenpertick();
		if (m_healthRegenPerTick < 0.0f)
		{
			m_healthRegenPerTick = 0.0f;
		}
		
		if (m_classEntry->spiritpermanaregen() != 0.0f)
		{
			m_manaRegenPerTick = (static_cast<float>(Get<uint32>(object_fields::StatSpirit)) / m_classEntry->spiritpermanaregen());
		}
		m_manaRegenPerTick += m_classEntry->basemanaregenpertick();
		if (m_manaRegenPerTick < 0.0f)
		{
			m_manaRegenPerTick = 0.0f;
		}

		UpdateDamage();
	}

	void GamePlayerS::SetLevel(uint32 newLevel)
	{
		// Anything to do?
		if (newLevel == 0)
		{
			return;
		}

		ASSERT(m_classEntry);

		// Exceed max level?
		newLevel = std::min(newLevel, Get<uint32>(object_fields::MaxLevel));

		// Calculate total attribute points available at level
		m_totalAvailablePointsAtLevel = 0;
		for (uint32 i = 0; i < newLevel; ++i)
		{
			m_totalAvailablePointsAtLevel += m_classEntry->levelbasevalues(i).attributepoints();
		}

		// Adjust stats
		GameUnitS::SetLevel(newLevel);

		// Update next level xp
		uint32 xpToNextLevel = 400 * newLevel;		// Dummy default value
		if (static_cast<int32>(newLevel) - 1 >= m_classEntry->xptonextlevel_size())
		{
			if (m_classEntry->xptonextlevel_size() == 0)
			{
				WLOG("Class " << m_classEntry->name() << " has no experience points per level set, a default value will be used!");
			}
			else
			{
				WLOG("Class " << m_classEntry->name() << " has no experience points per level set for level " << newLevel << ", value from last level will be used!");
				xpToNextLevel = m_classEntry->xptonextlevel(m_classEntry->xptonextlevel_size() - 1);
			}
		}
		else
		{
			xpToNextLevel = m_classEntry->xptonextlevel(static_cast<int32>(newLevel) - 1);
		}

		Set<uint32>(object_fields::NextLevelXp, xpToNextLevel);
	}

	void GamePlayerS::UpdateDamage()
	{
		uint32 attackSpeed = 2000;
		float minDamage= 1.0f;
		float maxDamage = 2.0f;

		// Derive min and max damage from wielded weapon if any
		std::shared_ptr<GameItemS> mainHandWeapon = m_inventory.GetWeaponByAttackType(weapon_attack::BaseAttack, true, true);
		if (mainHandWeapon != nullptr)
		{
			if (mainHandWeapon->GetEntry().has_damage())
			{
				minDamage = mainHandWeapon->GetEntry().damage().mindmg();
				maxDamage = mainHandWeapon->GetEntry().damage().maxdmg();
			}

			attackSpeed = mainHandWeapon->GetEntry().delay();
			if (attackSpeed == 0)
			{
				WLOG("Weapon " << mainHandWeapon->GetEntry().id() << " [" << mainHandWeapon->GetEntry().name() << "] had a delay of 0, using base attack speed instead!");
				attackSpeed = 2000;
			}
		}

		// Update attack speed value
		Set<uint32>(object_fields::BaseAttackTime, attackSpeed);

		float baseValue = 0.0f;

		// Calculate base value base on class
		if (m_classEntry)
		{
			baseValue = static_cast<float>(Get<uint32>(object_fields::Level)) * m_classEntry->attackpowerperlevel();

			// Apply stat values
			for(int i = 0; i < m_classEntry->attackpowerstatsources_size(); ++i)
			{
				const auto& statSource = m_classEntry->attackpowerstatsources(i);
				if (statSource.statid() < 5)
				{
					baseValue += static_cast<float>(Get<uint32>(object_fields::StatStamina + statSource.statid())) * statSource.factor();
				}
			}

			baseValue += m_classEntry->attackpoweroffset();
		}

		Set<float>(object_fields::AttackPower, baseValue);

		// 1 dps per 14 attack power
		const float attackTime = Get<uint32>(object_fields::BaseAttackTime) / 1000.0f;
		baseValue = baseValue / 14.0f * attackTime;

		Set<float>(object_fields::MinDamage, baseValue + minDamage);
		Set<float>(object_fields::MaxDamage, baseValue + maxDamage);
	}

	void GamePlayerS::UpdateArmor()
	{
		// Update armor value
		int32 baseArmor = static_cast<int32>(GetModifierValue(unit_mods::Armor, unit_mod_type::BaseValue));
		const int32 totalArmor = static_cast<int32>(GetModifierValue(unit_mods::Armor, unit_mod_type::TotalValue));

		// Class
		if (m_classEntry)
		{
			// Apply stat values
			for (int i = 0; i < m_classEntry->armorstatsources_size(); ++i)
			{
				const auto& statSource = m_classEntry->armorstatsources(i);
				if (statSource.statid() < 5)
				{
					baseArmor += static_cast<float>(Get<uint32>(object_fields::StatStamina + statSource.statid())) * statSource.factor();
				}
			}

		}

		Set<int32>(object_fields::Armor, baseArmor + totalArmor);
		Set<int32>(object_fields::PosStatArmor, totalArmor > 0 ? totalArmor : 0);
		Set<int32>(object_fields::NegStatArmor, totalArmor < 0 ? totalArmor : 0);
	}

	void GamePlayerS::UpdateAttributePoints()
	{
		// Calculate available attribute points
		uint32 availableAttributePoints = m_totalAvailablePointsAtLevel;
		for (uint32 i = 0; i < m_attributePointEnhancements.size(); ++i)
		{
			const uint32 realPointsSpent = m_attributePointsSpent[i];
			if (realPointsSpent > availableAttributePoints)
			{
				// Points spent exceeds available points - reset everything!
				WLOG("Points spent is bigger than points available! Resetting spent points...");
				m_attributePointEnhancements.fill(0);
				availableAttributePoints = m_totalAvailablePointsAtLevel;
				break;
			}

			availableAttributePoints -= realPointsSpent;
		}

		// Update available attribute points
		Set<uint32>(object_fields::AvailableAttributePoints, availableAttributePoints);

		// Update attribute point costs
		for (uint32 i = 0; i < m_attributePointEnhancements.size(); ++i)
		{
			const uint8 cost = CalculateAttributeCost(m_attributePointEnhancements[i]);
			SetAttributeCost(i, cost);
		}
	}

	void GamePlayerS::UpdateStat(const int32 stat)
	{
		// Validate stat
		if (stat > 4)
		{
			return;
		}

		// Determine unit mod
		const UnitMods mod = GetUnitModByStat(stat);

		// Calculate values
		const float totalVal = GetModifierValue(mod, unit_mod_type::TotalValue);
		const float value = GetCalculatedModifierValue(mod);

		Set<int32>(object_fields::StatStamina + stat, static_cast<int32>(value));
		Set<int32>(object_fields::PosStatStamina + stat, totalVal > 0 ? static_cast<int32>(totalVal) : 0);
		Set<int32>(object_fields::NegStatStamina + stat, totalVal < 0 ? static_cast<int32>(totalVal) : 0);
	}

	void GamePlayerS::RecalculateTotalAttributePointsConsumed(const uint32 attribute)
	{
		ASSERT(attribute < 5);

		m_attributePointsSpent[attribute] = 0;

		for (uint32 i = 1; i <= m_attributePointEnhancements[i]; ++i)
		{
			m_attributePointsSpent[attribute] += CalculateAttributeCost(i - 1);
		}
	}

	io::Writer& operator<<(io::Writer& w, GamePlayerS const& object)
	{
		// Write super class data
		w << reinterpret_cast<GameUnitS const&>(object);
		w << object.m_inventory;
		w << io::write_range(object.m_attributePointEnhancements);

		// Write known spell ids
		w << io::write<uint32>(object.m_spells.size());
		for (const auto& spell : object.m_spells)
		{
			w << io::write<uint32>(spell->id());
		}

		return w;
	}

	io::Reader& operator>>(io::Reader& r, GamePlayerS& object)
	{
		// Read super class data
		r >> reinterpret_cast<GameUnitS&>(object);
		r >> object.m_inventory;
		r >> io::read_range(object.m_attributePointEnhancements);

		// Read spells
		uint32 spellCount;
		r >> io::read<uint32>(spellCount);
		object.m_spells.clear();
		for (uint32 i = 0; i < spellCount; ++i)
		{
			uint32 spellId;
			r >> io::read<uint32>(spellId);

			const auto* spell = object.GetProject().spells.getById(spellId);
			if (spell)
			{
				object.m_spells.emplace(spell);
			}
		}

		for (uint32 i = 0; i < 5; ++i)
		{
			object.RecalculateTotalAttributePointsConsumed(i);
		}

		return r;
	}
}
