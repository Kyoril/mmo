-- e2e-own-character: dly
-- e2e-timeout: 240
--
-- The first content to use the daily quest system. quest_reset.cpp has been in the tree for a
-- while with nothing flagged Daily to exercise it, so this covers the half that does not need a
-- clock: a daily can be taken and turned in, and once turned in it cannot be taken again in the
-- same day.
--
-- That second half is the one worth pinning. Daily (0x0040) and Repeatable (0x0200) both satisfy
-- IsRepeatableQuest, and a daily authored with the wrong bit would be immediately repeatable --
-- indistinguishable from working until someone farms it in a loop. Rolling the boundary itself
-- needs the server's clock and is not covered here.

local THIN_THE_OSSUARY = 62      -- daily: slay 8 Skeleton Warriors
local SKELETON_WARRIOR = 18
local REWARD = 1200

GM.Godmode(true)
GM.LevelUp(9)
Assert(WaitUntil(function() return GetLevel(Me()) >= 10 end, 15000, "reaches the level cap"),
	"dailies are minlevel 10, so the character has to be capped first")

local function KillSkeletons(count)
	for i = 1, count do
		local g = GM.CreateMonster(SKELETON_WARRIOR)
		Assert(g ~= nil, "skeleton " .. i .. " should spawn")
		TargetUnit(g)
		GM.KillTarget()
		Assert(WaitUntil(function() return not IsAlive(g) end, 15000, "skeleton " .. i .. " dies"),
			"kill " .. i .. " should register")
		-- Map 0's instance is shared between scenarios; leaving corpses of a common entry behind
		-- makes later FindUnitByEntry calls ambiguous.
		GM.DestroyMonster(g)
	end
end

-- === First run of the day: it should behave like any other quest ===
GM.AcceptQuest(THIN_THE_OSSUARY)
KillSkeletons(8)

local before = GetMoney()
GM.TurnInQuest(THIN_THE_OSSUARY, 0)
Assert(WaitUntil(function() return GetMoney() >= before + REWARD end, 15000, "daily pays out"),
	"the daily should reward " .. REWARD .. " copper on its first completion")
Log("Daily 62 accepted and turned in")

-- === Second run the same day: it must not be available again ===
local afterFirst = GetMoney()
GM.AcceptQuest(THIN_THE_OSSUARY)
KillSkeletons(8)
GM.TurnInQuest(THIN_THE_OSSUARY, 0)

-- Give the server the same amount of time the successful turn-in needed, so this is a real
-- opportunity to pay out rather than a race the assertion happens to win.
Sleep(5000)
Assert(GetMoney() < afterFirst + REWARD,
	"a daily already completed today must not pay out a second time (money went from "
		.. afterFirst .. " to " .. GetMoney() .. ")")
Log("Daily 62 correctly refused a second completion in the same day")

GM.Godmode(false)
Assert(not IsDisconnected(), "world server should still be alive")
