-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- The overlay of a cutscene (CutsceneFrame): a subtitle and the hint how to skip, over a picture
-- that is partly faded to black. The client hides the rest of the ui itself while a cutscene plays;
-- this app does not, so the game ui stays visible underneath here.
--
-- Change UiTest.data.cutscene from the command line to see other states, e.g.
--   lua UiTest.data.cutscene.fade = 1
--   lua UiTest.data.cutscene.skipHint = false
--   event CUTSCENE_ENDED 1 1        (the black fades out)

dofile(UiTest.scenarioDir .. "world.lua")

UiTest.data.cutscene.playing = true
UiTest.data.cutscene.fade = 0.35
UiTest.data.cutscene.subtitle = "But the Haven Road leads on: east, across the river and the Old Stone Bridge, into the wilder lands of Falwyn Forest."
UiTest.data.cutscene.skipHint = true

UiTest.FireEvent("CUTSCENE_STARTED", 1)
