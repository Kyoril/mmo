// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "database.h"
#include "player_manager.h"
#include "base/non_copyable.h"

namespace mmo
{
    /// Manages all friend relationships on the realm server.
    /// Loads friendships at startup and provides fast in-memory lookups.
    /// Handles friend status change notifications to online players.
    class FriendMgr final : public NonCopyable
    {
    public:
        /// Initializes a new instance of the FriendMgr class.
        /// @param asyncDatabase Reference to the async database for loading friendships.
        /// @param playerManager Reference to the player manager for online status checks.
        explicit FriendMgr(AsyncDatabase &asyncDatabase, PlayerManager &playerManager);

    public:
        /// Loads all friendships from the database into memory.
        /// This is called at server startup to populate the friendship cache.
        void LoadAllFriendships();

        /// Checks if two characters are friends.
        /// @param charId1 The first character's guid.
        /// @param charId2 The second character's guid.
        /// @return True if the characters are friends, false otherwise.
        bool AreFriends(uint64 charId1, uint64 charId2) const;

        /// Notifies all online players who have this character as a friend about their status change.
        /// One-sided friendship: only players who added this character see the notification.
        /// @param characterGuid The character whose status changed.
        /// @param online True if the character is now online, false if offline.
        void NotifyFriendStatusChange(uint64 characterGuid, bool online);

        /// Checks if a character can add another friend (not at capacity).
        /// @param characterId The character's guid to check.
        /// @return True if the character has fewer than MAX_FRIENDS, false otherwise.
        bool CanAddFriend(uint64 characterId) const;

        /// Gets the current friend count for a character.
        /// @param characterId The character's guid.
        /// @return The number of friends the character has.
        uint32 GetFriendCount(uint64 characterId) const;

        /// Checks if all friendships have been loaded.
        /// @return True if LoadAllFriendships() has completed successfully.
        bool IsLoaded() const { return m_loaded; }

        /// Loads and caches a character's friend list.
        /// Called when a player logs in to populate the in-memory cache.
        /// @param characterId The character's guid.
        /// @param callback Function called with the friend list after loading.
        void LoadCharacterFriends(uint64 characterId, std::function<void(const std::vector<FriendData> &)> callback);

    private:
        AsyncDatabase &m_asyncDatabase;
        PlayerManager &m_playerManager;

        /// Maps character guid to their list of friends.
        /// One-sided friendships: if A adds B, only m_friendshipsByCharacter[A] contains B.
        /// B does not automatically have A in their list.
        std::map<uint64, std::vector<FriendData>> m_friendshipsByCharacter;

        /// Maps character guid to their friend count for fast capacity checks.
        std::map<uint64, uint32> m_friendCountByChar;

        /// Flag indicating whether all friendships have been loaded.
        volatile bool m_loaded = false;
    };
}
