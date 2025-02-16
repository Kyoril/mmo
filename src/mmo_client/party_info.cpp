
#include "party_info.h"

#include "frame_ui/frame_mgr.h"

namespace mmo
{
	PartyInfo::PartyInfo(RealmConnector& realmConnector, DBNameCache& nameCache)
		: m_realmConnector(realmConnector)
		, m_nameCache(nameCache)
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

		// Read group member info
		std::vector<PartyMember> members;
		members.resize(memberCount);

		// Do we have any group members?
		for (uint8 i = 0; i < memberCount; ++i)
		{
			packet
				>> io::read_container<uint8>(members[i].name)
				>> io::read<uint64>(members[i].guid)
				>> io::read<uint8>(members[i].status)
				>> io::read<uint8>(members[i].group)
				>> io::read<uint8>(members[i].assistant);
		}

		packet >> io::read<uint64>(m_leaderGuid);
		if (members.size() > 1)
		{
			packet
				>> io::read<uint8>(m_lootMethod)
				>> io::read<uint64>(m_lootMaster)
				>> io::read<uint8>(m_lootThreshold);
		}

		// Now we need to determine which members are new, which ones are gone and so on
		for (const auto& newMember : members)
		{
			bool wasInGroup = false;
			for (const auto& oldMember : m_members)
			{
				if (newMember.guid == oldMember.guid)
				{
					wasInGroup = true;
					break;
				}
			}

			if (!wasInGroup)
			{
				// Ensure we know the name
				m_nameCache.Get(newMember.guid);

				ILOG(newMember.name << " has joined the group.");
				m_members.push_back(newMember);
			}
		}

		for (auto it = m_members.begin(); it != m_members.end();)
		{
			bool isInGroup = false;
			for (const auto& newMember : members)
			{
				if (it->guid == newMember.guid)
				{
					isInGroup = true;
					break;
				}
			}

			if (!isInGroup)
			{
				ILOG(it->name << " has left the group.");
				it = m_members.erase(it);
			}
			else
			{
				++it;
			}
		}

		ASSERT(m_members.size() <= 4);

		FrameManager::Get().TriggerLuaEvent("PARTY_MEMBERS_CHANGED");

		return PacketParseResult::Pass;
	}
}
