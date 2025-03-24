
#include "guild_client.h"

#include "console/console.h"
#include "frame_ui/frame_mgr.h"

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
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildCommandResult, *this, &GuildClient::OnGuildCommandResult);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildInvite, *this, &GuildClient::OnGuildInvite);

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

	void GuildClient::GuildInviteByName(const String& name)
	{
		m_connector.sendSinglePacket([&name](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildInvite);
			packet << io::write_dynamic_range<uint8>(name);
			packet.Finish();
		});
	}

	void GuildClient::GuildUninviteByName(const String& name)
	{
		m_connector.sendSinglePacket([&name](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildRemove);
			packet << io::write_dynamic_range<uint8>(name);
			packet.Finish();
		});
	}

	void GuildClient::GuildPromoteByName(const String& name)
	{
		m_connector.sendSinglePacket([&name](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildPromote);
			packet << io::write_dynamic_range<uint8>(name);
			packet.Finish();
		});
	}

	void GuildClient::GuildDemoteByName(const String& name)
	{
		m_connector.sendSinglePacket([&name](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildDemote);
			packet << io::write_dynamic_range<uint8>(name);
			packet.Finish();
		});
	}

	void GuildClient::GuildSetLeaderByName(const String& name)
	{
		// Since there's no specific GuildSetLeader opcode, we'll use a custom command
		// that will be handled by the server appropriately
		m_connector.sendSinglePacket([&name](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildPromote);
			packet << io::write_dynamic_range<uint8>(name);
			packet << io::write<uint8>(0); // Special flag to indicate set leader operation
			packet.Finish();
		});
	}

	void GuildClient::GuildSetMOTD(const String& motd)
	{
		m_connector.sendSinglePacket([&motd](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildMotd);
			packet << io::write_dynamic_range<uint8>(motd);
			packet.Finish();
		});
	}

	void GuildClient::GuildLeave()
	{
		m_connector.sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildLeave);
			packet.Finish();
		});
	}

	void GuildClient::GuildDisband()
	{
		m_connector.sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildDisband);
			packet.Finish();
		});
	}

	void GuildClient::DeclineGuild()
	{
		if (m_inviteGuildName.empty())
		{
			ELOG("No guild invite to decline");
			return;
		}

		m_inviteGuildName.clear();
		m_invitePlayerName.clear();

		m_connector.sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildDecline);
			packet.Finish();
			});
	}

	void GuildClient::AcceptGuild()
	{
		if (m_inviteGuildName.empty())
		{
			ELOG("No guild invite to decline");
			return;
		}

		m_inviteGuildName.clear();
		m_invitePlayerName.clear();

		m_connector.sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GuildAccept);
			packet.Finish();
			});
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

	PacketParseResult GuildClient::OnGuildCommandResult(game::IncomingPacket& packet)
	{
		uint8 command, result;
		if (!(packet
			>> io::read<uint8>(command)
			>> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		switch (command)
		{
		case game::guild_command::Invite:
			break;

		default:
			ELOG("Unhandled guild command result: " << log_hex_digit(command));
			break;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildClient::OnGuildInvite(game::IncomingPacket& packet)
	{
		if (!(packet
			>> io::read_container<uint8>(m_invitePlayerName)
			>> io::read_container<uint8>(m_inviteGuildName)))
		{
			return PacketParseResult::Disconnect;
		}

		// TODO: Raise UI event
		FrameManager::Get().TriggerLuaEvent("GUILD_INVITE_REQUEST", m_invitePlayerName, m_inviteGuildName);

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
