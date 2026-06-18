// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "channel_client.h"

#include "frame_ui/frame_mgr.h"
#include "log/default_log_levels.h"
#include "luabind_lambda.h"

namespace mmo
{
	namespace
	{
		const String s_emptyString;
	}

	ChannelClient::ChannelClient(RealmConnector& connector)
		: m_connector(connector)
	{
	}

	void ChannelClient::Initialize()
	{
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::ChannelList, *this, &ChannelClient::OnChannelList);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::ChannelNotify, *this, &ChannelClient::OnChannelNotify);
	}

	void ChannelClient::Shutdown()
	{
		m_handlers.Clear();
		m_channels.clear();
	}

	void ChannelClient::RegisterScriptFunctions(lua_State* lua)
	{
#ifdef __INTELLISENSE__
#pragma warning(disable : 28)
#endif
		LUABIND_MODULE(lua,
			luabind::scope(
				luabind::class_<ChannelInfo>("ChannelInfo")
					.def_readonly("id", &ChannelInfo::id)
					.def_readonly("name", &ChannelInfo::name)
					.def_readonly("flags", &ChannelInfo::flags)),

			luabind::def_lambda("JoinChannel", [this](const String& name)
				{ JoinChannel(name); }),
			luabind::def_lambda("LeaveChannel", [this](int32 localId)
				{ LeaveChannelByLocalId(localId); }),
			luabind::def_lambda("GetNumChannels", [this]()
				{ return GetNumChannels(); }),
			luabind::def_lambda("GetChannelInfo", [this](int32 localId)
				{ return GetChannelInfo(localId); }),
			luabind::def_lambda("GetChannelLocalId", [this](uint32 globalId)
				{ return GetLocalId(globalId); }),
			luabind::def_lambda("GetChannelGlobalId", [this](int32 localId)
				{ return static_cast<int32>(GetGlobalId(localId)); }),
			luabind::def_lambda("GetChannelName", [this](uint32 globalId)
				{ return GetChannelNameByGlobalId(globalId).c_str(); }));
#ifdef __INTELLISENSE__
#pragma warning(default : 28)
#endif
	}

	void ChannelClient::JoinChannel(const String& name)
	{
		if (name.empty())
		{
			return;
		}

		m_connector.sendSinglePacket([&name](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::ChannelJoin);
			packet << io::write_dynamic_range<uint8>(name);
			packet.Finish();
		});
	}

	void ChannelClient::LeaveChannelByLocalId(int32 localId)
	{
		const uint32 globalId = GetGlobalId(localId);
		if (globalId == 0)
		{
			return;
		}

		m_connector.sendSinglePacket([globalId](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::ChannelLeave);
			packet << io::write<uint32>(globalId);
			packet.Finish();
		});
	}

	const ChannelInfo* ChannelClient::GetChannelInfo(int32 localId) const
	{
		if (localId < 1 || localId > static_cast<int32>(m_channels.size()))
		{
			return nullptr;
		}

		return &m_channels[localId - 1];
	}

	int32 ChannelClient::GetLocalId(uint32 globalId) const
	{
		for (size_t i = 0; i < m_channels.size(); ++i)
		{
			if (m_channels[i].id == globalId)
			{
				return static_cast<int32>(i) + 1;
			}
		}

		return 0;
	}

	uint32 ChannelClient::GetGlobalId(int32 localId) const
	{
		const ChannelInfo* info = GetChannelInfo(localId);
		return info ? info->id : 0;
	}

	const String& ChannelClient::GetChannelNameByGlobalId(uint32 globalId) const
	{
		for (const auto& channel : m_channels)
		{
			if (channel.id == globalId)
			{
				return channel.name;
			}
		}

		return s_emptyString;
	}

	PacketParseResult ChannelClient::OnChannelList(game::IncomingPacket& packet)
	{
		uint8 count = 0;
		if (!(packet >> io::read<uint8>(count)))
		{
			return PacketParseResult::Disconnect;
		}

		m_channels.clear();
		m_channels.reserve(count);

		for (uint8 i = 0; i < count; ++i)
		{
			ChannelInfo info;
			if (!(packet >> io::read<uint32>(info.id) >> io::read_container<uint8>(info.name) >> io::read<uint32>(info.flags)))
			{
				return PacketParseResult::Disconnect;
			}

			m_channels.push_back(std::move(info));
		}

		FrameManager::Get().TriggerLuaEvent("CHANNEL_LIST_UPDATE");
		return PacketParseResult::Pass;
	}

	PacketParseResult ChannelClient::OnChannelNotify(game::IncomingPacket& packet)
	{
		uint8 notifyType = 0;
		uint32 channelId = 0;
		String channelName;
		if (!(packet >> io::read<uint8>(notifyType) >> io::read<uint32>(channelId) >> io::read_container<uint8>(channelName)))
		{
			return PacketParseResult::Disconnect;
		}

		switch (static_cast<game::channel_notify::Type>(notifyType))
		{
		case game::channel_notify::Joined:
			if (GetLocalId(channelId) == 0)
			{
				ChannelInfo info;
				info.id = channelId;
				info.name = channelName;
				m_channels.push_back(std::move(info));
			}
			break;

		case game::channel_notify::Left:
			for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
			{
				if (it->id == channelId)
				{
					m_channels.erase(it);
					break;
				}
			}
			break;

		default:
			break;
		}

		FrameManager::Get().TriggerLuaEvent("CHANNEL_NOTIFY", static_cast<int32>(notifyType), GetLocalId(channelId), channelName);
		FrameManager::Get().TriggerLuaEvent("CHANNEL_LIST_UPDATE");
		return PacketParseResult::Pass;
	}
}
