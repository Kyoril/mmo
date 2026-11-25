-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- The main action bar switched to page 2 (ActionBar_SetPage): its twelve buttons show the slots of
-- the hidden second bar, and the page plate left of the health rail reads 2. Send
-- "lua ActionBar_SetPage(1)" or "lua ActionBar_HoldPage(true)" to the live app for the other states.

dofile(UiTest.scenarioDir .. "wayfinder.lua")

HideUIPanel(FriendsFrame);
SetCVar("ShowActionBar2", "0");
ActionBar_UpdateSecondBarVisibility();
ActionBar_SetPage(2);
