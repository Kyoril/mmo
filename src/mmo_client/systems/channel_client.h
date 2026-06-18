// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "net/realm_connector.h"

#include <vector>

struct lua_State;

namespace mmo
{
	/// Represents a chat channel the local player is currently a member of.
	struct ChannelInfo
	{
		/// Global channel id (matches the proto ChatChannelEntry id on the server).
		uint32 id = 0;
		/// Channel name (also used as a localization key).
		String name;
		/// Channel flag bitfield.
		uint32 flags = 0;
	};

	/// Client-side chat channel system. Maintains the ordered list of joined channels (the list
	/// order determines the local 1-based channel numbers shown to the player) and handles the
	/// channel-related network packets. The server only ever deals in global channel ids; the
	/// local numbering is purely a client-side convenience.
	class ChannelClient final : public NonCopyable
	{
	public:
		explicit ChannelClient(RealmConnector& connector);

	public:
		/// Registers the channel-related packet handlers.
		void Initialize();

		/// Unregisters the packet handlers and clears local state.
		void Shutdown();

		/// Registers the Lua API for chat channels.
		void RegisterScriptFunctions(lua_State* lua);

		/// Requests to join a channel by its (case-insensitive) name.
		void JoinChannel(const String& name);

		/// Requests to leave the channel currently shown under the given local id (1-based).
		void LeaveChannelByLocalId(int32 localId);

		/// Gets the number of channels the player is currently in.
		int32 GetNumChannels() const { return static_cast<int32>(m_channels.size()); }

		/// Gets channel info by local id (1-based), or nullptr if out of range.
		const ChannelInfo* GetChannelInfo(int32 localId) const;

		/// Maps a global channel id to its local id (1-based), or 0 if the player isn't in it.
		int32 GetLocalId(uint32 globalId) const;

		/// Maps a local id (1-based) to a global channel id, or 0 if out of range.
		uint32 GetGlobalId(int32 localId) const;

		/// Gets the name of the channel with the given global id, or an empty string.
		const String& GetChannelNameByGlobalId(uint32 globalId) const;

	private:
		PacketParseResult OnChannelList(game::IncomingPacket& packet);
		PacketParseResult OnChannelNotify(game::IncomingPacket& packet);

	private:
		RealmConnector& m_connector;
		RealmConnector::PacketHandlerHandleContainer m_handlers;

		/// Joined channels in display order; the index + 1 is the local channel number.
		std::vector<ChannelInfo> m_channels;
	};
}
