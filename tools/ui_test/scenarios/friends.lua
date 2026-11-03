-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- The friends window with the friend list from mocks/40_friends.lua.

dofile(UiTest.scenarioDir .. "world.lua")

ShowUIPanel(FriendsFrame)
UiTest.FireEvent("FRIEND_LIST_UPDATE")
