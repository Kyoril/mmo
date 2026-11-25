-- e2e-timeout: 900
-- e2e-own-character: hvr
--
-- The Haven Road quests (78-94): the level 6-10 content west of the river, from the Old Stone
-- Bridge down the Haven Road into Haven. See docs/haven-road-quests.md.
--
-- This plays the mechanics that are new with it, each through the path a player takes:
--
--   * The King's Toll (79): a gossip option, offered only with the quest, turns a friendly npc
--     hostile (SetFaction), calls his thugs (SetSpawnState) and makes him attack.
--   * Lamps for the Haven Road (80), Root and Branch (85), Stones Out of Tune (93): object-use
--     credit on quest objects.
--   * The Haven Dispatch (81): an area trigger that only fires for a player on the quest
--     (QuestStatus condition) springs an ambush.
--   * Pigs in the Corn (82): a quest item's spell makes sows follow, an area trigger hands the
--     followers in for credit (DeliverFollowers).
--   * The Scarecrow Watch (83/84): an object starts a wave event that ends in a named creature
--     whose loot starts the next quest.
--   * Lights on the Water (87), Sunk Cost (88): overheard scene by area trigger, chest loot.
--   * The Chandler's Customers (90): gossip options on a SUB PAGE of a dialogue grant credit.
--   * A Quiet Word (91), Low Tide at the Basin (92): confrontation by gossip, phased fight.
--   * Dusklings roam the Wainbrook fields at night only.
--
-- Own character: the scenario walks a quest chain and ends in Haven. The time of day is realm
-- state, so it is reset at the end.

local me = Me()

-- units
local PETRA, BRASK, THUG, WAYLAYER, ODELL, HESPER, SOW = 125, 129, 130, 131, 132, 133, 134
local DUSKLING, HARROW, LYSA, TALLOW, TALLOW_FOE = 135, 136, 143, 144, 145
local TOBIAS, HILDA, WENNA, ROSALIND = 99, 100, 102, 103
-- objects
local LANTERN, WATCHFIRE, LURE, SUNKEN, SKIFF = 16, 17, 18, 19, 20
local SHARD_HAVEN, SHARD_INN, SHARD_CAMP = 21, 22, 23
-- items
local TURNIPS, TOKEN, DISPATCH, CIDER, GOODS, MANIFEST, LEDGER = 164, 165, 166, 167, 168, 169, 170
-- gossip menus
local MENU_BRASK, MENU_TOBIAS_ACCOUNT, MENU_HILDA_ACCOUNT, MENU_WENNA_ACCOUNT, MENU_TALLOW = 23, 26, 28, 30, 31
-- area triggers
local AT_AMBUSH, AT_PEN, AT_OVERLOOK = 8, 9, 10

local POS = {
	petra = { -571.0, 10.72, 452.0 },
	brask = { -434.0, 2.32, 362.5 },
	-- the eight road lamp posts south of the inn: the quest objects stand where the road's own
	-- lanterns used to, so these are the post positions from the world
	lanterns = {
		{ -575.88, 10.71, 460.2 }, { -592.56, 13.22, 489.2 }, { -612.63, 15.13, 516.05 }, { -633.81, 16.69, 545.24 },
		{ -653.51, 16.76, 574.34 }, { -671.78, 16.14, 605.08 }, { -689.61, 14.52, 632.66 }, { -701.2, 12.56, 666.71 },
	},
	ambush = { -702.0, 12.6, 668.0 },
	odell = { -711.0, 28.29, 547.0 },
	hesper = { -712.0, 26.71, 562.0 },
	sows = { { -778.31, 27.19, 552.67 }, { -792.41, 28.95, 544.7 }, { -780.22, 28.61, 533.38 }, { -780.29, 30.0, 513.19 }, { -757.08, 29.0, 516.58 }, { -790.29, 31.77, 500.53 } },
	watchfire = { -757.0, 24.19, 584.0 },
	lures = { { -761.52, 25.68, 550.89 }, { -770.52, 23.61, 610.08 }, { -787.88, 26.14, 592.73 }, { -797.32, 31.09, 520.11 }, { -766.87, 29.14, 512.37 } },
	overlook = { -514.0, 1.03, 778.0 },
	skiff = { -475.19, 1.09, 768.41 },
	sunken = { { -414.49, -5.74, 773.21 }, { -464.64, -5.74, 798.73 }, { -424.91, -3.35, 773.58 }, { -409.36, -4.33, 767.22 }, { -475.72, -5.74, 804.94 } },
	lysa = { -672.0, 1.02, 886.0 },
	tobias = { -712.7, 0.64, 975.8 },
	hilda = { -705.8, 0.64, 975.8 },
	wenna = { -670.7, 0.14, 974.8 },
	tallow = { -694.0, 0.66, 978.0 },
	basin = { -768.0, 1.71, 850.5 },
	rosalind = { -692.5, 2.89, 1012.0 },
	shardHaven = { -687.4, 2.89, 1021.6 },
	shardInn = { -574.2, 10.7, 441.4 },
	shardCamp = { -85.4, 0.21, 547.4 },
}

