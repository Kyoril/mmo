// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mock_databases.h"

#include "realm_server/player_group.h"
#include "realm_server/player_manager.h"
#include "realm_server/motd_manager.h"
#include "base/timer_queue.h"

#include "asio/io_service.hpp"

using namespace mmo;

namespace
{
    void syncDispatch(const std::function<void()>& action) { action(); }

    struct PlayerGroupFixture
    {
        asio::io_service io;
        TimerQueue timerQueue{ io };

        MockMOTDDatabase motdDb;
        AsyncMOTDDatabase asyncMotdDb{ motdDb, syncDispatch, syncDispatch };
        MOTDManager motdMgr{ asyncMotdDb };
        PlayerManager playerMgr{ 100, motdMgr };

        MockGroupDatabase groupDb;

        std::shared_ptr<PlayerGroup> MakeGroup(uint64 id = 1)
        {
            return std::make_shared<PlayerGroup>(
                id, playerMgr,
                AsyncGroupDatabase{ groupDb, syncDispatch, syncDispatch },
                timerQueue);
        }
    };
}

TEST_CASE("PlayerGroup GetId returns the id passed to the constructor", "[player_group]")
{
    PlayerGroupFixture f;
    auto group = f.MakeGroup(42);
    REQUIRE(group->GetId() == 42);
}

TEST_CASE("PlayerGroup GetLeader returns 0 before Create is called", "[player_group]")
{
    PlayerGroupFixture f;
    auto group = f.MakeGroup(1);
    REQUIRE(group->GetLeader() == 0);
}

TEST_CASE("PlayerGroup IsFull returns false for empty group", "[player_group]")
{
    PlayerGroupFixture f;
    auto group = f.MakeGroup(1);
    REQUIRE_FALSE(group->IsFull());
}

TEST_CASE("PlayerGroup IsMember returns false for unknown guid", "[player_group]")
{
    PlayerGroupFixture f;
    auto group = f.MakeGroup(1);
    group->Create(100, "TestLeader"); // leader must be set before IsMember is valid
    // Leader is a member; an unknown guid is not
    REQUIRE(group->IsMember(100));
    REQUIRE_FALSE(group->IsMember(9999));
}

TEST_CASE("PlayerGroup AddInvite returns PartyResult::Ok for valid invite", "[player_group]")
{
    PlayerGroupFixture f;
    auto group = f.MakeGroup(1);
    group->Create(100, "TestLeader");

    const PartyResult result = group->AddInvite(200);
    // Should accept the invite (not a member, group not full)
    REQUIRE(result == PartyResult::Ok);
}
