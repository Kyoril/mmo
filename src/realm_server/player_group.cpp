
#include "player_group.h"

#include "database.h"
#include "world.h"
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

	void PlayerGroup::Preload()
	{
		ASSERT(!IsLoaded());

		// Save group for later user
		ms_groupsById[m_id] = shared_from_this();
	}

	bool PlayerGroup::CreateFromDatabase()
	{
		// Already created once
		if (m_leaderGUID != 0 || m_loading)
		{
			return true;
		}

		m_loading = true;

		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis](const std::optional<GroupData>& groupData)
		{
			auto strongThis = weakThis.lock();
			if (!strongThis)
			{
				return;
			}

			if (groupData)
			{
				strongThis->OnLoad(*groupData);
			}
			else
			{
				ELOG("Failed to load group data from database!");
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::LoadGroup, m_id);

		return true;
	}

	void PlayerGroup::Create(const uint64 leaderGuid, const String& leaderName)
	{
		// Already created once
		if (m_leaderGUID != 0 || m_loading)
		{
			return;
		}
		
		// Save group leader values
		m_leaderGUID = leaderGuid;
		m_leaderName = leaderName;

		// Add the leader as a group member
		auto& newMember = m_members[m_leaderGUID];
		newMember.name = m_leaderName;
		newMember.group = 0;
		newMember.assistant = false;

		// Save group
		ms_groupsById[m_id] = shared_from_this();

		auto handler = [](bool success){ };
		m_database.asyncRequest(std::move(handler), &IDatabase::CreateGroup, m_id, m_leaderGUID);
	}

	void PlayerGroup::SetLootMethod(const LootMethod method, const uint64 lootMaster, const uint32 lootThreshold)
	{
		ASSERT(m_leaderGUID != 0);

		m_lootMethod = method;
		m_lootTreshold = lootThreshold;
		m_lootMaster = lootMaster;
	}

	bool PlayerGroup::IsMember(const uint64 guid) const
	{
		ASSERT(m_leaderGUID != 0);

		if (m_leaderGUID == guid) return true;

		const auto it = m_members.find(guid);
		return (it != m_members.end());
	}

	void PlayerGroup::SetLeader(const uint64 guid)
	{
		// New character has to be a member of this group
		const auto it = m_members.find(guid);
		if (it == m_members.end())
		{
			WLOG("Trying to set a non-member as group leader!");
			return;
		}

		m_leaderGUID = it->first;
		m_leaderName = it->second.name;

		BroadcastPacket([&](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::GroupSetLeader);
			packet << io::write<uint64>(m_leaderGUID);
			packet.Finish();
		});

		auto handler = [](bool success) {};
		m_database.asyncRequest(std::move(handler), &IDatabase::SetGroupLeader, m_id, m_leaderGUID);
	}

	PartyResult PlayerGroup::AddMember(const uint64 memberGuid, const String& memberName)
	{
		ASSERT(m_leaderGUID != 0);

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
		auto handler = [](bool success) {};
		m_database.asyncRequest(std::move(handler), &IDatabase::AddGroupMember, m_id, memberGuid);

		return party_result::Ok;
	}

	PartyResult PlayerGroup::AddInvite(uint64 inviteGuid)
	{
		ASSERT(m_leaderGUID != 0);

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
		ASSERT(m_leaderGUID != 0);

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
				if (const auto player = m_playerManager.GetPlayerByCharacterGuid(guid))
				{
					if (const auto node = player->GetWorld())
					{
						node->NotifyPlayerGroupChanged(guid, 0);
					}

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
				auto handler = [](bool success) {};
				m_database.asyncRequest(std::move(handler), &IDatabase::RemoveGroupMember, m_id, guid);
			}
		}
	}

	bool PlayerGroup::RemoveInvite(const uint64 guid)
	{
		ASSERT(m_leaderGUID != 0);

		if (!m_invited.contains(guid))
		{
			WLOG("Trying to remove a non-invited member from the invite list!");
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
		ASSERT(m_leaderGUID != 0);

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
		ASSERT(m_leaderGUID != 0);

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
				auto node = player->GetWorld();
				if (node)
				{
					node->NotifyPlayerGroupChanged(it.first, 0);
				}

				// If the group is reset for every player, the group will be deleted!
				//player->sendPacket(
				//	std::bind(game::server_write::groupListRemoved, std::placeholders::_1));
				player->SetGroup(nullptr);
			}
		}

		// Remove from database
		auto handler = [](bool success) {};
		m_database.asyncRequest(std::move(handler), &IDatabase::DisbandGroup, m_id);

		// Erase group from the global list of all groups
		const auto it = ms_groupsById.find(m_id);
		if (it != ms_groupsById.end())
		{
			ms_groupsById.erase(it);
		}
	}

	uint64 PlayerGroup::GetMemberGuid(const String& name) const
	{
		ASSERT(m_leaderGUID != 0);

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
		ASSERT(m_leaderGUID != 0);

		const auto it = m_instances.find(map);
		if (it != m_instances.end())
		{
			return it->second;
		}

		return {};
	}

	bool PlayerGroup::AddInstanceBinding(const InstanceId instance, const uint32 map)
	{
		ASSERT(m_leaderGUID != 0);

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
		ASSERT(m_leaderGUID != 0);

		if (m_type == group_type::Raid)
		{
			return;
		}

		m_type = group_type::Raid;
		SendUpdate();
	}

	void PlayerGroup::SetAssistant(const uint64 guid, const uint8 flags)
	{
		ASSERT(m_leaderGUID != 0);

		auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			it->second.assistant = (flags != 0);
			SendUpdate();
		}
	}

	bool PlayerGroup::IsLeaderOrAssistant(const uint64 guid) const
	{
		ASSERT(m_leaderGUID != 0);

		const auto it = m_members.find(guid);
		if (it != m_members.end())
		{
			return (it->first == m_leaderGUID || it->second.assistant);
		}

		return false;
	}

	void PlayerGroup::OnLoad(const GroupData& groupData)
	{
		ASSERT(m_loading);

		m_leaderGUID = groupData.leaderGuid;
		m_leaderName = groupData.leaderName;
		m_loading = false;

		// Add the leader as a group member
		auto& leaderMember = m_members[m_leaderGUID];
		leaderMember.name = m_leaderName;
		leaderMember.group = 0;
		leaderMember.assistant = false;
		leaderMember.status = group_member_status::Offline;

		if (m_playerManager.GetPlayerByCharacterGuid(m_leaderGUID))
		{
			leaderMember.status = group_member_status::Online;
		}

		for (const auto& member : groupData.members)
		{
			// Add group member
			auto& newMember = m_members[member.guid];
			newMember.name = member.name;
			newMember.group = 0;
			newMember.assistant = false;
			newMember.status = group_member_status::Offline;
			if (m_playerManager.GetPlayerByCharacterGuid(m_leaderGUID))
			{
				leaderMember.status = group_member_status::Online;
			}
		}

		loaded(*this);
	}
}