local function Port(p, dx, dz)
	GM.Worldport(0, p[1] + (dx or 0), p[2] + 0.6, p[3] + (dz or 0), 0)
	Sleep(1200)
end

local function Unit(entry, p, what)
	local guid
	Assert(WaitUntil(function()
		guid = FindNearestUnitByEntry(entry, p[1], p[2], p[3])
		return guid ~= nil
	end, 10000, what .. " in view"), what .. " should stand at its place")
	return guid
end

local function Object(entry, p, what)
	local guid
	Assert(WaitUntil(function()
		guid = FindNearestObjectByEntry(entry, p[1], p[2], p[3])
		return guid ~= nil
	end, 10000, what .. " in view"), what .. " should be there for a player on the quest")
	return guid
end

-- A quest counts as done when the realm pays out for it: GM.TurnInQuest only works on a quest whose
-- objectives are complete, so the reward is the proof that every credit arrived.
local function Snapshot()
	return { xp = GetXp(), money = GetMoney(), level = GetLevel(me) }
end

local function Changed(s)
	return GetXp() ~= s.xp or GetMoney() ~= s.money or GetLevel(me) ~= s.level
end

local function ExpectRewarded(questId, choice, what)
	local s = Snapshot()
	GM.TurnInQuest(questId, choice or 0)
	Assert(WaitUntil(function() return Changed(s) end, 10000, what .. " rewarded"),
		"quest " .. questId .. " (" .. what .. ") should be complete and pay its reward")
	Log("Quest " .. questId .. " rewarded: " .. what)
end

local function ExpectNotYetComplete(questId, what)
	local s = Snapshot()
	GM.TurnInQuest(questId, 0)
	Sleep(2000)
	Assert(not Changed(s), "quest " .. questId .. " must not be complete yet: " .. what)
end

local function Kill(guid)
	TargetUnit(guid)
	GM.KillTarget()
	Assert(WaitUntil(function() return not IsAlive(guid) end, 10000, "target dies"), "GM.KillTarget should kill the target")
end

-- World objects are used through the generic Open spell (a 5 s cast), as the game client does it.
local OPEN = 39

local function KillAll(entry, range)
	for _ = 1, 10 do
		local guid = FindNearestUnitByEntry(entry, GetPosX(me), GetPosY(me), GetPosZ(me))
		if not guid or (range and GetDistance(me, guid) > range) then return end
		Kill(guid)
		Sleep(300)
	end
end

-- Everything hostile the Haven Road content puts into the open world.
local HOSTILES = { DUSKLING, 137, 142, 140, 141, 146 }

-- Kills what stands close enough to pull the character into combat, as a player would have to.
local function ClearField()
	for _, entry in ipairs(HOSTILES) do
		KillAll(entry, 45)
	end
end

local function UseObject(guid, what)
	-- Open cannot be cast in combat. Whatever a lure woke has to be dealt with first, as a player
	-- would, so a refused cast kills what is around and tries again.
	for attempt = 1, 5 do
		Assert(CastSpellOnObject(OPEN, guid), "the Open cast on " .. what .. " should be accepted")
		local done = WaitUntil(function()
			local result = LastCastResult()
			return result == "ok" or string.sub(result, 1, 6) == "failed"
		end, 9000, what .. " used")
		if done and LastCastResult() == "ok" then
			return
		end
		Log(what .. ": cast " .. tostring(LastCastResult()) .. ", clearing the field (attempt " .. attempt .. ")")
		ClearField()
		Sleep(2500)
	end
	Fail(what .. " should be usable (cast result " .. tostring(LastCastResult()) .. ")")
