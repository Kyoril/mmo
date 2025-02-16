#pragma once

#include "base/typedefs.h"
#include "game/group.h"

#include <map>
#include <memory>

#include "player.h"
#include "player_manager.h"
#include "base/linear_set.h"
#include "game/game.h"


namespace mmo
{
	class Player;
	struct IDatabase;
	class PlayerManager;

	class PlayerGroup final : public std::enable_shared_from_this<PlayerGroup>
	{
	public:

		static std::map<uint64, std::shared_ptr<PlayerGroup>> ms_groupsById;

	public:

		typedef std::map<uint64, GroupMemberSlot> MembersByGUID;
		typedef LinearSet<uint64> InvitedMembers;
		typedef std::map<uint32, InstanceId> InstancesByMap;


	public:

		/// Creates a new instance of a player group. Note that a group has to be
		/// created using the create method before it will be valid.
		explicit PlayerGroup(uint64 id, PlayerManager& playerManager, AsyncDatabase& database);

		/// Restores the group from the database.
		bool CreateFromDatabase();

		/// Creates the group and setup a leader.
		void Create(uint64 leaderGuid);

		/// Changes the loot method.
		void SetLootMethod(LootMethod method, uint64 lootMaster, uint32 lootThreshold);

		/// Sets a new group leader. The new leader has to be a member of this group.
		void SetLeader(uint64 guid);

		/// Sets a new group assistant.
		void SetAssistant(uint64 guid, uint8 flags);

		/// Adds a guid to the list of pending invites.
		PartyResult AddInvite(uint64 inviteGuid);

		/// Adds a new member to the group. The group member has to be invited first.
		PartyResult AddMember(uint64 memberGuid, const String& memberName);

		/// 
		uint64 GetMemberGuid(const String& name) const;

		/// 
		void RemoveMember(uint64 guid);

		/// 
		bool RemoveInvite(uint64 guid);

		/// Sends a group update message to all members of the group.
		void SendUpdate();

		/// 
		void Disband(bool silent);

		/// 
		InstanceId InstanceBindingForMap(uint32 map);

		/// 
		bool AddInstanceBinding(InstanceId instance, uint32 map);

		/// Converts the group into a raid group.
		void ConvertToRaidGroup();

		/// Returns the current group type (normal, raid).
		GroupType GetType() const { return m_type; }

		/// Checks if the specified game character is a member of this group.
		bool IsMember(uint64 guid) const;

		/// Checks whether the specified guid is the group leader or an assistant.
		bool IsLeaderOrAssistant(uint64 guid) const;

		/// Returns the number of group members.
		size_t GetMemberCount() const { return m_members.size(); }

		/// Determines whether this group has been created.
		bool IsCreated() const { return m_leaderGUID != 0; }

		/// Determines whether this group is full already.
		bool IsFull() const { return (m_type == group_type::Normal ? m_members.size() >= 5 : m_members.size() >= 40); }

		/// Gets the groups loot method.
		LootMethod getLootMethod() const { return m_lootMethod; }

		/// Gets the group leaders GUID.
		uint64 GetLeader() const { return m_leaderGUID; }

		/// Gets the group id.
		uint64 GetId() const { return m_id; }

		/// Broadcasts a network packet to all party mambers.
		/// @param creator Function pointer to the network packet writer method.
		/// @param except Optional array of character guids to exclude from the broadcast. May be nullptr.
		/// @param causer GUID of the character who caused this broadcast (if any). This is used for ignore list check right now.
		///               If the packet should be sent, even if the causer is ignored, 0 should be provided.
		template<class F>
		void BroadcastPacket(F creator, std::vector<uint64>* except = nullptr, uint64 causer = 0)
		{
			for (auto& member : m_members)
			{
				if (except)
				{
					bool exclude = false;
					for (const auto& exceptGuid : *except)
					{
						if (member.first == exceptGuid)
						{
							exclude = true;
							break;
						}
					}

					if (exclude)
						continue;
				}

				if (auto player = m_playerManager.GetPlayerByCharacterGuid(member.first))
				{
					player->SendPacket(creator);
				}
			}
		}

	private:

		bool AddOfflineMember(uint64 guid);

	private:

		uint64 m_id;
		PlayerManager& m_playerManager;
		AsyncDatabase& m_database;
		uint64 m_leaderGUID;
		String m_leaderName;
		GroupType m_type;
		MembersByGUID m_members;	// This is the map of actual party members.
		InvitedMembers m_invited;	// This is a list of pending member invites
		LootMethod m_lootMethod;
		uint8 m_lootTreshold;
		uint64 m_lootMaster;
		InstancesByMap m_instances;
	};
}
