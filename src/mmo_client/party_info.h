#pragma once
#include "base/non_copyable.h"
#include "game/group.h"
#include "net/realm_connector.h"

namespace mmo
{
	struct PartyMember
	{
		uint64 guid;
		String name;
		uint8 group;
		bool assistant;
		uint32 status;
	};

	class PartyInfo final : public NonCopyable
	{
	public:
		explicit PartyInfo(RealmConnector& realmConnector);

	public:
		void Initialize();

		void Shutdown();

		uint32 GetMemberCount() const { return m_members.size(); }

		uint64 GetMemberGuid(int32 index) const;

		uint64 GetLeaderGuid() const;

		uint64 GetLootMasterGuid() const;

		LootMethod GetLootMethod() const;

		GroupType GetGroupType() const;

		bool IsAssistant() const;

		uint8 GetLootThreshold() const { return m_lootThreshold; }

	private:
		PacketParseResult OnGroupDestroyed(game::IncomingPacket& packet);
		PacketParseResult OnGroupList(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerHandles;

		GroupType m_type;
		uint64 m_leaderGuid = 0;
		uint64 m_lootMaster = 0;
		LootMethod m_lootMethod = loot_method::GroupLoot;
		bool m_assistant = false;
		std::vector<PartyMember> m_members;
		uint8 m_lootThreshold = 0;
	};
}