end

local function TakeLoot(guid, itemId, count, what, isObject)
	local before = GetItemCount(itemId)
	if isObject then
		-- opening a chest leaves its loot window open
		UseObject(guid, what)
	else
		-- a corpse is looted from next to it
		GM.Worldport(0, GetPosX(guid) + 1.5, GetPosY(guid) + 0.6, GetPosZ(guid), 0)
		Sleep(1200)
		LootUnit(guid)
	end
	Sleep(800)
	for slot = 0, 3 do
		TakeLootItem(slot)
	end
	Assert(WaitUntil(function() return GetItemCount(itemId) >= before + count end, 8000, what),
		what .. " should be lootable (have " .. tostring(GetItemCount(itemId)) .. ")")
	ReleaseLoot(guid)
	Sleep(300)
end

-- ------------------------------------------------------------------------------------------------
-- Setup: level 9 (so xp still moves), unkillable, a fixed time of day.
-- ------------------------------------------------------------------------------------------------
if GetLevel(me) < 9 then
	GM.LevelUp(9 - GetLevel(me))
end
GM.Godmode(true)
GM.SetTimeOfDay("12:00", 1)

-- Breadcrumb 78 has no objective: it completes when accepted.
GM.AcceptQuest(78)
ExpectRewarded(78, 0, "Beyond the Old Stone Bridge")

-- ------------------------------------------------------------------------------------------------
-- 79 The King's Toll: gossip option -> hostile npc.
-- ------------------------------------------------------------------------------------------------
Port(POS.brask, -4, 0)
local brask = Unit(BRASK, POS.brask, "Tollmaster Brask")

-- Without the quest the option is not offered, so picking it does nothing.
SelectGossipOption(brask, MENU_BRASK, 1)
Sleep(2000)
Assert(CountUnitsByEntry(THUG) == 0, "Brask's thugs must not appear for a player who is not on the quest")
Assert(GetTarget(brask) ~= me, "Brask must stay peaceful towards a player who is not on the quest")

Port(POS.petra, 2, 0)
local petra = Unit(PETRA, POS.petra, "Road-Warden Petra Lowell")
AcceptQuestFrom(petra, 79)
Sleep(1000)

Port(POS.brask, -4, 0)
brask = Unit(BRASK, POS.brask, "Tollmaster Brask")
SelectGossipOption(brask, MENU_BRASK, 1)
Assert(WaitUntil(function() return CountUnitsByEntry(THUG) >= 2 end, 10000, "thugs called"),
	"refusing the toll should bring Brask's two thugs out")
Assert(WaitUntil(function() return GetTarget(brask) == me end, 10000, "Brask attacks"),
	"Brask should turn hostile and attack the player who refused the toll")
Kill(brask)
KillAll(THUG)
ExpectRewarded(79, 0, "The King's Toll")

-- ------------------------------------------------------------------------------------------------
-- 81 The Haven Dispatch: the ambush only springs for a courier.
-- ------------------------------------------------------------------------------------------------
Port(POS.ambush)
SendAreaTrigger(AT_AMBUSH)
Sleep(2500)
Assert(CountUnitsByEntry(WAYLAYER) == 0, "the road ambush must not spring for a player without the dispatch quest")

Port(POS.petra, 2, 0)
petra = Unit(PETRA, POS.petra, "Road-Warden Petra Lowell")
AcceptQuestFrom(petra, 81)
Assert(WaitUntil(function() return GetItemCount(DISPATCH) == 1 end, 8000, "dispatch handed over"),
	"accepting the quest should hand over the Sealed Haven Dispatch")

Port(POS.ambush)
SendAreaTrigger(AT_AMBUSH)
Assert(WaitUntil(function() return CountUnitsByEntry(WAYLAYER) >= 2 end, 10000, "ambush springs"),
	"two waylayers should ambush the courier on the Haven Road")
KillAll(WAYLAYER)

