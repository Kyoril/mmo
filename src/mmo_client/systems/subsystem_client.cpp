// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "subsystem_client.h"

#include "frame_ui/frame_mgr.h"
#include "log/default_log_levels.h"
#include "luabind_lambda.h"

namespace mmo
{
	SubsystemClient::SubsystemClient(RealmConnector& connector)
		: m_connector(connector)
	{
		m_status.fill(game::subsystem_status::Unavailable);
	}

	void SubsystemClient::Initialize()
	{
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::SubsystemStatus, *this, &SubsystemClient::OnSubsystemStatus);
	}

	void SubsystemClient::Shutdown()
	{
		m_handlers.Clear();
		m_status.fill(game::subsystem_status::Unavailable);
	}

	void SubsystemClient::RegisterScriptFunctions(lua_State* lua)
	{
#ifdef __INTELLISENSE__
#pragma warning(disable : 28)
#endif
		LUABIND_MODULE(lua,
			luabind::def_lambda("IsSubsystemAvailable", [this](const String& name)
			{
				return IsAvailable(name);
			}),
			luabind::def_lambda("GetSubsystemStatus", [this](const String& name)
			{
				return IsAvailable(name) ? "AVAILABLE" : "UNAVAILABLE";
			}));
#ifdef __INTELLISENSE__
#pragma warning(default : 28)
#endif
	}

	bool SubsystemClient::IsAvailable(const game::Subsystem subsystem) const
	{
		return subsystem < game::subsystem::Count_ && m_status[subsystem] == game::subsystem_status::Available;
	}

	bool SubsystemClient::IsAvailable(const String& name) const
	{
		game::Subsystem subsystem;
		return game::FindSubsystemByName(name.c_str(), subsystem) && IsAvailable(subsystem);
	}

	PacketParseResult SubsystemClient::OnSubsystemStatus(game::IncomingPacket& packet)
	{
		uint8 count = 0;
		if (!(packet >> io::read<uint8>(count)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint8 i = 0; i < count; ++i)
		{
			uint8 id = 0;
			uint8 status = 0;
			if (!(packet >> io::read<uint8>(id) >> io::read<uint8>(status)))
			{
				return PacketParseResult::Disconnect;
			}

			// Newer servers may know subsystems this client does not: ignore them.
			if (id >= game::subsystem::Count_)
			{
				continue;
			}

			const auto newStatus = status == game::subsystem_status::Available ? game::subsystem_status::Available : game::subsystem_status::Unavailable;
			m_status[id] = newStatus;

			// Fire for every received entry, including the full list sent after entering the
			// world, so UI can initialize itself from the event alone.
			DLOG("Subsystem " << game::GetSubsystemName(id) << " is " << (newStatus == game::subsystem_status::Available ? "available" : "unavailable"));
			FrameManager::Get().TriggerLuaEvent("SUBSYSTEM_STATUS_CHANGED", std::string(game::GetSubsystemName(id)), newStatus == game::subsystem_status::Available);
		}

		return PacketParseResult::Pass;
	}
}
