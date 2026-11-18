-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- A brand-new character's arrival card (ArrivalFrame): enter the world, then answer the played-time
-- request with a few seconds. Take screenshots after `wait <frames>` to see the card, the camera
-- pull-in has no visible effect here, and the Haldor hint follows after about 8 seconds.

dofile(UiTest.scenarioDir .. "world.lua")
UiTest.FireEvent("TIME_PLAYED_UPDATED", 5)