Port(POS.lysa, -2, 0)
local lysa = Unit(LYSA, POS.lysa, "Harbourmaster Lysa Fairwind")
local snapshot = Snapshot()
TurnInQuestAt(lysa, 81)
Assert(WaitUntil(function() return Changed(snapshot) end, 10000, "dispatch delivered"),
	"the harbourmaster should take the dispatch and reward the courier")
Assert(WaitUntil(function() return GetItemCount(DISPATCH) == 0 end, 5000, "dispatch taken"),
	"the dispatch should be gone after the turn-in")
Log("Quest 81 rewarded: The Haven Dispatch")

-- ------------------------------------------------------------------------------------------------
-- 80 Lamps for the Haven Road: eight object uses.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(80)
Sleep(500)
for index, lantern in ipairs(POS.lanterns) do
	Port(lantern, 1.5, 0)
	UseObject(Object(LANTERN, lantern, "road lantern " .. index), "road lantern " .. index)
	Sleep(400)
	if index == 4 then
		ExpectNotYetComplete(80, "only four of eight lanterns are lit")
	end
end
ExpectRewarded(80, 0, "Lamps for the Haven Road")

-- ------------------------------------------------------------------------------------------------
-- 82 Pigs in the Corn: turnip -> follow -> pen.
-- ------------------------------------------------------------------------------------------------
Port(POS.hesper, 2, 0)
local hesper = Unit(HESPER, POS.hesper, "Goodwife Hesper Wainbrook")
AcceptQuestFrom(hesper, 82)
Assert(WaitUntil(function() return GetItemCount(TURNIPS) >= 1 end, 8000, "turnips handed over"),
	"accepting the quest should hand over the Wainbrook Turnips")

-- Walking into the pen without sows gives nothing.
SendAreaTrigger(AT_PEN)
Sleep(1000)

-- Walk from sow to sow (no teleports: the ones already following have to keep up).
Port(POS.sows[1], 4, 0)
local lured = {}
local luredCount = 0
for _, spot in ipairs(POS.sows) do
	if luredCount >= 4 then break end

	Assert(MoveTo(spot[1] + 4, spot[2], spot[3], 60000), "should be able to walk through the corn")
	local sow = FindNearestUnitByEntry(SOW, spot[1], spot[2], spot[3])
	if sow and not lured[sow] and GetDistance(me, sow) < 22 then
		TargetUnit(sow)
		Assert(UseItem(TURNIPS, sow), "the turnips should be in the backpack")
		if WaitUntil(function() return GetDistance(me, sow) < 6 end, 15000, "sow follows") then
			lured[sow] = true
			luredCount = luredCount + 1
			Log("Sow " .. luredCount .. " follows")
		end
		-- the turnip spell has a short cooldown
		Sleep(1700)
	end
end
Assert(luredCount >= 4, "four sows should follow the turnips (got " .. luredCount .. ")")

Assert(MoveTo(POS.hesper[1] + 2, POS.hesper[2], POS.hesper[3], 120000), "should be able to walk the sows back to the farm")
-- let the herd catch up
local function HerdIsHere()
	for sow in pairs(lured) do
		if GetDistance(me, sow) > 9 then return false end
	end
	return true
end
if not WaitUntil(HerdIsHere, 45000, "the herd arrives") then
	for sow in pairs(lured) do
		Log("sow " .. sow .. " is " .. tostring(GetDistance(me, sow)) .. " m away, alive " .. tostring(IsAlive(sow)))
	end
	Fail("the four sows should follow the player back to the farm")
end
-- what the client shows runs a little ahead of where the server has them
Sleep(4000)
SendAreaTrigger(AT_PEN)
Sleep(2500)
ExpectRewarded(82, 0, "Pigs in the Corn")

-- ------------------------------------------------------------------------------------------------
-- 83 The Scarecrow Watch, 84 A Token in the Ashes: wave event, loot starts a quest.
-- ------------------------------------------------------------------------------------------------
Port(POS.odell, 2, 0)
local odell = Unit(ODELL, POS.odell, "Farmer Odell Wainbrook")
AcceptQuestFrom(odell, 83)
Sleep(1000)

