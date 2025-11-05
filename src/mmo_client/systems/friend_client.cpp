// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "friend_client.h"

#include "frame_ui/frame_mgr.h"
#include "log/default_log_levels.h"
#include "luabind_lambda.h"

namespace mmo
{
    FriendClient::FriendClient(RealmConnector &connector, const proto_client::RaceManager &races, const proto_client::ClassManager &classes)
        : m_connector(connector), m_races(races), m_classes(classes)
    {
    }

    void FriendClient::Initialize()
    {
        m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::FriendInvite, *this, &FriendClient::OnFriendInvite);
        m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::FriendListUpdate, *this, &FriendClient::OnFriendListUpdate);
        m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::FriendStatusChange, *this, &FriendClient::OnFriendStatusChange);
        m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::FriendCommandResult, *this, &FriendClient::OnFriendCommandResult);
    }

    void FriendClient::Shutdown()
    {
        m_handlers.Clear();
    }

    void FriendClient::RegisterScriptFunctions(lua_State *lua)
    {
#ifdef __INTELLISENSE__
#pragma warning(disable : 28)
#endif
        LUABIND_MODULE(lua,
                       luabind::scope(
                           luabind::class_<FriendInfo>("FriendInfo")
                               .def_readonly("name", &FriendInfo::name)
                               .def_readonly("level", &FriendInfo::level)
                               .def_readonly("className", &FriendInfo::className)
                               .def_readonly("raceName", &FriendInfo::raceName)
                               .def_readonly("online", &FriendInfo::online)),

                       luabind::def_lambda("FriendInviteByName", [this](const String &name)
                                           { FriendInviteByName(name); }),
                       luabind::def_lambda("AcceptFriend", [this]()
                                           { AcceptFriend(); }),
                       luabind::def_lambda("DeclineFriend", [this]()
                                           { DeclineFriend(); }),
                       luabind::def_lambda("RemoveFriendByName", [this](const String &name)
                                           { RemoveFriendByName(name); }),
                       luabind::def_lambda("RequestFriendList", [this]()
                                           { RequestFriendList(); }),
                       luabind::def_lambda("GetNumFriends", [this]()
                                           { return GetNumFriends(); }),
                       luabind::def_lambda("GetFriendInfo", [this](int32 index)
                                           { return GetFriendInfo(index); }),
                       luabind::def_lambda("GetPendingInviter", [this]()
                                           { return GetPendingInviter().c_str(); }));
#ifdef __INTELLISENSE__
#pragma warning(default : 28)
#endif
    }

    void FriendClient::FriendInviteByName(const String &name)
    {
        m_connector.sendSinglePacket([&name](game::OutgoingPacket &packet)
                                     {
packet.Start(game::client_realm_packet::FriendInvite);
packet << io::write_dynamic_range<uint8>(name);
packet.Finish(); });

        DLOG("Sent friend invite to: " << name);
    }

    void FriendClient::AcceptFriend()
    {
        m_connector.sendSinglePacket([](game::OutgoingPacket &packet)
                                     {
packet.Start(game::client_realm_packet::FriendAccept);
packet.Finish(); });

        m_pendingInviter.clear();
        DLOG("Accepted friend invite");
    }

    void FriendClient::DeclineFriend()
    {
        m_connector.sendSinglePacket([](game::OutgoingPacket &packet)
                                     {
packet.Start(game::client_realm_packet::FriendDecline);
packet.Finish(); });

        m_pendingInviter.clear();
        DLOG("Declined friend invite");
    }

    void FriendClient::RemoveFriendByName(const String &name)
    {
        m_connector.sendSinglePacket([&name](game::OutgoingPacket &packet)
                                     {
packet.Start(game::client_realm_packet::FriendRemove);
packet << io::write_dynamic_range<uint8>(name);
packet.Finish(); });

        DLOG("Requested friend removal: " << name);
    }

    void FriendClient::RequestFriendList()
    {
        m_connector.sendSinglePacket([](game::OutgoingPacket &packet)
                                     {
packet.Start(game::client_realm_packet::FriendListRequest);
packet.Finish(); });

        DLOG("Requested friend list");
    }

    int32 FriendClient::GetNumFriends() const
    {
        return static_cast<int32>(m_friends.size());
    }

    const FriendInfo *FriendClient::GetFriendInfo(int32 index) const
    {
        if (index < 0 || index >= static_cast<int32>(m_friends.size()))
        {
            return nullptr;
        }

        return &m_friends[index];
    }

    PacketParseResult FriendClient::OnFriendInvite(game::IncomingPacket &packet)
    {
        String inviterName;
        if (!(packet >> io::read_container<uint8>(inviterName)))
        {
            return PacketParseResult::Disconnect;
        }

        m_pendingInviter = inviterName;

        // Trigger Lua event for UI
        FrameManager::Get().TriggerLuaEvent("FRIEND_INVITE", inviterName);

        ILOG("Received friend invite from: " << inviterName);
        return PacketParseResult::Pass;
    }

    PacketParseResult FriendClient::OnFriendListUpdate(game::IncomingPacket &packet)
    {
        uint16 count = 0;
        if (!(packet >> io::read<uint16>(count)))
        {
            return PacketParseResult::Disconnect;
        }

        m_friends.clear();
        m_friends.reserve(count);

        for (uint16 i = 0; i < count; ++i)
        {
            FriendInfo friendInfo;

            uint32 raceId = 0;
            uint32 classId = 0;
            uint8 online = 0;

            if (!(packet >> io::read<uint64>(friendInfo.guid) >> io::read_container<uint8>(friendInfo.name) >> io::read<uint32>(friendInfo.level) >> io::read<uint32>(raceId) >> io::read<uint32>(classId) >> io::read<uint8>(online)))
            {
                return PacketParseResult::Disconnect;
            }

            // Resolve race and class names
            const proto_client::RaceEntry *race = m_races.getById(raceId);
            if (race)
            {
                friendInfo.raceName = race->name();
            }

            const proto_client::ClassEntry *classEntry = m_classes.getById(classId);
            if (classEntry)
            {
                friendInfo.className = classEntry->name();
            }
            friendInfo.online = (online != 0);

            m_friends.push_back(std::move(friendInfo));
        }

        // Trigger Lua event for UI update
        FrameManager::Get().TriggerLuaEvent("FRIEND_LIST_UPDATE");

        ILOG("Friend list updated: " << count << " friends");
        return PacketParseResult::Pass;
    }

    PacketParseResult FriendClient::OnFriendStatusChange(game::IncomingPacket &packet)
    {
        uint64 friendGuid = 0;
        uint8 online = 0;

        if (!(packet >> io::read<uint64>(friendGuid) >> io::read<uint8>(online)))
        {
            return PacketParseResult::Disconnect;
        }

        // Find friend and update online status
        for (auto &friendInfo : m_friends)
        {
            if (friendInfo.guid == friendGuid)
            {
                friendInfo.online = (online != 0);

                // Trigger Lua event for UI update
                FrameManager::Get().TriggerLuaEvent("FRIEND_STATUS_CHANGE", friendInfo.name, online != 0);

                DLOG("Friend status changed: " << friendInfo.name << " is now " << (online ? "online" : "offline"));
                break;
            }
        }

        return PacketParseResult::Pass;
    }

    PacketParseResult FriendClient::OnFriendCommandResult(game::IncomingPacket &packet)
    {
        uint8 result = 0;
        String playerName;

        if (!(packet >> io::read<uint8>(result) >> io::read_container<uint8>(playerName)))
        {
            return PacketParseResult::Disconnect;
        }

        // Trigger Lua event with result and player name
        FrameManager::Get().TriggerLuaEvent("FRIEND_COMMAND_RESULT", static_cast<int32>(result), playerName);

        DLOG("Friend command result: " << static_cast<int32>(result) << " for player: " << playerName);
        return PacketParseResult::Pass;
    }
}
