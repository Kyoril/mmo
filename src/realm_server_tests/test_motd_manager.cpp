// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mock_databases.h"

#include "realm_server/motd_manager.h"

using namespace mmo;

namespace
{
    /// Synchronous dispatcher: runs the action immediately on the calling thread.
    /// This lets tests verify database interactions without real async infrastructure.
    void syncDispatch(const std::function<void()>& action) { action(); }
}

TEST_CASE("MOTDManager returns default MOTD before database responds", "[motd]")
{
    MockMOTDDatabase mockDb;
    mockDb.motdToReturn = std::nullopt; // simulate no DB value yet

    // Use a no-op async worker so the load request is queued but not executed
    AsyncMOTDDatabase asyncDb{ mockDb,
        [](const std::function<void()>&) { /* drop the request */ },
        [](const std::function<void()>&) { /* drop results  */ } };

    MOTDManager mgr{ asyncDb };

    // Constructor queues a load; since async worker is no-op, MOTD stays at default
    REQUIRE(mgr.GetMessageOfTheDay() == "Welcome to the server!");
}

TEST_CASE("MOTDManager updates MOTD when database returns a value", "[motd]")
{
    MockMOTDDatabase mockDb;
    mockDb.motdToReturn = String("Hello, adventurers!");

    // Synchronous dispatcher executes work inline so results arrive before assertions
    AsyncMOTDDatabase asyncDb{ mockDb, syncDispatch, syncDispatch };

    MOTDManager mgr{ asyncDb };

    REQUIRE(mgr.GetMessageOfTheDay() == "Hello, adventurers!");
}

TEST_CASE("MOTDManager SetMessageOfTheDay persists to database", "[motd]")
{
    MockMOTDDatabase mockDb;
    mockDb.motdToReturn = std::nullopt;

    AsyncMOTDDatabase asyncDb{ mockDb, syncDispatch, syncDispatch };

    MOTDManager mgr{ asyncDb };

    mgr.SetMessageOfTheDay("New MOTD");

    REQUIRE(mockDb.lastSetMotd.has_value());
    REQUIRE(*mockDb.lastSetMotd == "New MOTD");
    REQUIRE(mgr.GetMessageOfTheDay() == "New MOTD");
}

TEST_CASE("MOTDManager fires motdChanged signal when MOTD is updated", "[motd]")
{
    MockMOTDDatabase mockDb;
    mockDb.motdToReturn = std::nullopt;

    AsyncMOTDDatabase asyncDb{ mockDb, syncDispatch, syncDispatch };

    MOTDManager mgr{ asyncDb };

    String signalValue;
    auto conn = mgr.motdChanged.connect([&signalValue](const String& m) { signalValue = m; });

    mgr.SetMessageOfTheDay("Signal test MOTD");

    REQUIRE(signalValue == "Signal test MOTD");
}