Port(POS.watchfire, 2, 0)
Assert(CountUnitsByEntry(DUSKLING) == 0, "no dusklings in the fields by day")
UseObject(Object(WATCHFIRE, POS.watchfire, "the field watch-fire"), "the field watch-fire")
Assert(WaitUntil(function() return CountUnitsByEntry(DUSKLING) >= 2 end, 10000, "first wave"),
	"lighting the watch-fire should draw the first dusklings")
local harrow
Assert(WaitUntil(function()
	harrow = FindUnitByEntry(HARROW)
	return harrow ~= nil
end, 60000, "Harrow rises"), "Harrow, the Field-Haunt should rise after the waves")
Kill(harrow)
TakeLoot(harrow, TOKEN, 1, "the Scorched Thorn Token")
KillAll(DUSKLING)
ExpectRewarded(83, 0, "The Scarecrow Watch")

-- The token starts quest 84 when used; the headless client has no quest dialogue, so accept it
-- directly. It is the quest's only requirement and is taken at the turn-in.
GM.AcceptQuest(84)
ExpectRewarded(84, 0, "A Token in the Ashes")
Assert(WaitUntil(function() return GetItemCount(TOKEN) == 0 end, 5000, "token taken"), "the token should be handed in with quest 84")

-- ------------------------------------------------------------------------------------------------
-- 85 Root and Branch: five lures.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(85)
Sleep(500)
for index, lure in ipairs(POS.lures) do
	Port(lure, 1.2, 0)
	UseObject(Object(LURE, lure, "buried dusk-lure " .. index), "buried dusk-lure " .. index)
	Sleep(400)
end
ExpectRewarded(85, 0, "Root and Branch")
KillAll(DUSKLING)

-- ------------------------------------------------------------------------------------------------
-- 86 Cider for Haven: a delivery into the town.
-- ------------------------------------------------------------------------------------------------
Port(POS.hesper, 2, 0)
hesper = Unit(HESPER, POS.hesper, "Goodwife Hesper Wainbrook")
AcceptQuestFrom(hesper, 86)
Assert(WaitUntil(function() return GetItemCount(CIDER) == 1 end, 8000, "cider handed over"), "accepting the quest should hand over the cider cask")
Port(POS.rosalind, 0, -2)
local rosalind = Unit(ROSALIND, POS.rosalind, "Rosalind Tull")
snapshot = Snapshot()
TurnInQuestAt(rosalind, 86)
Assert(WaitUntil(function() return Changed(snapshot) end, 10000, "cider delivered"), "Rosalind Tull should take the cider and pay the carrier")
Log("Quest 86 rewarded: Cider for Haven")

-- ------------------------------------------------------------------------------------------------
-- 87 Lights on the Water: overhear, then take the manifest.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(87)
Sleep(500)
Port(POS.skiff, -1.5, 0)
TakeLoot(Object(SKIFF, POS.skiff, "the smugglers' skiff"), MANIFEST, 1, "the Smuggler's Manifest", true)
ExpectNotYetComplete(87, "the smugglers have not been overheard")

Port(POS.overlook)
SendAreaTrigger(AT_OVERLOOK)
-- the scene takes about fifteen seconds before the credit is given
Sleep(17000)
ExpectRewarded(87, 0, "Lights on the Water")

-- ------------------------------------------------------------------------------------------------
-- 88 Sunk Cost: crates on the river bed.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(88)
Sleep(500)
for index, crate in ipairs(POS.sunken) do
	GM.Worldport(0, crate[1], crate[2] + 1.0, crate[3], 0)
	Sleep(1200)
	TakeLoot(Object(SUNKEN, crate, "sunken crate " .. index), GOODS, 1, "King's Sealed Goods from crate " .. index, true)
end
ExpectRewarded(88, 0, "Sunk Cost")

-- ------------------------------------------------------------------------------------------------
-- 90 The Chandler's Customers: credit from options on a sub page of the traders' dialogue.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(90)
Sleep(500)

Port(POS.tobias, 0, 2.5)
local tobias = Unit(TOBIAS, POS.tobias, "Tobias Wainwright")
-- Hilda's page is not part of Tobias' dialogue: the server must refuse it.
SelectGossipOption(tobias, MENU_HILDA_ACCOUNT, 1)
Sleep(800)
SelectGossipOption(tobias, MENU_TOBIAS_ACCOUNT, 1)
Sleep(800)

