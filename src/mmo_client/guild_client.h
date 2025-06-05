
#pragma once

#include "client_cache.h"
#include "base/non_copyable.h"
#include "client_data/project.h"
#include "net/realm_connector.h"

struct lua_State;

namespace mmo
{
	struct GuildMemberInfo
	{
		uint64 guid = 0;
		String name {};
		String rank{};
		uint32 rankIndex = 0;
		uint32 level = 0;
		String className{};
		String raceName{};
		String zoneName{};
		bool online = false;
	};

	class GuildClient final : public NonCopyable
	{
	public:

		explicit GuildClient(RealmConnector& realmConnector, DBGuildCache& guildCache, const proto_client::RaceManager& races, const proto_client::ClassManager& classes);

	public:
		void Initialize();

		void Shutdown();

		void RegisterScriptFunctions(lua_State* lua);

		void GuildInviteByName(const String& name);

		void GuildUninviteByName(const String& name);

		void GuildPromoteByName(const String& name);

		void GuildDemoteByName(const String& name);

		void GuildSetLeaderByName(const String& name);

		void GuildSetMOTD(const String& motd);

		void GuildLeave();

		void GuildDisband();

		void AcceptGuild();

		void DeclineGuild();

		bool IsInGuild() const;

		int32 GetNumGuildMembers() const;

		int32 GetNumRanks() const;

		bool IsGuildLeader() const;

		bool CanGuildInvite() const;

		bool CanGuildPromote() const;

		bool CanGuildDemote() const;

		bool CanGuildRemove() const;

		const GuildMemberInfo* GetGuildMemberInfo(int32 index) const;

		void GuildRoster();

		void NotifyGuildChanged(uint64 guildId);

		const String& GetGuildName() const { return m_guildName; }

		const String& GetGuildMOTD() const { return m_guildMotd; }

	private:
		PacketParseResult OnGuildQueryResult(game::IncomingPacket& packet);

		PacketParseResult OnGuildCommandResult(game::IncomingPacket& packet);

		PacketParseResult OnGuildInvite(game::IncomingPacket& packet);

		PacketParseResult OnGuildDecline(game::IncomingPacket& packet);

		PacketParseResult OnGuildUninvite(game::IncomingPacket& packet);

		PacketParseResult OnGuildEvent(game::IncomingPacket& packet);

		PacketParseResult OnGuildRoster(game::IncomingPacket& packet);

#ifdef MMO_WITH_DEV_COMMANDS
		void Command_GuildCreate(const std::string& cmd, const std::string& args) const;
#endif

	private:
		RealmConnector& m_connector;
		DBGuildCache& m_guildCache;
		RealmConnector::PacketHandlerHandleContainer m_handlers;

		String m_invitePlayerName;
		String m_inviteGuildName;
		String m_guildName;
		String m_guildMotd;

		uint64 m_guildId = 0;
		int32 m_guildRank = -1;

		std::vector<GuildMemberInfo> m_guildMembers;

		const proto_client::RaceManager& m_races;
		const proto_client::ClassManager& m_classes;
	};
}
