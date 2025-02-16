
#include "party_info.h"

#include "frame_ui/frame_mgr.h"

namespace mmo
{
	PartyInfo::PartyInfo(RealmConnector& realmConnector)
		: m_realmConnector(realmConnector)
	{
	}

	void PartyInfo::Initialize()
	{
		ASSERT(m_packetHandlerHandles.IsEmpty());

		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::GroupDestroyed, *this, &PartyInfo::OnGroupDestroyed);
		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::GroupList, *this, &PartyInfo::OnGroupList);
	}

	void PartyInfo::Shutdown()
	{
		m_packetHandlerHandles.Clear();
	}

	uint64 PartyInfo::GetMemberGuid(int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(m_members.size()))
		{
			return 0;
		}
		return m_members[index].guid;
	}

	uint64 PartyInfo::GetLeaderGuid() const
	{
		return m_leaderGuid;
	}

	uint64 PartyInfo::GetLootMasterGuid() const
	{
		return m_lootMaster;
	}

	LootMethod PartyInfo::GetLootMethod() const
	{
		return m_lootMethod;
	}

	GroupType PartyInfo::GetGroupType() const
	{
		return m_type;
	}

	bool PartyInfo::IsAssistant() const
	{
		return m_assistant;
	}

	PacketParseResult PartyInfo::OnGroupDestroyed(game::IncomingPacket& packet)
	{
		DLOG("Your group has been disbanded.");
		return PacketParseResult::Pass;
	}

	PacketParseResult PartyInfo::OnGroupList(game::IncomingPacket& packet)
	{
		uint8 memberCount = 0;
		if (!(packet 
			>> io::read<uint8>(m_type)
			>> io::read<uint8>(m_assistant)
			>> io::read<uint8>(memberCount)
			))
		{
			ELOG("Failed to read GroupList packet!");
			return PacketParseResult::Disconnect;
		}

		ASSERT(memberCount <= 4);
		m_members.resize(memberCount);

		// Do we have any group members?
		for (uint8 i = 0; i < memberCount; ++i)
		{
			packet
				>> io::read_container<uint8>(m_members[i].name)
				>> io::read<uint64>(m_members[i].guid)
				>> io::read<uint8>(m_members[i].status)
				>> io::read<uint8>(m_members[i].group)
				>> io::read<uint8>(m_members[i].assistant);
		}

		packet >> io::read<uint64>(m_leaderGuid);
		if (m_members.size() > 1)
		{
			packet
				>> io::read<uint8>(m_lootMethod)
				>> io::read<uint64>(m_lootMaster)
				>> io::read<uint8>(m_lootThreshold);
		}

		FrameManager::Get().TriggerLuaEvent("PARTY_MEMBERS_CHANGED");

		return PacketParseResult::Pass;
	}
}
