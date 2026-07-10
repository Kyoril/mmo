-- Regression: character progression pipeline — level-ups (stat recalculation,
-- field replication) and money grants.

local me = Me()

local levelBefore = GetLevel(me)
local maxHealthBefore = GetMaxHealth(me)
local moneyBefore = GetMoney()

Assert(levelBefore >= 1, "player should have a valid level")
Assert(maxHealthBefore > 0, "player should have max health")

GM.LevelUp(2)
Assert(WaitUntil(function() return GetLevel(me) == levelBefore + 2 end, 10000, "level increases by 2"),
	"level should rise from " .. levelBefore .. " to " .. (levelBefore + 2) .. ", got " .. tostring(GetLevel(me)))

-- Unit stat formulas scale health with level; the exact number is data-driven,
-- so only assert the direction.
Assert(WaitUntil(function() return GetMaxHealth(me) > maxHealthBefore end, 10000, "max health scales"),
	"max health should increase with level (before " .. maxHealthBefore .. ", now " .. tostring(GetMaxHealth(me)) .. ")")

GM.GiveMoney(10000)
Assert(WaitUntil(function() return GetMoney() >= moneyBefore + 10000 end, 10000, "money arrives"),
	"money should increase by 10000 copper (before " .. moneyBefore .. ", now " .. tostring(GetMoney()) .. ")")

Log("Progression verified: level " .. levelBefore .. " -> " .. GetLevel(me)
	.. ", max health " .. maxHealthBefore .. " -> " .. GetMaxHealth(me)
	.. ", money " .. moneyBefore .. " -> " .. GetMoney())
