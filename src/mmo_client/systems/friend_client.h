// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "client_data/project.h"
#include "net/realm_connector.h"

struct lua_State;

namespace mmo
{
    /// Represents information about a friend in the friend list.
    struct FriendInfo
    {
        uint64 guid = 0;
        String name;
        uint32 level = 0;
        String className;
        String raceName;
        bool online = false;
    };

    /// Client-side friend system manager.
    /// Handles friend-related network packets and maintains the local friend list state.
    class FriendClient final : public NonCopyable
    {
    public:
        /// Initializes the friend client system.
        /// @param connector Reference to the realm connector for packet communication.
        /// @param races Reference to race manager for resolving race names.
        /// @param classes Reference to class manager for resolving class names.
        explicit FriendClient(RealmConnector &connector, const proto_client::RaceManager &races, const proto_client::ClassManager &classes);

    public:
        /// Initializes the friend system and registers packet handlers.
        void Initialize();

        /// Shuts down the friend system and unregisters packet handlers.
        void Shutdown();

        /// Registers Lua API functions for friend operations.
        /// @param lua The Lua state to register functions with.
        void RegisterScriptFunctions(lua_State *lua);

        /// Sends a friend invitation to a player by name.
        /// @param name The name of the player to invite.
        void FriendInviteByName(const String &name);

        /// Accepts a pending friend invitation.
        void AcceptFriend();

        /// Declines a pending friend invitation.
        void DeclineFriend();

        /// Removes a friend from the friend list by name.
        /// @param name The name of the friend to remove.
        void RemoveFriendByName(const String &name);

        /// Requests the complete friend list from the server.
        void RequestFriendList();

        /// Gets the number of friends in the friend list.
        /// @return The friend count.
        int32 GetNumFriends() const;

        /// Gets information about a friend by index.
        /// @param index The zero-based index of the friend (0 to GetNumFriends()-1).
        /// @return Pointer to friend info, or nullptr if index is out of range.
        const FriendInfo *GetFriendInfo(int32 index) const;

        /// Gets the name of the player who sent a pending friend invite.
        /// @return The inviter's name, or empty string if no pending invite.
        const String &GetPendingInviter() const { return m_pendingInviter; }

    private:
        /// Handles incoming friend invitation packet.
        PacketParseResult OnFriendInvite(game::IncomingPacket &packet);

        /// Handles friend list update packet.
        PacketParseResult OnFriendListUpdate(game::IncomingPacket &packet);

        /// Handles friend status change packet (online/offline).
        PacketParseResult OnFriendStatusChange(game::IncomingPacket &packet);

        /// Handles friend command result packet.
        PacketParseResult OnFriendCommandResult(game::IncomingPacket &packet);

    private:
        RealmConnector &m_connector;
        RealmConnector::PacketHandlerHandleContainer m_handlers;

        /// List of all friends with their information.
        std::vector<FriendInfo> m_friends;

        /// Name of player who sent pending friend invite.
        String m_pendingInviter;

        const proto_client::RaceManager &m_races;
        const proto_client::ClassManager &m_classes;
    };
}
