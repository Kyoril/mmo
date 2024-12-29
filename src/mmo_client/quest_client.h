#pragma once

#include "client_cache.h"
#include "base/non_copyable.h"
#include "net/realm_connector.h"

namespace mmo
{
	struct QuestListEntry
	{
		uint32 questId;
		uint32 menuIcon;
		int32 questLevel;
		String questTitle;
	};

	/// This class handles quest op codes and interaction for the client with the game server.
	class QuestClient final : public NonCopyable
	{
	public:
		QuestClient(RealmConnector& connector, DBQuestCache& questCache);
		~QuestClient() override = default;

	public:
		void Initialize();

		void Shutdown();

		void CloseQuest();

		[[nodiscard]] uint64 GetQuestGiverGuid() const {	return m_questGiverGuid; }

		[[nodiscard]] bool HasQuestGiver() const { return GetQuestGiverGuid() != 0; }

		[[nodiscard]] const String& GetGreetingText() const;

		[[nodiscard]] int32 GetNumAvailableQuests() const { return m_questList.size(); }

		[[nodiscard]] const QuestListEntry* GetAvailableQuest(uint32 index) const { return &m_questList[index]; }

	private:
		PacketParseResult OnQuestGiverQuestList(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverQuestDetails(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverQuestComplete(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverOfferReward(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverRequestItems(game::IncomingPacket& packet);

		PacketParseResult OnQuestUpdate(game::IncomingPacket& packet);

		PacketParseResult OnQuestLogFull(game::IncomingPacket& packet);

	private:
		RealmConnector& m_connector;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlers;
		DBQuestCache& m_questCache;

		std::vector<QuestListEntry> m_questList;
		uint64 m_questGiverGuid = 0;
		String m_greetingText;
	};
}
