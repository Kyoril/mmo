
#include "player_group.h"

#include "database.h"
#include "log/log_exception.h"

namespace mmo
{
	// Implementation. Will store all available player group instances by their id.
	std::map<uint64, std::shared_ptr<PlayerGroup>> PlayerGroup::ms_groupsById;

	PlayerGroup::PlayerGroup(const uint64 id, PlayerManager& playerManager, AsyncDatabase& database)
		: m_id(id)
		, m_playerManager(playerManager)
		, m_database(database)
		, m_leaderGUID(0)
		, m_type(group_type::Normal)
		, m_lootMethod(loot_method::GroupLoot)
		, m_lootTreshold(2)
		, m_lootMaster(0)
	{
	}

	bool PlayerGroup::CreateFromDatabase()
	{
		// Already created once
		if (m_leaderGUID != 0)
		{
			return true;
		}
		
		//auto groupData = m_database.loadGroup(m_id);
		//if (!groupData)
		{
			// The group will automatically be deleted and not stored when not saved in GroupsById
			ELOG("Could not load group from database");
			return false;
		}

#if 0
		// Save leader information
		m_leaderGUID = groupData->leaderGuid;

		// Add the leader first
		if (!AddOfflineMember(groupData->leaderGuid))
		{
			// Leader no longer exists?
			ELOG("Could not add group leader");
			return false;
		}

		// Same for members
		for (auto& memberGuid : groupData->memberGuids)
		{
			AddOfflineMember(memberGuid);
		}

		// Save group for later user
		GroupsById[m_id] = shared_from_this();
		return true;
#endif
	}

	void PlayerGroup::Create(const uint64 leaderGuid)
	{
		// Already created once
		if (m_leaderGUID != 0)
		{
			return;
		}
		
		// Save group leader values
		m_leaderGUID = leaderGuid;
		m_leaderName = "UNKNOWN";

		// Add the leader as a group member
		auto& newMember = m_members[m_leaderGUID];
		newMember.name = m_leaderName;
		newMember.group = 0;
		newMember.assistant = false;

		// Other checks have already been done in addInvite method, so we are good to go here
		//leader.modifyGroupUpdateFlags(group_update_flags::Full, true);

		// Save group
		ms_groupsById[m_id] = shared_from_this();
		//m_database.createGroup(m_id, m_leaderGUID);
	}

	void PlayerGroup::SetLootMethod(const LootMethod method, const uint64 lootMaster, const uint32 lootThreshold)
	{
		m_lootMethod = method;
		m_lootTreshold = lootThreshold;
		m_lootMaster = lootMaster;
	}

	bool PlayerGroup::IsMember(const uint64 guid) const
	{
		auto it = m_members.find(guid);
		return (it != m_members.end());
	}

	void PlayerGroup::SetLeader(const uint64 guid)
	{
		// New character has to be a member of this group
		const auto it = m_members.find(guid);
		if (it == m_members.end())
		{
			return;
		}

		m_leaderGUID = it->first;
		m_leaderName = it->second.name;

		//BroadcastPacket(
		//	std::bind(game::server_write::groupSetLeader, std::placeholders::_1, std::cref(m_leaderName)));

		//m_database.setGroupLeader(m_id, m_leaderGUID);
	}

	PartyResult PlayerGroup::AddMember(const uint64 memberGuid, const String& memberName)
	{
		if (!m_invited.contains(memberGuid))
		{
			return party_result::NotInYourParty;
		}

		// Remove from invite list
		m_invited.remove(memberGuid);

		// Already full?
		if (IsFull())
		{
			return party_result::PartyFull;
		}

		// Create new group member
		auto& newMember = m_members[memberGuid];
		newMember.status = 1;
		newMember.name = memberName;
		newMember.group = 0;
		newMember.assistant = false;
		//member.modifyGroupUpdateFlags(group_update_flags::Full, true);

		// Update group list
		SendUpdate();

		// Make sure that all group members know about us
		for (const auto& it : m_members)
		{
			const auto player = m_playerManager.GetPlayerByCharacterGuid(it.first);
			if (!player)
			{
				// TODO
				continue;
			}

			for (const auto& it2 : m_members)
			{
				if (it2.first == it.first)
				{
					continue;
				}

				const auto player2 = m_playerManager.GetPlayerByCharacterGuid(it2.first);
				if (!player2)
				{
					// TODO
					continue;
				}

				// Spawn that player for us
				//player->sendPacket(
				//	std::bind(game::server_write::partyMemberStats, std::placeholders::_1, std::cref(*player2->getGameCharacter())));
			}
		}

		// Update database
		//m_database.addGroupMember(m_id, memberGuid);
		return party_result::Ok;
	}

	PartyResult PlayerGroup::AddInvite(uint64 inviteGuid)
	{
		// Can't invite any more members since this group is full already.
		if (IsFull())
		{
			return party_result::PartyFull;
		}

		m_invited.add(inviteGuid);
		return party_result::Ok;
	}

