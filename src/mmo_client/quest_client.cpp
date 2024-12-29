
#include "quest_client.h"

#include "frame_ui/frame_mgr.h"

namespace mmo
{
	QuestClient::QuestClient(RealmConnector& connector, DBQuestCache& questCache, const proto_client::SpellManager& spells)
		: m_connector(connector)
		, m_questCache(questCache)
		, m_spells(spells)
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

		m_connector.QuestGiverQueryQuest(m_questGiverGuid, questId);
	}

	void QuestClient::AcceptQuest(uint32 questId)
	{
		ASSERT(questId != 0);
		ASSERT(HasQuestGiver());
		ASSERT(HasQuest());

		m_connector.AcceptQuest(m_questGiverGuid, questId);
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

		uint32 rewardItemsChoiceCount;
		uint32 rewardItemsCount;

		if (!(packet >> io::read<uint32>(rewardItemsChoiceCount)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		if (rewardItemsChoiceCount > 0)
		{
			// TODO: Read reward
			/*packet
				<< io::write<uint32>(reward.itemid())
				<< io::write<uint32>(reward.count());
			const auto* item = m_project.items.getById(reward.itemid());
			packet
				<< io::write<uint32>(item ? item->displayid() : 0);*/
		}

		if (!(packet >> io::read<uint32>(rewardItemsCount)))
		{
			ELOG("Failed to read QuestGiverQuestDetails packet");
			return PacketParseResult::Disconnect;
		}

		if (rewardItemsCount > 0)
		{
			// TODO: Read reward
			/*packet
				<< io::write<uint32>(reward.itemid())
				<< io::write<uint32>(reward.count());
			const auto* item = m_project.items.getById(reward.itemid());
			packet
				<< io::write<uint32>(item ? item->displayid() : 0);*/
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
		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestGiverOfferReward(game::IncomingPacket& packet)
	{
		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestGiverRequestItems(game::IncomingPacket& packet)
	{
		return PacketParseResult::Pass;
	}

	PacketParseResult QuestClient::OnQuestUpdate(game::IncomingPacket& packet)
	{
		switch(packet.GetId())
		{
		case game::realm_client_packet::QuestUpdateAddItem:
			break;
		case game::realm_client_packet::QuestUpdateAddKill:
			break;
		case game::realm_client_packet::QuestUpdateComplete:
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
}
