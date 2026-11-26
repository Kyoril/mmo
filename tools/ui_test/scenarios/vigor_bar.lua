-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
-- Vigor bar above the action bar: the character knows Dash/Glider and has spent some Vigor.
-- Try other states with: lua UiTest.data.vigor.current = 12 / event PLAYER_VIGOR_CHANGED
dofile(UiTest.scenarioDir .. "world.lua")

UiTest.data.vigor = { current = 62, max = 100 }
UiTest.FireEvent("PLAYER_VIGOR_CHANGED")
