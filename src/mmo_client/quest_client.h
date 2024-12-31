#pragma once

#include "client_cache.h"
#include "spell_cast.h"
#include "base/non_copyable.h"
#include "game/quest.h"
#include "net/realm_connector.h"

namespace mmo
{
	class GamePlayerC;

	namespace proto_client
	{
		class SpellEntry;
	}

	struct QuestListEntry
	{
		uint32 questId;
		uint32 menuIcon;
		int32 questLevel;
		String questTitle;
	};

	struct QuestDetails
	{
		uint32 questId = 0;
		String questTitle;
		String questDetails;
		String questObjectives;
		uint32 suggestedPlayerCount = 0;
		// TODO: Rewarded item count
		uint32 rewardXp = 0;
		uint32 rewardMoney = 0;
		const proto_client::SpellEntry* rewardSpell = nullptr;

		void Clear()
		{
			questId = 0;
			questTitle.clear();
			questDetails.clear();
			questObjectives.clear();
			suggestedPlayerCount = 0;
			rewardXp = 0;
			rewardMoney = 0;
			rewardSpell = nullptr;
		}
	};

	struct QuestLogEntry
	{
		uint32 questId = 0;
		const QuestInfo* quest = nullptr;
		QuestStatus status = QuestStatus::Incomplete;
		uint8 counters[4] = { 0, 0, 0, 0 };
	};

	/// This class handles quest op codes and interaction for the client with the game server.
	class QuestClient final : public NonCopyable
	{
	public:
		QuestClient(RealmConnector& connector, DBQuestCache& questCache, const proto_client::SpellManager& spells);
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

		void QueryQuestDetails(uint32 questId);

		[[nodiscard]] bool HasQuest() const { return m_questDetails.questId != 0; }

		[[nodiscard]] const QuestDetails* GetQuestDetails() const { return HasQuest() ? &m_questDetails : nullptr; }

		void AcceptQuest(uint32 questId);

		void UpdateQuestLog(const GamePlayerC& player);

		[[nodiscard]] uint32 GetNumQuestLogEntries() const { return m_questLogQuests.size(); }

		const QuestLogEntry* GetQuestLogEntry(uint32 index) const;

		void RefreshQuestGiverStatus();

		void AbandonQuest(uint32 questId);

	private:
		void ProcessQuestText(String& questText);

	private:
		PacketParseResult OnQuestGiverQuestList(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverQuestDetails(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverQuestComplete(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverOfferReward(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverRequestItems(game::IncomingPacket& packet);

		PacketParseResult OnQuestUpdate(game::IncomingPacket& packet);

		PacketParseResult OnQuestLogFull(game::IncomingPacket& packet);

		PacketParseResult OnGossipComplete(game::IncomingPacket& packet);

	private:
		RealmConnector& m_connector;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlers;
		DBQuestCache& m_questCache;
		const proto_client::SpellManager& m_spells;

		std::vector<QuestListEntry> m_questList;
		uint64 m_questGiverGuid = 0;
		String m_greetingText;
		QuestDetails m_questDetails;

		std::array<QuestLogEntry, MaxQuestLogSize> m_questLog;
		std::vector<uint32> m_questLogQuests;
	};
}