	void PlayerGroup::RemoveMember(const uint64 guid)
	{
		const auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			if (m_members.size() <= 2)
			{
				Disband(false);
				return;
			}
			else
			{
				const auto player = m_playerManager.GetPlayerByCharacterGuid(guid);
				if (player)
				{
					//auto* node = player->GetWorldNode();
					//if (node)
					//{
						//player->getGameCharacter()->setGroupId(0);
						//node->characterGroupChanged(guid, 0);
					//}

					// Send packet
					//player->sendPacket(
					//	std::bind(game::server_write::groupListRemoved, std::placeholders::_1));
					player->SetGroup(nullptr);
				}

				m_members.erase(it);

				if (m_leaderGUID == guid && !m_members.empty())
				{
					const auto firstMember = m_members.begin();
					if (firstMember != m_members.end())
					{
						SetLeader(firstMember->first);
					}
				}

				SendUpdate();

				// Remove from database
				//m_database.removeGroupMember(m_id, guid);
			}
		}
	}

	bool PlayerGroup::RemoveInvite(const uint64 guid)
	{
		if (!m_invited.contains(guid))
		{
			return false;
		}

		m_invited.remove(guid);

		// Check if group is empty
		if (m_members.size() < 2)
		{
			Disband(true);
		}

		return true;
	}

	void PlayerGroup::SendUpdate()
	{
		// Update member status
		for (auto& member : m_members)
		{
			const auto player = m_playerManager.GetPlayerByCharacterGuid(member.first);
			if (!player)
			{
				member.second.status = group_member_status::Offline;
			}
			else
			{
				member.second.status = group_member_status::Online;
			}
		}

		ASSERT(!m_members.empty());

		// Send to every group member
		for (auto& member : m_members)
		{
			auto player = m_playerManager.GetPlayerByCharacterGuid(member.first);
			if (!player)
			{
				continue;
			}

			// Send packet
			player->SendPacket([this, &member, &player](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::GroupList);
					packet
						<< io::write<uint8>(m_type)
						<< io::write<uint8>(member.second.assistant ? 1 : 0)
						<< io::write<uint8>(m_members.size() - 1);
					for (const auto& memberToSend : m_members)
					{
						// Skip receiving member itself
						if (memberToSend.first == member.first)
						{
							continue;
						}

						packet
							<< io::write_dynamic_range<uint8>(memberToSend.second.name)
							<< io::write<uint64>(memberToSend.first)
							<< io::write<uint8>(memberToSend.second.status)
							<< io::write<uint8>(memberToSend.second.group)
							<< io::write<uint8>(memberToSend.second.assistant ? 1 : 0);
					}
					packet << io::write<uint64>(m_leaderGUID);
					if (m_members.size() > 1)
					{
						packet
							<< io::write<uint8>(m_lootMethod)
							<< io::write<uint64>(m_lootMaster)
							<< io::write<uint8>(m_lootTreshold);
					}
					packet.Finish();
				});
		}
	}

	void PlayerGroup::Disband(const bool silent)
	{
		if (!silent)
		{
			BroadcastPacket([](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::GroupDestroyed);
					packet.Finish();
				});
		}

		const auto memberList = m_members;
		for (auto& it : memberList)
		{
			const auto player = m_playerManager.GetPlayerByCharacterGuid(it.first);
			if (player)
			{
				//auto* node = player->getWorldNode();
				//if (node)
				//{
				//	node->characterGroupChanged(it.first, 0);
				//}

				// If the group is reset for every player, the group will be deleted!
				//player->sendPacket(
				//	std::bind(game::server_write::groupListRemoved, std::placeholders::_1));
				player->SetGroup(nullptr);
			}
		}

		// Remove from database
		//m_database.disbandGroup(m_id);

		// Erase group from the global list of all groups
		const auto it = ms_groupsById.find(m_id);
		if (it != ms_groupsById.end())
		{
			ms_groupsById.erase(it);
		}
	}

	uint64 PlayerGroup::GetMemberGuid(const String& name) const
	{
		for (auto& member : m_members)
		{
			if (member.second.name == name)
			{
				return member.first;
			}
		}

		return 0;
	}

	InstanceId PlayerGroup::InstanceBindingForMap(const uint32 map)
	{
		const auto it = m_instances.find(map);
		if (it != m_instances.end())
		{
			return it->second;
		}

		return {};
	}

	bool PlayerGroup::AddInstanceBinding(const InstanceId instance, const uint32 map)
	{
		const auto it = m_instances.find(map);
		if (it != m_instances.end())
		{
			return false;
		}

		m_instances[map] = instance;
		return true;
	}

	void PlayerGroup::ConvertToRaidGroup()
	{
		if (m_type == group_type::Raid)
		{
			return;
		}

		m_type = group_type::Raid;
		SendUpdate();
	}

	void PlayerGroup::SetAssistant(const uint64 guid, const uint8 flags)
	{
		auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			it->second.assistant = (flags != 0);
			SendUpdate();
		}
	}

	bool PlayerGroup::IsLeaderOrAssistant(const uint64 guid) const
	{
		const auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			return (it->first == m_leaderGUID || it->second.assistant);
		}

		return false;
	}

	bool PlayerGroup::AddOfflineMember(const uint64 guid)
	{
		try
		{
			//game::CharEntry entry = m_database.getCharacterById(guid);
			if (guid == m_leaderGUID)
			{
				//m_leaderName = entry.name;
			}

			// Add group member
			auto& newMember = m_members[guid];
			newMember.name = "UNKNOWN"; // entry.name;
			newMember.group = 0;
			newMember.assistant = false;
			newMember.status = group_member_status::Offline;
			return true;
		}
		catch (const std::exception& ex)
		{
			defaultLogException(ex);
		}

		return false;
	}
	
}
