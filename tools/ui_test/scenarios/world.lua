-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- The game UI right after entering the world: the events the client raises once the player exists.
-- Other scenarios start with dofile(UiTest.scenarioDir .. "world.lua") and then open their window.

UiTest.FireEvent("PLAYER_ENTER_WORLD")
UiTest.FireEvent("MONEY_CHANGED")
UiTest.FireEvent("PLAYER_XP_CHANGED")
UiTest.FireEvent("ZONE_CHANGED")
