
#include "party_info.h"

#include "frame_ui/frame_mgr.h"
#include "game_client/object_mgr.h"

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
		m_packetHandlerHandles += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PartyMemberStats, *this, &PartyInfo::OnPartyMemberStats);
	}

	void PartyInfo::Shutdown()
	{
		m_packetHandlerHandles.Clear();
	}

	bool PartyInfo::IsGroupMember(uint64 memberGuid) const
	{
		if (m_type == group_type::None)
		{
			return false;
		}

		if (memberGuid == m_leaderGuid)
		{
			return true;
		}

		return std::find_if(m_members.begin(), m_members.end(), [memberGuid](const PartyMember& member) { return memberGuid == member.guid; }) != m_members.end();
	}

	int32 PartyInfo::GetMemberIndexByGuid(uint64 memberGuid) const
	{
		if (m_type == group_type::None)
		{
			return -1;
		}

		int32 index = 0;
		for (const auto& member : m_members)
		{
			if (memberGuid == member.guid)
			{
				return index;
			}

			index++;
		}

		return -1;
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

	int32 PartyInfo::GetLeaderIndex() const
	{
		for (int32 i = 0; i < m_members.size(); ++i)
		{
			if (m_members[i].guid == m_leaderGuid)
			{
				return i + 1;
			}
		}

		return 0;
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

	const PartyMember* PartyInfo::GetMember(int32 index) const
	{
		if (index < 0 || index > m_members.size())
		{
			return nullptr;
		}

		return &m_members[index];
	}

	void PartyInfo::OnPlayerSpawned(GamePlayerC& player)
	{
		// Check if player is a group member
		if (!IsGroupMember(player.GetGuid()))
		{
			return;
		}

		RegisterPlayerMirrorHandlers(player);
	}

	void PartyInfo::OnPlayerDespawned(uint64 guid)
	{
		// Check if player is a group member
		if (!IsGroupMember(guid))
		{
			return;
		}

		if (auto it = m_memberObservers.find(guid); it != m_memberObservers.end())
		{
			it = m_memberObservers.erase(it);
		}
	}

	void PartyInfo::RegisterPlayerMirrorHandlers(GamePlayerC& player)
	{
		scoped_connection_container& memberObservers = m_memberObservers[player.GetGuid()];
		memberObservers.disconnect();

		memberObservers += player.RegisterMirrorHandler(object_fields::MaxHealth, 2, *this, &PartyInfo::OnMemberHealthChanged);
		memberObservers += player.RegisterMirrorHandler(object_fields::Mana, 7, *this, &PartyInfo::OnMemberPowerChanged);
		memberObservers += player.RegisterMirrorHandler(object_fields::Level, 2, *this, &PartyInfo::OnMemberLevelChanged);
	}

	void PartyInfo::OnMemberHealthChanged(uint64 monitoredGuid)
	{
		ForMemberIndex(monitoredGuid, [monitoredGuid](int32 index)
			{
				FrameManager::Get().TriggerLuaEvent("UNIT_HEALTH_UPDATED", "party" + std::to_string(index + 1));

				if (monitoredGuid == ObjectMgr::GetSelectedObjectGuid())
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_HEALTH_UPDATED", "target");
				}
			});
	}

	void PartyInfo::OnMemberPowerChanged(uint64 monitoredGuid)
	{
		ForMemberIndex(monitoredGuid, [monitoredGuid](int32 index)
			{
				FrameManager::Get().TriggerLuaEvent("UNIT_POWER_UPDATED", "party" + std::to_string(index + 1));

				if (monitoredGuid == ObjectMgr::GetSelectedObjectGuid())
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_POWER_UPDATED", "target");
				}
			});
	}

	void PartyInfo::OnMemberLevelChanged(uint64 monitoredGuid)
	{
		ForMemberIndex(monitoredGuid, [monitoredGuid](int32 index)
			{
				FrameManager::Get().TriggerLuaEvent("UNIT_LEVEL_UPDATED", "party" + std::to_string(index + 1));

				if (monitoredGuid == ObjectMgr::GetSelectedObjectGuid())
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_LEVEL_UPDATED", "target");
				}
			});
	}

	PacketParseResult PartyInfo::OnGroupDestroyed(game::IncomingPacket& packet)
	{
		DLOG("Your group has been disbanded.");

		// Reset everything
		m_type = group_type::None;
		m_assistant = false;
		m_lootMaster = 0;
		m_leaderGuid = 0;
		m_members.clear();
		m_lootMethod = loot_method::GroupLoot;
		m_lootThreshold = 2;

		FrameManager::Get().TriggerLuaEvent("PARTY_MEMBERS_CHANGED");

		return PacketParseResult::Pass;
	}

	PacketParseResult PartyInfo::OnGroupList(game::IncomingPacket& packet)
	{
		m_memberObservers.clear();

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
				// Monitor player (again)
				if (auto player = ObjectMgr::Get<GamePlayerC>(it->guid))
				{
					RegisterPlayerMirrorHandlers(*player);
				}

				++it;
			}
		}

		ASSERT(m_members.size() <= 4);

		FrameManager::Get().TriggerLuaEvent("PARTY_MEMBERS_CHANGED");

		return PacketParseResult::Pass;
	}

	PacketParseResult PartyInfo::OnPartyMemberStats(game::IncomingPacket& packet)
	{
		uint64 playerGuid;
		uint32 groupUpdateFlags;
		if (!(packet
			>> io::read_packed_guid(playerGuid)
			>> io::read<uint32>(groupUpdateFlags)))
		{
			return PacketParseResult::Disconnect;
		}

		// Get group member
		const auto it = std::find_if(m_members.begin(), m_members.end(), [playerGuid](const PartyMember& member) { return member.guid == playerGuid; });
		if (it == m_members.end())
		{
			WLOG("Unable to find party member by guid for party member stats update");
			return PacketParseResult::Pass;
		}

		if (groupUpdateFlags & group_update_flags::Status)
		{
			if (!(packet >> io::read<uint16>(it->status)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::CurrentHP)
		{
			if (!(packet >> io::read<uint16>(it->health)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::MaxHP)
		{
			if (!(packet >> io::read<uint16>(it->maxHealth)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::PowerType)
		{
			if (!(packet >> io::read<uint8>(it->powerType)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::CurrentPower)
		{
			if (!(packet >> io::read<uint16>(it->power)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::MaxPower)
		{
			if (!(packet >> io::read<uint16>(it->maxPower)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::Level)
		{
			if (!(packet >> io::read<uint16>(it->level)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::Zone)
		{
			if (!(packet >> io::skip<uint16>()))
			{
				return PacketParseResult::Disconnect;
			}
		}

		if (groupUpdateFlags & group_update_flags::Position)
		{
			Vector3 location;
			if (!(packet >> io::read<float>(location.x) >> io::read<float>(location.y) >> io::read<float>(location.z)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		ForMemberIndex(playerGuid, [playerGuid, groupUpdateFlags](int32 index)
			{
				String unitName = "party" + std::to_string(index + 1);

				if (groupUpdateFlags & (group_update_flags::MaxHP | group_update_flags::CurrentHP))
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_HEALTH_UPDATED", unitName);

					if (playerGuid == ObjectMgr::GetSelectedObjectGuid())
					{
						FrameManager::Get().TriggerLuaEvent("UNIT_HEALTH_UPDATED", "target");
					}
				}

				if (groupUpdateFlags & (group_update_flags::PowerType | group_update_flags::MaxPower | group_update_flags::CurrentPower))
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_POWER_UPDATED", unitName);

					if (playerGuid == ObjectMgr::GetSelectedObjectGuid())
					{
						FrameManager::Get().TriggerLuaEvent("UNIT_POWER_UPDATED", "target");
					}
				}

				if (groupUpdateFlags & group_update_flags::Level)
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_LEVEL_UPDATED", unitName);

					if (playerGuid == ObjectMgr::GetSelectedObjectGuid())
					{
						FrameManager::Get().TriggerLuaEvent("UNIT_LEVEL_UPDATED", "target");
					}
				}
			});

		return PacketParseResult::Pass;
	}
}
