
#include "guild_client.h"

#include "console/console.h"

namespace mmo
{
	GuildClient::GuildClient(RealmConnector& realmConnector, DBGuildCache& guildCache)
		: m_connector(realmConnector)
		, m_guildCache(guildCache)
	{
	}

	void GuildClient::Initialize()
	{
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildQueryResponse, *this, &GuildClient::OnGuildQueryResult);

#ifdef MMO_WITH_DEV_COMMANDS
		Console::RegisterCommand("guildcreate", [this](const std::string& cmd, const std::string& args) { Command_GuildCreate(cmd, args); }, ConsoleCommandCategory::Gm, "Creates a new guild with yourself as the leader.");
#endif
	}

	void GuildClient::Shutdown()
	{
		m_handlers.Clear();

#ifdef MMO_WITH_DEV_COMMANDS
		Console::UnregisterCommand("guildcreate");
#endif
	}

	PacketParseResult GuildClient::OnGuildQueryResult(game::IncomingPacket& packet)
	{
		uint64 guid;
		bool succeeded;
		GuildInfo info;
		if (!(packet
			>> io::read_packed_guid(guid)
			>> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Unable to retrieve guild data for guild " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		if (!(packet >> info))
		{
			return PacketParseResult::Disconnect;
		}

		m_guildCache.NotifyObjectResponse(guid, info);

		return PacketParseResult::Pass;
	}

#ifdef MMO_WITH_DEV_COMMANDS
	void GuildClient::Command_GuildCreate(const std::string& cmd, const std::string& args) const
	{
		if (args.empty())
		{
			ELOG("Usage: guildcreate <name>");
			return;
		}

		m_connector.CreateGuild(args);
	}
#endif
}
