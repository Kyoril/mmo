#include "guild_client.h"

#include "console/console.h"
#include "frame_ui/frame_mgr.h"
#include "game/guild_info.h"
#include "game_client/object_mgr.h"

#include "luabind_lambda.h"

namespace mmo
{
	namespace
	{
		template<typename Ret, typename Class, typename... Args>
		auto bind_this(Ret(Class::* method)(Args...), Class* instance)
		{
			return [=](Args... args) -> Ret
			{
				return (instance->*method)(std::forward<Args>(args)...);
			};
		}
	}

	GuildClient::GuildClient(RealmConnector& realmConnector, DBGuildCache& guildCache, const proto_client::RaceManager& races, const proto_client::ClassManager& classes)
		: m_connector(realmConnector)
		, m_guildCache(guildCache)
		, m_races(races)
		, m_classes(classes)
	{
	}

	void GuildClient::Initialize()
	{
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildQueryResponse, *this, &GuildClient::OnGuildQueryResult);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildCommandResult, *this, &GuildClient::OnGuildCommandResult);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildInvite, *this, &GuildClient::OnGuildInvite);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildDecline, *this, &GuildClient::OnGuildDecline);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildUninvite, *this, &GuildClient::OnGuildUninvite);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildEvent, *this, &GuildClient::OnGuildEvent);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::GuildRoster, *this, &GuildClient::OnGuildRoster);
		

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

	void GuildClient::RegisterScriptFunctions(lua_State* lua)
	{
#ifdef __INTELLISENSE__
#pragma warning(disable: 28)  // Suppress IntelliSense warning about operator[]
#endif
		// Register common functions
		LUABIND_MODULE(lua,
			luabind::scope(
				luabind::class_<GuildMemberInfo>("GuildMemberInfo")
				.def_readonly("name", &GuildMemberInfo::name)
				.def_readonly("rank", &GuildMemberInfo::rank)
				.def_readonly("rankIndex", &GuildMemberInfo::rankIndex)
				.def_readonly("className", &GuildMemberInfo::className)
				.def_readonly("raceName", &GuildMemberInfo::raceName)
				.def_readonly("level", &GuildMemberInfo::level)
				.def_readonly("online", &GuildMemberInfo::online)
			),

			luabind::def_lambda("GuildInviteByName", [this](const String& name) { return GuildInviteByName(name); }),
			luabind::def_lambda("GuildUninviteByName", [this](const String& name) { return GuildUninviteByName(name); }),
			luabind::def_lambda("GuildPromoteByName", [this](const String& name) { return GuildPromoteByName(name); }),
			luabind::def_lambda("GuildDemoteByName", [this](const String& name) { return GuildDemoteByName(name); }),
			luabind::def_lambda("GuildSetLeaderByName", [this](const String& name) { return GuildSetLeaderByName(name); }),
			luabind::def_lambda("GuildSetMOTD", [this](const String& motd) { return GuildSetMOTD(motd); }),
			luabind::def_lambda("GuildLeave", [this]() { return GuildLeave(); }),
			luabind::def_lambda("GuildDisband", [this]() { return GuildDisband(); }),
			luabind::def_lambda("AcceptGuild", [this]() { return AcceptGuild(); }),
			luabind::def_lambda("DeclineGuild", [this]() { return DeclineGuild(); }),

			luabind::def_lambda("IsInGuild", [this]() { return IsInGuild(); }),
			luabind::def_lambda("GetNumGuildMembers", [this]() { return GetNumGuildMembers(); }),
			luabind::def_lambda("GetNumRanks", [this]() { return GetNumRanks(); }),
			luabind::def_lambda("GetGuildMemberInfo", [this](int32 index) { return GetGuildMemberInfo(index); }),

			luabind::def_lambda("IsGuildLeader", [this]() { return IsGuildLeader(); }),
			luabind::def_lambda("CanGuildPromote", [this]() { return CanGuildPromote(); }),
			luabind::def_lambda("CanGuildDemote", [this]() { return CanGuildDemote(); }),
			luabind::def_lambda("CanGuildInvite", [this]() { return CanGuildInvite(); }),
			luabind::def_lambda("CanGuildRemove", [this]() { return CanGuildRemove(); }),
			luabind::def_lambda("GuildRoster", [this]() { GuildRoster(); }),
			luabind::def_lambda("GetGuildName", [this]() { return GetGuildName().c_str(); }),
			luabind::def_lambda("GetGuildMOTD", [this]() { return GetGuildMOTD().c_str(); })
		);
#ifdef __INTELLISENSE__
#pragma warning(default: 28)  // Restore IntelliSense warning
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

	bool GuildClient::IsInGuild() const
	{
		return m_guildId != 0;
	}

	int32 GuildClient::GetNumGuildMembers() const
	{
		if (!IsInGuild())
		{
			return 0;
		}

		return m_guildMembers.size();
	}

	int32 GuildClient::GetNumRanks() const
	{
		if (!IsInGuild())
		{
			return 0;
		}

		// TODO
		return 0;
	}

	bool GuildClient::IsGuildLeader() const
	{
		if (!IsInGuild())
		{
			return false;
		}

		return m_guildRank == 0;
	}

	bool GuildClient::CanGuildInvite() const
	{
		if (!IsInGuild())
		{
			return false;
		}

		if (IsGuildLeader())
		{
			return true;
		}

		// TODO
		return false;
	}

	bool GuildClient::CanGuildPromote() const
	{
		if (!IsInGuild())
		{
			return false;
		}

		if (IsGuildLeader())
		{
			return true;
		}

		// TODO
		return false;
	}

	bool GuildClient::CanGuildDemote() const
	{
		if (!IsInGuild())
		{
			return false;
		}

		if (IsGuildLeader())
		{
			return true;
		}

		// TODO
		return false;
	}

	bool GuildClient::CanGuildRemove() const
	{
		if (!IsInGuild())
		{
			return false;
		}

		if (IsGuildLeader())
		{
			return true;
		}

		// TODO
		return false;
	}

	const GuildMemberInfo* GuildClient::GetGuildMemberInfo(int32 index) const
	{
		if (!IsInGuild() || index < 0 || index >= GetNumGuildMembers())
		{
			return nullptr;
		}

		return &m_guildMembers[index];
	}

	void GuildClient::GuildRoster()
	{
		m_connector.GuildRoster();
	}

	void GuildClient::NotifyGuildChanged(const uint64 guildId)
	{
		m_guildId = guildId;

		if (m_guildId != 0)
		{
			m_guildCache.Get(m_guildId, [this](const uint64 guildId, const GuildInfo& guild)
				{
					if (guildId != m_guildId)
					{
						return;
					}

					m_guildName = guild.name;
					m_guildMotd = "TODO";
				});
		}
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
		String playerName;
		if (!(packet
			>> io::read<uint8>(command)
			>> io::read<uint8>(result)
			>> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (result != game::guild_command_result::Ok)
		{
			FrameManager::Get().TriggerLuaEvent("GUILD_COMMAND_RESULT", static_cast<int32>(result), playerName);
		}
		else
		{
			if (command == game::guild_command::Invite)
			{
				FrameManager::Get().TriggerLuaEvent("GUILD_INVITE_SENT", playerName);
			}
			else if (command == game::guild_command::Leave)
			{
				FrameManager::Get().TriggerLuaEvent("GUILD_LEFT");
			}
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

		FrameManager::Get().TriggerLuaEvent("GUILD_INVITE_REQUEST", m_invitePlayerName, m_inviteGuildName);

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildClient::OnGuildDecline(game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("GUILD_INVITE_DECLINED", playerName);

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildClient::OnGuildUninvite(game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("GUILD_REMOVED", playerName);

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildClient::OnGuildEvent(game::IncomingPacket& packet)
	{
		GuildEvent event;
		uint8 stringCount;
		if (!(packet
			>> io::read<uint8>(event)
			>> io::read<uint8>(stringCount)))
		{
			return PacketParseResult::Disconnect;
		}

		std::vector<String> args;
		args.resize(stringCount);
		for (uint8 i = 0; i < stringCount; ++i)
		{
			if (!(packet >> io::read_container<uint8>(args[i])))
			{
				return PacketParseResult::Disconnect;
			}
		}

		ASSERT(static_cast<size_t>(event) < guild_event::Count_);
		static const char* s_eventStrings[] = {
			"PROMOTION",
			"DEMOTION",
			"MOTD",
			"JOINED",
			"LEFT",
			"REMOVED",
			"LEADER_CHANGED",
			"DISBANDED",
			"LOGGED_IN",
			"LOGGED_OUT"
		};

		static_assert(std::size(s_eventStrings) == static_cast<size_t>(GuildEvent::Count_), "Event string count mismatch");

		const char* arg1 = args.size() >= 1 ? args[0].c_str() : nullptr;
		const char* arg2 = args.size() >= 2 ? args[1].c_str() : nullptr;
		const char* arg3 = args.size() >= 3 ? args[2].c_str() : nullptr;
		FrameManager::Get().TriggerLuaEvent("GUILD_EVENT", s_eventStrings[static_cast<size_t>(event)], arg1, arg2, arg3);

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildClient::OnGuildRoster(game::IncomingPacket& packet)
	{
		uint32 memberCount;
		uint32 rankCount;
		if (!(packet >> io::read<uint32>(memberCount) >> io::read<uint32>(rankCount)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint32 i = 0; i < rankCount; ++i)
		{
			uint32 permissions;
			if (!(packet >> io::read<uint32>(permissions)))
			{
				return PacketParseResult::Disconnect;
			}
		}

		m_guildMembers.resize(memberCount);
		for (auto& member : m_guildMembers)
		{
			uint32 classId, raceId;
			if (!(packet
				>> io::read<uint64>(member.guid)
				>> io::read<uint8>(member.online)
				>> io::read_container<uint8>(member.name)
				>> io::read<uint32>(member.rankIndex)
				>> io::read<uint32>(member.level)
				>> io::read<uint32>(classId)
				>> io::read<uint32>(raceId)))
			{
				return PacketParseResult::Disconnect;
			}

			if (member.guid == ObjectMgr::GetActivePlayerGuid())
			{
				m_guildRank = member.rankIndex;
			}

			member.rank = "UNKNOWN";

			const proto_client::RaceEntry* race = m_races.getById(raceId);
			member.raceName = race ? race->name() : "UNKNOWN";

			const proto_client::ClassEntry* classEntry = m_classes.getById(classId);
			member.className = classEntry ? classEntry->name() : "UNKNOWN";
		}

		// Notify the UI that the roster updated
		FrameManager::Get().TriggerLuaEvent("GUILD_ROSTER_UPDATE");

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
