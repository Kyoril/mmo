
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

	private:
		PacketParseResult OnGuildQueryResult(game::IncomingPacket& packet);

#ifdef MMO_WITH_DEV_COMMANDS
		void Command_GuildCreate(const std::string& cmd, const std::string& args) const;
#endif

	private:
		RealmConnector& m_connector;
		DBGuildCache& m_guildCache;
		RealmConnector::PacketHandlerHandleContainer m_handlers;
	};
}
