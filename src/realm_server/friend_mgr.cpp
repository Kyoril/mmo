// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "friend_mgr.h"
#include "player.h"
#include "base/macros.h"
#include "log/default_log_levels.h"
#include "game_protocol/game_protocol.h"

namespace mmo
{
    FriendMgr::FriendMgr(AsyncDatabase &asyncDatabase, PlayerManager &playerManager)
        : m_asyncDatabase(asyncDatabase), m_playerManager(playerManager)
    {
    }

    void FriendMgr::LoadAllFriendships()
    {
        ILOG("Loading friendships...");
        const auto startTime = GetAsyncTimeMs();

        // Load all characters and their friend lists
        // We'll query each character's friends individually since we have LoadFriendList per character
        // For initial implementation, we'll mark as loaded immediately and load on-demand
        // A future optimization would be to add a LoadAllFriendships database method

        m_loaded = true;
        ILOG("Friend system initialized (friendships loaded on-demand per player login) in " << (GetAsyncTimeMs() - startTime) << " ms");
    }

    bool FriendMgr::AreFriends(uint64 charId1, uint64 charId2) const
    {
        const auto it = m_friendshipsByCharacter.find(charId1);
        if (it == m_friendshipsByCharacter.end())
        {
            return false;
        }

        const auto &friends = it->second;
        for (const auto &friendData : friends)
        {
            if (friendData.guid == charId2)
            {
                return true;
            }
        }

        return false;
    }

    void FriendMgr::NotifyFriendStatusChange(uint64 characterGuid, bool online)
    {
        // Find all characters who have added this character as a friend (one-sided friendship)
        // We need to query the database since our cache only stores who WE friended, not who friended US
        auto admirerHandler = [this, characterGuid, online](std::vector<uint64> admirerIds)
        {
            // Send status change packet to each online admirer
            for (const auto admirerId : admirerIds)
            {
                Player *admirerPlayer = m_playerManager.GetPlayerByCharacterGuid(admirerId);
                if (admirerPlayer)
                {
                    admirerPlayer->SendPacket([characterGuid, online](game::OutgoingPacket &packet)
                    {
                        packet.Start(game::realm_client_packet::FriendStatusChange);
                        packet << io::write<uint64>(characterGuid);
                        packet << io::write<uint8>(online ? 1 : 0);
                        packet.Finish();
                    });
                }
            }
        };

        m_asyncDatabase.asyncRequest(std::move(admirerHandler), &IDatabase::GetCharactersWithFriend, characterGuid);
    }

    bool FriendMgr::CanAddFriend(uint64 characterId) const
    {
        const auto it = m_friendCountByChar.find(characterId);
        if (it == m_friendCountByChar.end())
        {
            return true; // No friends yet, can definitely add
        }

        return it->second < game::MAX_FRIENDS;
    }

    uint32 FriendMgr::GetFriendCount(uint64 characterId) const
    {
        const auto it = m_friendCountByChar.find(characterId);
        if (it == m_friendCountByChar.end())
        {
            return 0;
        }

        return it->second;
    }

    void FriendMgr::LoadCharacterFriends(uint64 characterId, std::function<void(const std::vector<FriendData> &)> callback)
    {
        auto handler = [this, characterId, callback](std::optional<std::vector<FriendData>> friends)
        {
            if (!friends)
            {
                WLOG("Failed to load friend list for character " << characterId);
                callback(std::vector<FriendData>());
                return;
            }

            // Cache the friend list
            m_friendshipsByCharacter[characterId] = *friends;
            m_friendCountByChar[characterId] = static_cast<uint32>(friends->size());

            // Also cache bidirectional - add this character to each friend's list
            for (const auto &friendData : *friends)
            {
                // We need to create a FriendData for the current character
                // But we don't have their info here, so we'll handle bidirectionality
                // when both characters are online or via a separate query
                // For now, just track the count
                auto &friendCount = m_friendCountByChar[friendData.guid];
                // Don't update count here to avoid double-counting
            }

            callback(*friends);
        };

        m_asyncDatabase.asyncRequest(std::move(handler), &IDatabase::LoadFriendList, characterId);
    }
}