Port(POS.wenna, 0, 2.5)
SelectGossipOption(Unit(WENNA, POS.wenna, "Wenna Thistle"), MENU_WENNA_ACCOUNT, 1)
Sleep(800)
ExpectNotYetComplete(90, "Hilda Brandt has not been asked, and her page cannot be reached through Tobias")

Port(POS.hilda, 0, 2.5)
SelectGossipOption(Unit(HILDA, POS.hilda, "Hilda Brandt"), MENU_HILDA_ACCOUNT, 1)
Sleep(800)
ExpectRewarded(90, 0, "The Chandler's Customers")

-- ------------------------------------------------------------------------------------------------
-- 91 A Quiet Word: Tallow bolts when confronted.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(91)
Sleep(500)
Port(POS.tallow, 0, 2.5)
local tallow = Unit(TALLOW, POS.tallow, "Ambrose Tallow")
SelectGossipOption(tallow, MENU_TALLOW, 1)
Assert(WaitUntil(function() return FindUnitByEntry(TALLOW) == nil end, 20000, "Tallow flees"),
	"Ambrose Tallow should run from his stall and vanish once confronted")
ExpectRewarded(91, 0, "A Quiet Word")

-- ------------------------------------------------------------------------------------------------
-- 92 Low Tide at the Basin: the phased fight.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(92)
Sleep(500)
Port(POS.basin, 0, -4)
local foe = Unit(TALLOW_FOE, POS.basin, "Ambrose Tallow at the basin")
local dusklingsBefore = CountUnitsByEntry(DUSKLING)
TargetUnit(foe)
local maxHealth = GetMaxHealth(foe)
-- across 50 % (he rages) and 25 % (he calls what the fields bred)
GM.DamageTarget(math.floor(maxHealth * 0.55))
Sleep(1500)
GM.DamageTarget(math.floor(maxHealth * 0.25))
Assert(WaitUntil(function() return CountUnitsByEntry(DUSKLING) >= dusklingsBefore + 2 end, 10000, "dusklings summoned"),
	"at a quarter of his health Tallow should call two dusklings")
Kill(foe)
TakeLoot(foe, LEDGER, 1, "the Thorn Ledger")
-- his enforcers come running; finish the fight before walking back into town
ClearField()
Sleep(3000)
ClearField()
ExpectRewarded(92, 0, "Low Tide at the Basin")

-- ------------------------------------------------------------------------------------------------
-- 93 Stones Out of Tune: three different objects, one use each.
-- ------------------------------------------------------------------------------------------------
GM.AcceptQuest(93)
Sleep(500)
Port(POS.shardHaven, 1.5, 0)
UseObject(Object(SHARD_HAVEN, POS.shardHaven, "the Haven resonance shard"), "the Haven resonance shard")
Sleep(800)
Port(POS.shardInn, 1.5, 0)
UseObject(Object(SHARD_INN, POS.shardInn, "the Crossroads resonance shard"), "the Crossroads resonance shard")
Sleep(800)
ExpectNotYetComplete(93, "the Briarwatch shard has not been touched")
Port(POS.shardCamp, 1.5, 0)
UseObject(Object(SHARD_CAMP, POS.shardCamp, "the Briarwatch resonance shard"), "the Briarwatch resonance shard")
Sleep(800)
ExpectRewarded(93, 0, "Stones Out of Tune")

-- ------------------------------------------------------------------------------------------------
-- Dusklings roam the Wainbrook fields at night only.
-- ------------------------------------------------------------------------------------------------
Port(POS.watchfire, 6, 0)
KillAll(DUSKLING)
Assert(WaitUntil(function() return CountUnitsByEntry(DUSKLING) == 0 end, 10000, "fields clear by day"), "no dusklings by day")
GM.SetTimeOfDay("23:00", 1)
Assert(WaitUntil(function() return CountUnitsByEntry(DUSKLING) >= 3 end, 15000, "dusklings at night"),
	"dusklings should roam the fields at night")
GM.SetTimeOfDay("12:00", 1)
Assert(WaitUntil(function() return CountUnitsByEntry(DUSKLING) == 0 end, 15000, "dusklings gone at dawn"),
	"the dusklings should be gone by day")

GM.ResetTimeOfDay(1)
GM.Godmode(false)
Log("Haven Road quests played through")
