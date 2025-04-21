
#pragma once

#include "client_cache.h"
#include "base/non_copyable.h"
#include "net/realm_connector.h"

namespace mmo
{
	class GuildClient final : public NonCopyable
	{
	public:

		explicit GuildClient(RealmConnector& realmConnector, DBGuildCache& guildCache);

	public:
		void Initialize();

		void Shutdown();

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

	private:
		PacketParseResult OnGuildQueryResult(game::IncomingPacket& packet);

		PacketParseResult OnGuildCommandResult(game::IncomingPacket& packet);

		PacketParseResult OnGuildInvite(game::IncomingPacket& packet);

		PacketParseResult OnGuildDecline(game::IncomingPacket& packet);

		PacketParseResult OnGuildUninvite(game::IncomingPacket& packet);

		PacketParseResult OnGuildEvent(game::IncomingPacket& packet);

#ifdef MMO_WITH_DEV_COMMANDS
		void Command_GuildCreate(const std::string& cmd, const std::string& args) const;
#endif

	private:
		RealmConnector& m_connector;
		DBGuildCache& m_guildCache;
		RealmConnector::PacketHandlerHandleContainer m_handlers;

		String m_invitePlayerName;
		String m_inviteGuildName;

		uint64 m_guildId = 0;
	};
}
