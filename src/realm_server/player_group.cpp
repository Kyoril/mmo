
#include "player_group.h"

#include "log/log_exception.h"

namespace mmo
{
	// Implementation. Will store all available player group instances by their id.
	std::map<uint64, std::shared_ptr<PlayerGroup>> PlayerGroup::GroupsById;

	PlayerGroup::PlayerGroup(uint64 id, PlayerManager& playerManager, AsyncDatabase& database)
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

	void PlayerGroup::Create(uint64 leaderGuid)
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
		GroupsById[m_id] = shared_from_this();
		//m_database.createGroup(m_id, m_leaderGUID);
	}

	void PlayerGroup::SetLootMethod(LootMethod method, uint64 lootMaster, uint32 lootTreshold)
	{
		m_lootMethod = method;
		m_lootTreshold = lootTreshold;
		m_lootMaster = lootMaster;
	}

	bool PlayerGroup::IsMember(uint64 guid) const
	{
		auto it = m_members.find(guid);
		return (it != m_members.end());
	}

	void PlayerGroup::SetLeader(uint64 guid)
	{
		// New character has to be a member of this group
		auto it = m_members.find(guid);
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

	PartyResult PlayerGroup::AddMember(uint64 memberGuid, const String& memberName)
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
		for (auto& it : m_members)
		{
			auto player = m_playerManager.GetPlayerByCharacterGuid(it.first);
			if (!player)
			{
				// TODO
				continue;
			}

			for (auto& it2 : m_members)
			{
				if (it2.first == it.first)
				{
					continue;
				}

				auto player2 = m_playerManager.GetPlayerByCharacterGuid(it2.first);
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

	void PlayerGroup::RemoveMember(uint64 guid)
	{
		auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			if (m_members.size() <= 2)
			{
				Disband(false);
				return;
			}
			else
			{
				auto player = m_playerManager.GetPlayerByCharacterGuid(guid);
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
					auto firstMember = m_members.begin();
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

	bool PlayerGroup::RemoveInvite(uint64 guid)
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
			auto player = m_playerManager.GetPlayerByCharacterGuid(member.first);
			if (!player)
			{
				member.second.status = group_member_status::Offline;
			}
			else
			{
				member.second.status = group_member_status::Online;
			}
		}

		// Send to every group member
		for (auto& member : m_members)
		{
			auto player = m_playerManager.GetPlayerByCharacterGuid(member.first);
			if (!player)
			{
				continue;
			}

			// Send packet
			//player->sendPacket(
			//	std::bind(game::server_write::groupList, std::placeholders::_1,
			//		member.first,
			//		m_type,
			//		false,
			//		member.second.group,
			//		member.second.assistant ? 1 : 0,
			//		0x50000000FFFFFFFELL,
			//		std::cref(m_members),
			//		m_leaderGUID,
			//		m_lootMethod,
			//		m_lootMaster,
			//		m_lootTreshold,
			//		0));
		}
	}

	void PlayerGroup::Disband(bool silent)
	{
		if (!silent)
		{
			//BroadcastPacket(
			//	std::bind(game::server_write::groupDestroyed, std::placeholders::_1));
		}

		auto memberList = m_members;
		for (auto& it : memberList)
		{
			auto player = m_playerManager.GetPlayerByCharacterGuid(it.first);
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
		auto it = GroupsById.find(m_id);
		if (it != GroupsById.end()) GroupsById.erase(it);
	}

	uint64 PlayerGroup::GetMemberGuid(const String& name)
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

	InstanceId PlayerGroup::InstanceBindingForMap(uint32 map)
	{
		auto it = m_instances.find(map);
		if (it != m_instances.end())
		{
			return it->second;
		}

		return {};
	}

	bool PlayerGroup::AddInstanceBinding(InstanceId instance, uint32 map)
	{
		auto it = m_instances.find(map);
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

	void PlayerGroup::SetAssistant(uint64 guid, uint8 flags)
	{
		auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			it->second.assistant = (flags != 0);
			SendUpdate();
		}
	}

	bool PlayerGroup::IsLeaderOrAssistant(uint64 guid) const
	{
		auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			return (it->first == m_leaderGUID || it->second.assistant);
		}

		return false;
	}

	bool PlayerGroup::AddOfflineMember(uint64 guid)
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
