-- e2e-class: 1
-- e2e-timeout: 900
-- e2e-own-character: q
--
-- (Own character: charge_spell shares the warrior character and GM-levels it;
-- this scenario must start at level 1.)
--
-- Leveling coverage: verifies that the quest content in the world carries a
-- character from level 1 to level 10.
--
-- The quest path is SIMULATED via GM commands rather than played out:
--   * quests are accepted/turned in with GM.AcceptQuest/GM.TurnInQuest
--     (no navigation to questgivers),
--   * kill objectives spawn the target creature at the character and GM-kill
--     it (the GM kill tags the creature, so real kill XP and quest kill
--     credit are granted),
--   * item objectives are satisfied with GM.AddItem.
--
-- This intentionally UNDER-counts real play (incidental kills on the way and
-- drop-farming kills for collection quests grant nothing here), so a real
-- player following the same quests ends up with more XP than this scenario.
-- If this scenario reaches level 10, live players can too.
--
-- The table below mirrors the natural play order (warrior character):
-- Haven/farm starter quests -> Briarwatch March -> Kingsroad Waypost ->
-- Mirewater camp -> Barrowfront camp.
--
-- Class-unlock chains (quests 31-40) are deliberately NOT part of the path:
-- reaching level 10 must not require doing the class-change content.

local PATH = {
	{ q = 1,  kills = { { 14, 10 } } },                  -- The Boar Problem
	{ q = 2,  items = { { 37, 5 } } },                   -- Gathering Healing Herbs
	{ q = 3 },                                           -- An Urgent Request (warrior)
	{ q = 9,  items = { { 60, 1 } } },                   -- A Crate Too Far
	{ q = 5,  kills = { { 19, 12 } } },                  -- Boars in the Thickets
	{ q = 10, kills = { { 28, 6 } } },                   -- Rat Troubles
	{ q = 24, items = { { 120, 6 } } },                  -- The Red Armbands
	{ q = 25 },                                          -- Briarwatch Dispatch
	{ q = 26, kills = { { 44, 8 } } },                   -- Teeth at the Treeline
	{ q = 6,  kills = { { 21, 1 } } },                   -- The Alpha of the Herd
	{ q = 28, items = { { 121, 5 }, { 142, 3 } } },      -- For Camp and Kettle
	{ q = 54, kills = { { 67, 6 } } },                   -- Wolves at the Watch
	{ q = 19, items = { { 66, 10 }, { 67, 12 } } },      -- Foundations of Progress
	{ q = 27, kills = { { 48, 6 }, { 49, 2 } } },        -- The Old King's Road
	{ q = 29, kills = { { 45, 6 } }, items = { { 141, 2 } } }, -- Deepwood Teeth
	{ q = 41 },                                          -- Word for the Waypost
	{ q = 30, kills = { { 50, 2 }, { 51, 1 } } },        -- Broken Ground
	{ q = 42, kills = { { 66, 10 } } },                  -- Thinning the Roadside Packs
	{ q = 43, items = { { 139, 8 } }, reward = 126 },    -- Contraband Recovery
	{ q = 44, reward = 128 },                            -- Past the Reeds
	{ q = 45, kills = { { 55, 10 } } },                  -- Rats in the Reeds
	{ q = 55, items = { { 137, 6 } } },                  -- Tails for the Tally
	{ q = 46, items = { { 138, 6 } }, reward = 129 },    -- A Sharper Scent
	{ q = 47, kills = { { 57, 8 } }, reward = 131 },     -- Poachers' Toll
	{ q = 48 },                                          -- The Silent Barrowfield
	{ q = 49, kills = { { 58, 10 } } },                  -- Lay Them to Rest
	{ q = 50, kills = { { 59, 10 } }, reward = 133 },    -- Bones That Walk
	{ q = 51, items = { { 140, 8 } }, reward = 132 },    -- Fetishes of the Grave
	{ q = 52, kills = { { 60, 6 } } },                   -- Silence the Gravespeakers
	{ q = 53, kills = { { 61, 1 } }, reward = 136 },     -- The Barrow-King
}

local me = Me()
Assert(GetLevel(me) >= 1, "player should be alive and at least level 1")

local function killOne(entry)
	local guid = GM.CreateMonster(entry)
	Assert(guid ~= nil, "creature " .. entry .. " should spawn")
	TargetUnit(guid)
	GM.KillTarget()
	Assert(WaitUntil(function() return not IsAlive(guid) end, 10000, "creature dies"),
		"creature " .. entry .. " should die from the GM kill")
	GM.DestroyMonster(guid)
end

for index, step in ipairs(PATH) do
	local questId = step.q

	Log("Quest " .. questId .. " (step " .. index .. "/" .. #PATH .. ", level " .. GetLevel(me) .. ")")

	GM.AcceptQuest(questId)

	-- Objectives: kills are earned for real (spawn + tagged GM kill -> kill
	-- xp and quest credit), items are granted directly.
	if step.kills then
		for _, k in ipairs(step.kills) do
			for i = 1, k[2] do
				killOne(k[1])
			end
		end
		-- Let the kill xp of the last kill replicate before snapshotting,
		-- otherwise it could satisfy the turn-in wait below.
		Sleep(500)
	end
	if step.items then
		for _, it in ipairs(step.items) do
			GM.AddItem(it[1], it[2])
		end
	end

	-- Snapshot immediately before the turn-in: the wait below must only be
	-- satisfied by the turn-in rewards themselves.
	local xpBefore = GetXp()
	local levelBefore = GetLevel(me)
	local moneyBefore = GetMoney()
	local rewardCountBefore = step.reward and GetItemCount(step.reward) or 0

	GM.TurnInQuest(questId, 0)

	-- The turn-in is observable through xp (below max level), a level up,
	-- quest money, or the reward item landing in the inventory.
	local ok = WaitUntil(function()
		if GetXp() ~= xpBefore then return true end
		if GetLevel(me) > levelBefore then return true end
		if GetMoney() ~= moneyBefore then return true end
		if step.reward and GetItemCount(step.reward) > rewardCountBefore then return true end
		return false
	end, 15000, "quest " .. questId .. " turn-in")
	Assert(ok, "quest " .. questId .. " should be turned in (accepted? objectives complete? level "
		.. GetLevel(me) .. ", xp " .. tostring(GetXp()) .. ")")

	-- Real players equip and vendor their loot; the simulation would fill its
	-- backpack after a handful of reward quests instead. Clear it so
	-- CanStoreItems never blocks a later reward.
	GM.ClearInventory()
end

Assert(GetLevel(me) >= 10,
	"quest path should carry the character to level 10, got " .. GetLevel(me))

Log("Leveling verified: the quest path alone reaches level " .. GetLevel(me)
	.. " (xp " .. GetXp() .. "/" .. GetNextLevelXp() .. ")")
