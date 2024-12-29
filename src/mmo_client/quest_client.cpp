
#include "quest_client.h"

#include "frame_ui/frame_mgr.h"

namespace mmo
{
	QuestClient::QuestClient(RealmConnector& connector, DBQuestCache& questCache)
		: m_connector(connector)
		, m_questCache(questCache)
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
	}

	void QuestClient::CloseQuest()
	{
	}

	const String& QuestClient::GetGreetingText() const
	{
		return m_greetingText;
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
