-- e2e-own-character: cqc
-- e2e-timeout: 300
--
-- The Hollow Choir's quest chain: 57 "Whispers Under the Barrowfield" -> 58 "The Choir's Two Wings"
-- -> 59 "Silence the Coffinwright", all given and ended by Chaplain Osric Dawnmere.
--
-- This is what actually points a capped character at the dungeon, so it needs to be more than a
-- proto round-trip: the chain is enforced by prevquestid back-links, the objectives name creatures
-- that only exist on map 1, and the capstone offers a reward choice, which is exactly the
-- combination that silently misbehaves if a quest is flagged AutoRewarded.
--
-- Targets are spawned with GM.CreateMonster rather than hunted down in the crypt: GM.KillTarget
-- tags what it kills, so a spawned creature grants real kill credit, and the scenario does not
-- depend on which of the eighteen live skeletons happens to be nearest.

local SKELETON_WARRIOR = 18
local OSSUAR = 84
local VELL = 85
local SEVRIN_WAX = 81

local SASH, BRACERS = 153, 154

GM.Godmode(true)

-- The whole chain is minlevel 10, which is the cap. A fresh character starts at 1, so without this
-- every accept fails and the first turn-in reports "status 2" with nothing to show for it.
GM.LevelUp(9)
Assert(WaitUntil(function() return GetLevel(Me()) >= 10 end, 15000, "reaches the level cap"),
	"the character must be level 10 before any of these quests can be accepted")

--- Spawns `count` of `entry` and kills each, granting quest credit.
local function KillFor(entry, count, label)
	for i = 1, count do
		local g = GM.CreateMonster(entry)
		Assert(g ~= nil, label .. ": spawn " .. i .. " should exist")
		TargetUnit(g)
		GM.KillTarget()
		Assert(WaitUntil(function() return not IsAlive(g) end, 15000, label .. " " .. i .. " dies"),
			label .. ": kill " .. i .. " should register")

		-- Clear the corpse once the kill has been credited. Map 0's world instance is shared
		-- between scenarios, and a boss corpse left lying here is still an object of that entry:
		-- a later scenario doing FindUnitByEntry can latch onto it instead of the live boss on
		-- map 1, and then reads its health as 0 forever.
		GM.DestroyMonster(g)
	end
	Log(string.format("%s: %d killed", label, count))
end

--- Turns in `questId` and proves it paid out. Money is snapshotted immediately before the turn-in
--- because kill gold from the objectives would otherwise mask a turn-in that did nothing.
local function TurnIn(questId, expectedMoney, rewardChoice)
	local before = GetMoney()
	GM.TurnInQuest(questId, rewardChoice)
	Assert(WaitUntil(function() return GetMoney() >= before + expectedMoney end, 15000,
		"quest " .. questId .. " pays out"),
		"quest " .. questId .. " should reward " .. expectedMoney .. " copper (had " .. before .. ")")
	Log("Quest " .. questId .. " turned in")
end

-- === 57: Whispers Under the Barrowfield ===
GM.AcceptQuest(57)
KillFor(SKELETON_WARRIOR, 6, "Skeleton Warrior")
TurnIn(57, 800, 0)
GM.ClearInventory()

-- === 58: The Choir's Two Wings ===
GM.AcceptQuest(58)
KillFor(OSSUAR, 1, "Ossuar")
KillFor(VELL, 1, "Choirmistress Vell")
TurnIn(58, 1500, 0)
GM.ClearInventory()

-- === 59: Silence the Coffinwright ===
GM.AcceptQuest(59)
KillFor(SEVRIN_WAX, 1, "Sevrin Wax")

-- The capstone is the only quest in the chain offering a choice. Take the first reward and check
-- the chosen item is what actually arrives -- a choice quest that hands out the wrong index, or
-- auto-rewards before the choice is read, both show up here.
GM.ClearInventory()
local before = GetMoney()
GM.TurnInQuest(59, 0)
Assert(WaitUntil(function() return GetMoney() >= before + 3000 end, 15000, "quest 59 pays out"),
	"quest 59 should reward 3000 copper")
Assert(WaitUntil(function() return GetItemCount(SASH) >= 1 end, 15000, "chosen reward arrives"),
	"choosing index 0 should grant the Choirbinder's Sash")
Assert(GetItemCount(BRACERS) == 0, "the unchosen reward should not be granted as well")
Log("Quest 59 turned in with reward choice 0")

GM.Godmode(false)
Assert(not IsDisconnected(), "world server should still be alive")
