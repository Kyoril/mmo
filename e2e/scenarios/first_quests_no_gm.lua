-- e2e-class: 1
-- e2e-timeout: 900
-- e2e-own-character: fqn
--
-- The first minutes of a new warrior, played like a player and with no GM command at all: walk to the
-- real quest givers, accept at them, fight with auto-attack, rest when hurt, loot, and turn in at the
-- quest enders. Covers quest 1 (The Boar Problem) and quest 10 (Rat Troubles).
--
-- It fails when the character dies or a quest cannot be finished. Both happened before the
-- 2026-11-18 polish: Young Forest Boars stood inside the Forest Bandit camp's aggro range, so the first
-- quest of the game killed level 1 players. Anything that makes the opening area lethal again (spawn
-- density, creature levels, aggro, assist radius) turns this red.
--
-- Own character: it must start at level 1 at the race start position.

local me = Me()
local startTime = os.time()
local function elapsed() return os.time() - startTime end

local HALDOR, HARBIN = 15, 27
local YOUNG_FOREST_BOAR, CELLAR_RAT = 14, 28
local POS = {
	haldor = { 289.9, 5.6, 546.2 },
	harbin = { 265.2, 2.4, 612.7 },
	boars = { 251.0, 6.0, 664.0 },
	rats = { 266.1, -1.8, 600.0 },
}

local kills = 0

local function walkTo(p, label)
	Assert(MoveTo(p[1], p[2], p[3], 90000), "should reach " .. label)
end

local function restIfNeeded()
	if GetHealth(me) < GetMaxHealth(me) * 0.45 then
		DoEmote(4)
		WaitUntil(function() return GetHealth(me) >= GetMaxHealth(me) * 0.95 end, 90000, "rest")
		DoEmote(4)
		Sleep(300)
	end
end

local function findNpc(entry, p)
	walkTo({ p[1] + 1.5, p[2], p[3] + 1.5 }, "npc " .. entry)
	local npc = nil
	WaitUntil(function() npc = FindUnitByEntry(entry); return npc ~= nil end, 10000, "npc " .. entry .. " in sight")
	Assert(npc ~= nil, "npc " .. entry .. " should be in sight")
	if GetDistance(me, npc) > 4 then
		MoveTo(GetPosX(npc), GetPosY(npc), GetPosZ(npc), 15000)
	end
	return npc
end

local function accept(entry, p, questId)
	AcceptQuestFrom(findNpc(entry, p), questId)
	Sleep(800)
end

local function turnIn(entry, p, questId)
	local npc = findNpc(entry, p)
	local xp, level = GetXp(), GetLevel(me)
	TurnInQuestAt(npc, questId, 0)
	Assert(WaitUntil(function() return GetXp() ~= xp or GetLevel(me) ~= level end, 8000, "turn in " .. questId),
		"quest " .. questId .. " should be turned in")
	Log("quest " .. questId .. " turned in at " .. elapsed() .. " s, level " .. GetLevel(me))
end

local function fight(g)
	TargetUnit(g)
	if GetDistance(me, g) > 3.5 then
		MoveTo(GetPosX(g), GetPosY(g), GetPosZ(g), 20000)
	end
	FaceUnit(g)
	StartAttack(g)
	local done = WaitUntil(function()
		if not IsAlive(me) or not UnitExists(g) or not IsAlive(g) then return true end
		if GetDistance(me, g) > 4 then
			MoveTo(GetPosX(g), GetPosY(g), GetPosZ(g), 3000)
			FaceUnit(g)
			StartAttack(g)
		end
		return false
	end, 60000, "fight")
	StopAttack()
	Assert(IsAlive(me), "the character should survive the opening fights (died after " .. kills .. " kills, "
		.. elapsed() .. " s in)")
	if not done then
		return false
	end
	kills = kills + 1
	LootUnit(g)
	Sleep(400)
	for slot = 0, 3 do TakeLootItem(slot); Sleep(100) end
	ReleaseLoot(g)
	return true
end

local function hunt(entry, count, area, label)
	local got, attempts = 0, 0
	while got < count and attempts < count * 4 do
		attempts = attempts + 1
		restIfNeeded()
		local g = FindNearestUnitByEntry(entry, GetPosX(me), GetPosY(me), GetPosZ(me))
		if g == nil or GetDistance(me, g) > 80 then
			walkTo(area, label)
			Sleep(1500)
			g = FindNearestUnitByEntry(entry, area[1], area[2], area[3])
		end
		if g == nil then
			Sleep(3000)
		elseif fight(g) then
			got = got + 1
		end
	end
	Assert(got >= count, label .. ": killed " .. got .. " of " .. count)
end

Assert(GetLevel(me) == 1, "the scenario needs a fresh level 1 character")

accept(HALDOR, POS.haldor, 1)
hunt(YOUNG_FOREST_BOAR, 10, POS.boars, "Young Forest Boars")
restIfNeeded()
turnIn(HALDOR, POS.haldor, 1)

accept(HARBIN, POS.harbin, 10)
hunt(CELLAR_RAT, 5, POS.rats, "Cellar Rats")
restIfNeeded()
turnIn(HARBIN, POS.harbin, 10)

Assert(GetLevel(me) >= 2, "the opening quests should reach level 2, got " .. GetLevel(me))
Log("First quests played without GM help: " .. kills .. " kills, level " .. GetLevel(me) .. ", " .. elapsed() .. " s")
