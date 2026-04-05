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

	/// Represents a single guild and its member roster.
	class Guild final : public NonCopyable, public std::enable_shared_from_this<Guild>
	{
	public:
		/// Creates a guild instance.
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
		/// Gets the guild id.
		uint64 GetId() const { return m_id; }

		/// Gets the guild name.
		const String& GetName() const { return m_name; }

		/// Gets the leader's character guid.
		uint64 GetLeaderGuid() const { return m_leaderGuid; }

		/// Gets the guild's current message of the day.
		[[nodiscard]] const String& GetMotd() const { return m_motd; }

		/// Sets the guild's message of the day (in-memory only — caller must persist to DB).
		void SetMotd(const String& motd) { m_motd = motd; }

		/// Checks if the given character is a member of this guild.
		bool IsMember(uint64 playerGuid) const;

		/// Gets the rank for the given member.
		uint32 GetMemberRank(uint64 playerGuid) const;

		/// Checks if the member has the given permission bit.
		bool HasPermission(uint64 playerGuid, uint32 permission) const;

		/// Gets the list of members who have the given permission bit.
		std::vector<uint64> GetMembersWithPermission(uint32 permission) const;

		/// Adds a member to the guild.
		bool AddMember(uint64 playerGuid, uint32 rank = 4);

		/// Removes a member from the guild.
		bool RemoveMember(uint64 playerGuid);

		/// Promotes a member and broadcasts the change.
		bool PromoteMember(uint64 playerGuid, const String& promoterName, const String& promotedName);

		/// Demotes a member and broadcasts the change.
		bool DemoteMember(uint64 playerGuid, const String& demoterName, const String& demotedName);

		/// Writes the guild roster to the given writer.
		void WriteRoster(io::Writer& writer);

		/// Gets the member list.
		const std::vector<GuildMember>& GetMembers() const { return m_members; }
		/// Gets the mutable member list.
		std::vector<GuildMember>& GetMembersRef() { return m_members; }

		/// Gets the rank list.
		const std::vector<GuildRank>& GetRanks() const { return m_ranks; }
		/// Gets the mutable rank list.
		std::vector<GuildRank>& GetRanksRef() { return m_ranks; }

		/// Gets the lowest rank index.
		uint32 GetLowestRank() const;

		/// Gets rank data by index.
		const GuildRank* GetRank(uint32 rank) const;

		/// Broadcasts a guild event message to online members.
		void BroadcastEvent(GuildEvent event, uint64 exceptGuid = 0, const char* arg1 = nullptr, const char* arg2 = nullptr, const char* arg3 = nullptr);

		/// Broadcasts a packet to members with the given permission.
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
		/// Current message of the day; empty string if not set.
		String m_motd;
	};

	/// Manages all guilds on the realm server.
	class GuildMgr final : public NonCopyable
	{
	public:
		/// Creates a guild manager.
		explicit GuildMgr(AsyncDatabase& asyncDatabase, PlayerManager& playerManager);

	public:
		/// Loads all guilds from the database into memory.
		void LoadGuilds();

		/// Creates a new guild and persists it to the database.
		bool CreateGuild(const String& name, uint64 leaderGuid, const std::vector<uint64>& initialMembers, std::function<void(Guild*)> callback);

		/// Checks if a guild id exists.
		bool HasGuild(uint64 guildId) const { return m_guildsById.find(guildId) != m_guildsById.end(); }

		/// Gets a guild id by its name.
		uint64 GetGuildIdByName(const String& name) const;

		/// Returns whether all guilds have been loaded.
		bool GuildsLoaded() const { return m_guildsLoaded; }

		/// Gets a guild by id.
		Guild* GetGuild(uint64 guildId) const;

		/// Disbands a guild and removes it from the database.
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
