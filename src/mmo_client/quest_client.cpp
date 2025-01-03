
#include "quest_client.h"

#include "frame_ui/frame_mgr.h"
#include "game_client/game_player_c.h"
#include "game_client/object_mgr.h"

namespace mmo
{
	QuestClient::QuestClient(RealmConnector& connector, DBQuestCache& questCache, const proto_client::SpellManager& spells, DBItemCache& itemCache, DBCreatureCache& creatureCache, const Localization& localization)
		: m_connector(connector)
		, m_questCache(questCache)
		, m_spells(spells)
		, m_itemCache(itemCache)
		, m_creatureCache(creatureCache)
		, m_localization(localization)
	{
	}

	void QuestClient::Initialize()
	{
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestGiverQuestList, *this, &QuestClient::OnQuestGiverQuestList);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestGiverQuestDetails, *this, &QuestClient::OnQuestGiverQuestDetails);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestGiverQuestComplete, *this, &QuestClient::OnQuestGiverQuestComplete);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestGiverOfferReward, *this, &QuestClient::OnQuestGiverOfferReward);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestGiverRequestItems, *this, &QuestClient::OnQuestGiverRequestItems);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestUpdateAddItem, *this, &QuestClient::OnQuestUpdate);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestUpdateAddKill, *this, &QuestClient::OnQuestUpdate);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestUpdateComplete, *this, &QuestClient::OnQuestUpdate);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestLogFull, *this, &QuestClient::OnQuestLogFull);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GossipComplete, *this, &QuestClient::OnGossipComplete);
		m_packetHandlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::QuestQueryResult, *this, &QuestClient::OnQuestQueryResult);
	}

	void QuestClient::Shutdown()
	{
		m_packetHandlers.Clear();

		CloseQuest();
	}

	void QuestClient::CloseQuest()
	{
		m_questGiverGuid = 0;
		m_questDetails.Clear();
		m_greetingText.clear();
		m_questList.clear();

		// Raise UI event to show the quest list window to the user
		FrameManager::Get().TriggerLuaEvent("QUEST_FINISHED");
	}

	const String& QuestClient::GetGreetingText() const
	{
		ASSERT(HasQuestGiver());

		return m_greetingText;
	}

	void QuestClient::QueryQuestDetails(uint32 questId)
	{
		ASSERT(questId != 0);
		ASSERT(HasQuestGiver());

		const auto it = std::find_if(m_questList.begin(), m_questList.end(), [questId](const QuestListEntry& entry)
		{
			return entry.questId == questId;
		});
		if (it == m_questList.end())
		{
			ELOG("Unable to query quest details for quest " << questId << ": Quest not in quest list");
			return;
		}

		if (it->isActive)
		{
			m_connector.QuestGiverCompleteQuest(m_questGiverGuid, questId);
		}
		else
		{
			m_connector.QuestGiverQueryQuest(m_questGiverGuid, questId);
		}
	}

	void QuestClient::AcceptQuest(const uint32 questId)
	{
		ASSERT(questId != 0);
		ASSERT(HasQuestGiver());
		ASSERT(HasQuest());

		m_connector.AcceptQuest(m_questGiverGuid, questId);
	}

	void QuestClient::UpdateQuestLog(const GamePlayerC& player)
	{
		m_questLogQuests.clear();

		bool relevantQuestChanges = false;
		bool selectedQuestExisted = false;

		for (uint32 i = 0; i < MaxQuestLogSize; ++i)
		{
			const QuestField field = player.Get<QuestField>(object_fields::QuestLogSlot_1 + i * (sizeof(QuestField) / sizeof(uint32)));

			if (!selectedQuestExisted && m_selectedQuestLogQuest != 0 && field.questId == m_selectedQuestLogQuest)
			{
				selectedQuestExisted = true;
			}

			// Quest id changed?
			if (field.questId != m_questLog[i].questId)
			{
				relevantQuestChanges = true;

				m_questLog[i].questId = field.questId;
				m_questLog[i].quest = nullptr;
				m_questLog[i].status = static_cast<QuestStatus>(field.status);
				std::memcpy(m_questLog[i].counters, field.counters, sizeof(m_questLog[i].counters));
				if (m_questLog[i].questId != 0)
				{
					m_questCache.Get(m_questLog[i].questId, [this, i](uint64 entry, const QuestInfo& info)
					{
						if (m_questLog[i].questId == entry)
						{
							m_questLog[i].quest = &info;
						}
					});
				}
			}
			else if (field.questId != 0)
			{
				// Did the quest status change?
				if (m_questLog[i].status != field.status)
				{
					relevantQuestChanges = true;
				}

				// Update counters
				m_questLog[i].status = static_cast<QuestStatus>(field.status);
				std::memcpy(m_questLog[i].counters, field.counters, sizeof(m_questLog[i].counters));
			}

			if(field.questId != 0)
			{
				m_questLogQuests.push_back(i);
			}
		}

		if (!selectedQuestExisted)
		{
			m_selectedQuestLogQuest = 0;
		}
		else
		{
			// Refresh objective text caches
			QuestLogSelectQuest(m_selectedQuestLogQuest);
		}

		// Either we got new quests, abandoned old ones or the status of a quest changed
		if (relevantQuestChanges)
		{
			RefreshQuestGiverStatus();
		}

		FrameManager::Get().TriggerLuaEvent("QUEST_LOG_UPDATE");
	}

	const QuestLogEntry* QuestClient::GetQuestLogEntry(uint32 index) const
	{
		if (index >= m_questLogQuests.size())
		{
			return nullptr;
		}

		return m_questLog.data() + m_questLogQuests[index];
	}

	void QuestClient::ProcessQuestText(String& questText)
	{
		std::shared_ptr<GamePlayerC> player = std::static_pointer_cast<GamePlayerC, GameUnitC>(ObjectMgr::GetActivePlayer());
		ASSERT(player);

		// TODO: Make class string data driven
		static const String s_classNames[] = { "Mage", "Warrior", "Cleric", "Shadowmancer" };

		// Iterate over quest text until we find a '$' character. Take the special character after it and replace it with a dynamic value
		for (auto it = questText.begin(); it != questText.end();)
		{
			// Search for the first occurence of '$'
			if (*it != '$')
			{
				++it;
				continue;
			}

			// Remove the '$' character first
			it = questText.erase(it);

			// We found it, now check the next character
			if (it == questText.end())
			{
				break;
			}

			// Get next character and erase it as well
			const char command = *it;
			it = questText.erase(it);

			switch (command)
			{
			case 'n':
			case 'N':
				it = questText.insert(it, player->GetName().begin(), player->GetName().end());
				break;
			case 'c':
			case 'C':
				{
					const uint32 classId = player->Get<uint32>(object_fields::Class);
					ASSERT(classId < std::size(s_classNames));
					it = questText.insert(it, s_classNames[classId].begin(), s_classNames[classId].end());
				}
				break;
			case 'r':
			case 'R':
				{
					// TODO: Use real race data driven instead of hard coded
					static const String s_raceName = "Human";
					it = questText.insert(it, s_raceName.begin(), s_raceName.end());
				}
				break;
			default:
				break;
			}
		}
	}

	void QuestClient::QuestLogSelectQuest(uint32 questId)
	{
		m_questObjectiveTexts.clear();

		// Check if we know this quest
		const auto questLogEntryIt = std::find_if(m_questLog.begin(), m_questLog.end(), [questId](const QuestLogEntry& entry)
			{
				return entry.questId == questId;
			});

		if (questLogEntryIt == m_questLog.end())
		{
			ELOG("Quest " << questId << "is not in quest log");
			return;
		}

		// Check for quest details
		const QuestInfo* quest = m_questCache.Get(questId);
		if (!quest)
		{
			ELOG("Unknown quest " << questId);
			return;
		}

		m_selectedQuestLogQuest = questId;

		const String* monstersKilledFormat = m_localization.FindStringById("QUEST_MONSTERS_KILLED");
		const String* itemsGatheredFormat = m_localization.FindStringById("QUEST_ITEMS_NEEDED");
		const String* complete = m_localization.FindStringById("COMPLETE");

		char buffer[512];

		// Check for required creatures
		int counter = 0;
		for (const auto& creature : quest->requiredCreatures)
		{
			const CreatureInfo* creatureEntry = m_creatureCache.Get(creature.creatureId);
			if (!creatureEntry)
			{
				ELOG("Unknown creature " << creature.creatureId);
				continue;
			}

			if (monstersKilledFormat)
			{
				ASSERT(counter < 4);
				snprintf(buffer, 512, monstersKilledFormat->c_str(), creatureEntry->name.c_str(), questLogEntryIt->counters[counter++], creature.count);
			}
			else
			{
				snprintf(buffer, 512, "QUEST_MONSTERS_KILLED");
			}

			m_questObjectiveTexts.emplace_back(buffer);
		}

		for (const auto& item : quest->requiredItems)
		{
			const ItemInfo* itemEntry = m_itemCache.Get(item.itemId);
			if (!itemEntry)
			{
				ELOG("Unknown item " << item.itemId);
				continue;
			}

			if (itemsGatheredFormat)
			{
				const uint32 itemCount = std::min(item.count, ObjectMgr::GetItemCount(item.itemId));
				snprintf(buffer, 512, itemsGatheredFormat->c_str(), itemEntry->name.c_str(), itemCount, item.count);
			}
			else
			{
				snprintf(buffer, 512, "QUEST_ITEMS_NEEDED");
			}

			m_questObjectiveTexts.emplace_back(buffer);
		}
	}

	uint32 QuestClient::GetSelectedQuestLogQuest() const
	{
		return m_selectedQuestLogQuest;
	}

	uint32 QuestClient::GetQuestObjectiveCount() const
	{
		return m_questObjectiveTexts.size();
	}

	const char* QuestClient::GetQuestObjectiveText(uint32 i)
	{
		if (i >= m_questObjectiveTexts.size())
		{
			return nullptr;
		}

		return m_questObjectiveTexts[i].c_str();
	}

	bool QuestClient::HasQuestInQuestLog(uint32 questId)
	{
		// Check if we know that quest
		const auto it = std::find_if(m_questLog.begin(), m_questLog.end(), [questId](const QuestLogEntry& entry)
			{
				return entry.questId == questId;
			});

		return it != m_questLog.end();
	}

	void QuestClient::RefreshQuestGiverStatus()
	{
		ObjectMgr::ForEachObject<GameUnitC>([this](const std::shared_ptr<GameUnitC>& unit)
			{
				if (!unit)
				{
					return;
				}

				if (unit->Get<uint32>(object_fields::NpcFlags) & npc_flags::QuestGiver)
				{
					m_connector.UpdateQuestStatus(unit->GetGuid());
				}
			});
	}

	void QuestClient::AbandonQuest(uint32 questId)
	{
		// Check if we know that quest
		if (const auto it = std::find_if(m_questLog.begin(), m_questLog.end(), [questId](const QuestLogEntry& entry)
		{
			return entry.questId == questId;
		}); it == m_questLog.end())
		{
			ELOG("Unable to abandon quest " << questId << ": Quest not in quest log");
			return;
		}

		m_connector.AbandonQuest(questId);
	}

	void QuestClient::GetQuestReward(const uint32 rewardChoice) const
	{
		ASSERT(HasQuestGiver());
		ASSERT(HasQuest());

		m_connector.QuestGiverChooseQuestReward(m_questGiverGuid, m_questDetails.questId, rewardChoice);
	}

	PacketParseResult QuestClient::OnQuestGiverQuestList(game::IncomingPacket& packet)
	{
		m_questList.clear();

		uint8 numQuests;

		if (!(packet
			>> io::read<uint64>(m_questGiverGuid)
			>> io::read_container<uint16>(m_greetingText, 512)
			>> io::read<uint8>(numQuests)))
		{
			ELOG("Failed to read QuestGiverQuestList packet");
			return PacketParseResult::Disconnect;
		}

		ProcessQuestText(m_greetingText);

		for (uint8 i = 0; i < numQuests; ++i)
		{
			QuestListEntry entry;
			if (!(packet
				>> io::read<uint32>(entry.questId)
				>> io::read<uint32>(entry.menuIcon)
				>> io::read<int32>(entry.questLevel)
				>> io::read_container<uint8>(entry.questTitle)))
			{
				m_questList.clear();

				ELOG("Failed to read QuestList entry");
				return PacketParseResult::Disconnect;
			}

			auto questLogIt = std::find_if(m_questLog.begin(), m_questLog.end(), [this, &entry](const QuestLogEntry& logEntry)
				{
					if (logEntry.questId == entry.questId)
					{
						return true;
					}
					return false;
				});
			entry.isActive = questLogIt != m_questLog.end();

			// Ensure we have the quest in the cache
			m_questCache.Get(entry.questId);

			// Add to list of quests
			m_questList.emplace_back(std::move(entry));
		}

		// Raise UI event to show the quest list window to the user
		FrameManager::Get().TriggerLuaEvent("QUEST_GREETING");

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestGiverQuestDetails(game::IncomingPacket& packet)
	{
		m_questDetails.Clear();

		if (!(packet >> io::read<uint64>(m_questGiverGuid) >> io::read<uint32>(m_questDetails.questId)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		if (!(packet 
				>> io::read_container<uint8>(m_questDetails.questTitle)
				>> io::read_container<uint16>(m_questDetails.questDetails, 512)
				>> io::read_container<uint16>(m_questDetails.questObjectives, 512)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		// Process quest text
		ProcessQuestText(m_questDetails.questDetails);
		ProcessQuestText(m_questDetails.questObjectives);

		uint32 rewardItemsChoiceCount;
		uint32 rewardItemsCount;

		if (!(packet >> io::read<uint32>(rewardItemsChoiceCount)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		if (rewardItemsChoiceCount > 0)
		{
			uint32 itemId, count, displayId;
			if (!(packet >> io::read<uint32>(itemId) >> io::read<uint32>(count) >> io::read<uint32>(displayId)))
			{
				ELOG("Failed to read QuestGiverQuestDetails packet");
				return PacketParseResult::Disconnect;
			}

			if (itemId != 0)
			{
				m_itemCache.Get(itemId);
			}
		}

		if (!(packet >> io::read<uint32>(rewardItemsCount)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		if (rewardItemsCount > 0)
		{
			uint32 itemId, count, displayId;
			if (!(packet >> io::read<uint32>(itemId) >> io::read<uint32>(count) >> io::read<uint32>(displayId)))
			{
				ELOG("Failed to read QuestGiverQuestDetails packet");
				return PacketParseResult::Disconnect;
			}

			if (itemId != 0)
			{
				m_itemCache.Get(itemId);
			}
		}

		uint32 rewardSpellId = 0;
		if (!(packet >> io::read<uint32>(m_questDetails.rewardMoney) >> io::read<uint32>(rewardSpellId)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		// Resolve reward spell
		m_questDetails.rewardSpell = (rewardSpellId != 0) ? m_spells.getById(rewardSpellId) : nullptr;

		// Ensure we have the quest in the cache
		m_questCache.Get(m_questDetails.questId);

		// Raise UI event to show the quest list window to the user
		FrameManager::Get().TriggerLuaEvent("QUEST_DETAIL");

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestGiverQuestComplete(game::IncomingPacket& packet)
	{
		m_questDetails.Clear();

		if (!(packet >> io::read<uint64>(m_questGiverGuid) >> io::read<uint32>(m_questDetails.questId)))
		{
			ELOG("Failed to read QuestGiverOfferReward packet");
			return PacketParseResult::Disconnect;
		}

		uint32 rewardXp, rewardMoney;
		if (!(packet >> io::read<uint32>(rewardXp) >> io::read<uint32>(rewardMoney)))
		{
			ELOG("Failed to read QuestGiverOfferReward packet");
			return PacketParseResult::Disconnect;
		}

		const QuestInfo* quest = m_questCache.Get(m_questDetails.questId);
		if (!quest)
		{
			ELOG("Unknown quest completed: " << m_questDetails.questId);
		}
		else
		{
			// Trigger event
			FrameManager::Get().TriggerLuaEvent("QUEST_REWARDED", quest->title, rewardXp, rewardMoney);
		}

		// Quest done
		CloseQuest();

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestGiverOfferReward(game::IncomingPacket& packet)
	{
		m_questDetails.Clear();

		if (!(packet >> io::read<uint64>(m_questGiverGuid) >> io::read<uint32>(m_questDetails.questId)))
		{
			ELOG("Failed to read QuestGiverOfferReward packet");
			return PacketParseResult::Disconnect;
		}

		if (!(packet
			>> io::read_container<uint8>(m_questDetails.questTitle)
			>> io::read_container<uint16>(m_questDetails.questOfferRewardText, 512)))
		{
			ELOG("Failed to read QuestGiverOfferReward packet");
			return PacketParseResult::Disconnect;
		}

		// Process quest text
		ProcessQuestText(m_questDetails.questOfferRewardText);

		// Raise UI event to show the quest list window to the user
		FrameManager::Get().TriggerLuaEvent("QUEST_OFFER_REWARDS");

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestGiverRequestItems(game::IncomingPacket& packet)
	{
		m_questDetails.Clear();

		if (!(packet >> io::read<uint64>(m_questGiverGuid) >> io::read<uint32>(m_questDetails.questId)))
		{
			ELOG("Failed to read QuestGiverRequestItems packet");
			return PacketParseResult::Disconnect;
		}

		if (!(packet
			>> io::read_container<uint8>(m_questDetails.questTitle)
			>> io::read_container<uint16>(m_questDetails.questRequestItemsText, 512)))
		{
			ELOG("Failed to read QuestGiverRequestItems packet");
			return PacketParseResult::Disconnect;
		}

		// Process quest text
		ProcessQuestText(m_questDetails.questRequestItemsText);

		// Raise UI event to show the quest list window to the user
		FrameManager::Get().TriggerLuaEvent("QUEST_REQUEST_ITEMS");

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestUpdate(game::IncomingPacket& packet)
	{
		uint32 questId;
		if (!(packet >> io::read<uint32>(questId)))
		{
			ELOG("Failed to read QuestUpdate packet");
			return PacketParseResult::Disconnect;
		}

		// Get quest data
		const QuestInfo* quest = m_questCache.Get(questId);
		if (!quest)
		{
			ELOG("Received quest event for unknown quest " << questId);
			return PacketParseResult::Pass;
		}

		switch(packet.GetId())
		{
		case game::realm_client_packet::QuestUpdateAddItem:

			break;
		case game::realm_client_packet::QuestUpdateAddKill:
		{
			uint32 entry, count, maxCount;
			uint64 guid;
			if (!(packet >> io::read<uint32>(entry) >> io::read<uint32>(count) >> io::read<uint32>(maxCount) >> io::read<uint64>(guid)))
			{
				ELOG("Failed to read QuestUpdateAddKill packet");
				return PacketParseResult::Disconnect;
			}

			ASSERT(entry);

			static const String s_monsterKilledFormat = "QUEST_MONSTERS_KILLED";
			const String* format = m_localization.FindStringById("QUEST_MONSTERS_KILLED");
			if (!format) format = &s_monsterKilledFormat;

			const CreatureInfo* creature = m_creatureCache.Get(entry);

			char buffer[512];
			snprintf(buffer, 512, format->c_str(), creature ? creature->name.c_str() : "UNKNOWN", count, maxCount);
			FrameManager::Get().TriggerLuaEvent("UI_INFO_MESSAGE", buffer);
		}
			break;
		case game::realm_client_packet::QuestUpdateComplete:
			{
			static const String s_monsterKilledFormat = "QUEST_REWARDED_TITLE";
			const String* format = m_localization.FindStringById("QUEST_REWARDED_TITLE");
			if (!format) format = &s_monsterKilledFormat;

			char buffer[512];
			snprintf(buffer, 512, format->c_str(), quest->title.c_str());
			FrameManager::Get().TriggerLuaEvent("UI_INFO_MESSAGE", buffer);

			RefreshQuestGiverStatus();
		}
			break;
		default:
			ELOG("Unhandled packet op code in " << __FUNCTION__);
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestLogFull(game::IncomingPacket& packet)
	{
		// Add a UI event to display an error message
		FrameManager::Get().TriggerLuaEvent("GERR_QUEST_LOG_FULL");

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnGossipComplete(game::IncomingPacket& packet)
	{
		CloseQuest();

		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestQueryResult(game::IncomingPacket& packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet
			>> io::read_packed_guid(id)
			>> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Quest query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		QuestInfo entry{ id };
		if (!(packet >> entry))
		{
			ELOG("Failed to read quest data");
			return PacketParseResult::Disconnect;
		}

		// Ensure items are known
		for (auto& item : entry.requiredItems)
		{
			m_itemCache.Get(item.itemId);
		}
		for (auto& item : entry.rewardItems)
		{
			m_itemCache.Get(item.itemId);
		}
		for (auto& item : entry.optionalItems)
		{
			m_itemCache.Get(item.itemId);
		}

		// Ensure creatures are known
		for (auto& creature : entry.requiredCreatures)
		{
			m_creatureCache.Get(creature.creatureId);
		}

		m_questCache.NotifyObjectResponse(id, std::move(entry));
		return PacketParseResult::Pass;
	}
}
