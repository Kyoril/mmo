#pragma once

#include "database.h"
#include "player.h"
#include "player_manager.h"
#include "base/non_copyable.h"
#include "game/guild_info.h"

namespace mmo
{
	class GuildMgr;
	class GamePlayerS;

	class Guild final : public NonCopyable, public std::enable_shared_from_this<Guild>
	{
	public:
		Guild(GuildMgr& manager, PlayerManager& playerManager, AsyncDatabase& database, uint64 id, String name, uint64 leaderGuid)
			: m_manager(manager)
			, m_playerManager(playerManager)
			, m_database(database)
			, m_id(id)
			, m_name(std::move(name))
			, m_leaderGuid(leaderGuid)
		{
			ASSERT(leaderGuid != 0);
		}

	public:
		uint64 GetId() const { return m_id; }

		const String& GetName() const { return m_name; }

		uint64 GetLeaderGuid() const { return m_leaderGuid; }

		bool IsMember(uint64 playerGuid) const;

		uint32 GetMemberRank(uint64 playerGuid) const;

		bool HasPermission(uint64 playerGuid, uint32 permission) const;

		std::vector<uint64> GetMembersWithPermission(uint32 permission) const;

		bool AddMember(uint64 playerGuid, uint32 rank = 4);

		bool RemoveMember(uint64 playerGuid);

		bool PromoteMember(uint64 playerGuid, const String& promoterName, const String& promotedName);

		bool DemoteMember(uint64 playerGuid, const String& demoterName, const String& demotedName);

		const std::vector<GuildMember>& GetMembers() const { return m_members; }
		std::vector<GuildMember>& GetMembersRef() { return m_members; }

		const std::vector<GuildRank>& GetRanks() const { return m_ranks; }
		std::vector<GuildRank>& GetRanksRef() { return m_ranks; }

		uint32 GetLowestRank() const;

		const GuildRank* GetRank(uint32 rank) const;

		void BroadcastEvent(GuildEvent event, uint64 exceptGuid = 0, const char* arg1 = nullptr, const char* arg2 = nullptr, const char* arg3 = nullptr);

		template<class F>
		void BroadcastPacketWithPermission(const F& creator, const uint32 permissions, uint64 exceptGuid = 0)
		{
			for (auto& member : m_members)
			{
				if (exceptGuid != 0 && member.guid == exceptGuid)
				{
					continue;
				}

				if (permissions != 0 && !HasPermission(member.guid, permissions))
				{
					continue;
				}

				if (auto player = m_playerManager.GetPlayerByCharacterGuid(member.guid))
				{
					DLOG("Send packet to player " << member.guid);
					player->SendPacket(creator);
				}
			}
		}

	private:
		GuildMgr& m_manager;
		PlayerManager& m_playerManager;
		AsyncDatabase& m_database;
		uint64 m_id;
		String m_name;
		uint64 m_leaderGuid;
		std::vector<GuildMember> m_members;
		std::vector<GuildRank> m_ranks;
	};

	class GuildMgr final : public NonCopyable
	{
	public:
		explicit GuildMgr(AsyncDatabase& asyncDatabase, PlayerManager& playerManager);

	public:
		void LoadGuilds();

		bool CreateGuild(const String& name, uint64 leaderGuid, const std::vector<uint64>& initialMembers, std::function<void(Guild*)> callback);

		bool HasGuild(uint64 guildId) const { return m_guildsById.find(guildId) != m_guildsById.end(); }

		uint64 GetGuildIdByName(const String& name) const;

		bool GuildsLoaded() const { return m_guildsLoaded; }

		Guild* GetGuild(uint64 guildId) const;

		bool DisbandGuild(uint64 guildId);

	private:
		bool AddGuild(const GuildData& info);

	private:
		AsyncDatabase& m_asyncDatabase;

		PlayerManager& m_playerManager;

		std::map<uint64, std::shared_ptr<Guild>> m_guildsById;

		std::map<String, uint64> m_guildIdsByName;

		IdGenerator<uint64> m_idGenerator { 1 };

		volatile bool m_guildsLoaded = false;
	};
}
