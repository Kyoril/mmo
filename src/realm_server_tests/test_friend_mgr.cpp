// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mock_databases.h"

#include "realm_server/friend_mgr.h"
#include "realm_server/player_manager.h"
#include "realm_server/motd_manager.h"

using namespace mmo;

namespace
{
    /// Runs database work inline against `database`, and results inline too.
    ///
    /// The pool is not involved here: these tests assert on manager behaviour, not on routing,
    /// so the ordering key is ignored and the work is handed the mock directly.
    template <class TInterface>
    typename mmo::AsyncDatabaseT<TInterface>::WorkDispatcher inlineWorker(TInterface& database)
    {
        return [&database](mmo::uint64, std::function<void(TInterface&)> work) { work(database); };
    }

    void syncResult(std::function<void()> action) { action(); }

    /// Minimal test fixture: creates a MOTDManager + PlayerManager with sync dispatching.
    struct FriendMgrFixture
    {
        MockMOTDDatabase motdDb;
        AsyncMOTDDatabase asyncMotdDb{ inlineWorker<IMOTDDatabase>(motdDb), syncResult };
        MOTDManager motdMgr{ asyncMotdDb };
        PlayerManager playerMgr{ 100, motdMgr };

        MockFriendDatabase mockDb;
        AsyncFriendDatabase asyncFriendDb{ inlineWorker<IFriendDatabase>(mockDb), syncResult };
        FriendMgr mgr{ asyncFriendDb, playerMgr };
    };
}

TEST_CASE("FriendMgr IsLoaded is false before LoadAllFriendships completes", "[friend_mgr]")
{
    MockFriendDatabase mockDb;
    AsyncFriendDatabase asyncDb{
        [](uint64, std::function<void(IFriendDatabase&)>) { /* defer */ },
        [](std::function<void()>) { /* defer */ } };

    MockMOTDDatabase motdDb;
    AsyncMOTDDatabase asyncMotdDb{ inlineWorker<IMOTDDatabase>(motdDb), syncResult };
    MOTDManager motdMgr{ asyncMotdDb };
    PlayerManager playerMgr{ 100, motdMgr };

    FriendMgr mgr{ asyncDb, playerMgr };

    REQUIRE_FALSE(mgr.IsLoaded());
}

TEST_CASE("FriendMgr AreFriends returns false for unknown pair", "[friend_mgr]")
{
    FriendMgrFixture f;
    // No friendships loaded into mock — should return false
    REQUIRE_FALSE(f.mgr.AreFriends(1, 2));
}

TEST_CASE("FriendMgr GetFriendCount returns 0 for unknown character", "[friend_mgr]")
{
    FriendMgrFixture f;
    REQUIRE(f.mgr.GetFriendCount(9999) == 0);
}

TEST_CASE("FriendMgr CanAddFriend returns true for character with no friends", "[friend_mgr]")
{
    FriendMgrFixture f;
    REQUIRE(f.mgr.CanAddFriend(42));
}
