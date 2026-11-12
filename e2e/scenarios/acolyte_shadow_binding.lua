-- e2e-class: 3
-- e2e-own-character: sb
-- e2e-timeout: 300
--
-- The acolyte's shadow binding quest line in the cellar of the Oakenshire Inn:
-- 75 "Beneath the Common Room" (Malrik Duskwright -> Old Harbin), 76 "Loose Tongues" (Old Harbin ->
-- Malrik, 5 Bandit Mages), 77 "A Duskling of Your Own" (Malrik -> Malrik).
--
-- Quests are accepted and turned in at the real NPCs (AcceptQuestFrom / TurnInQuestAt), so the
-- scenario proves the provider and ender wiring and that Malrik stands where a player can reach
-- him. Turning in 77 raises OnQuestReward on Malrik: he casts Bind Duskling (301) on the character,
-- which binds the Duskling and teaches Summon Duskling and Dismiss Duskling. Bind Duskling itself
-- is never learned.

local MALRIK, OLD_HARBIN, BANDIT_MAGE = 13, 27, 39
local BENEATH_THE_COMMON_ROOM, LOOSE_TONGUES, A_SHADE_OF_YOUR_OWN = 75, 76, 77
local BIND_SHADOW, SUMMON_SHADOW, DISMISS_SHADOW = 301, 302, 303
local SHADE_PET = 1

-- Next to Malrik in the inn cellar, and next to Old Harbin behind his bar.
local CELLAR = { 263.4, -1.6, 594.6 }
local COMMON_ROOM = { 265.2, 2.6, 611.4 }

local me = Me()
GM.Godmode(true)

-- The chain starts at level 3.
GM.LevelUp(2)
Assert(WaitUntil(function() return GetLevel(me) >= 3 end, 15000, "level 3"), "the character should reach level 3")

-- A retried attempt finds the duskling of the first one.
GM.PetRemove(SHADE_PET)
Assert(not HasSpell(BIND_SHADOW), "Bind Duskling is not a spell the character learns")

local function GoTo(spot, what)
	GM.Worldport(0, spot[1], spot[2], spot[3], 0)
	Log("At " .. what)
end

local function Npc(entry, what)
	local guid = nil
	Assert(WaitUntil(function()
		guid = FindUnitByEntry(entry)
		return guid ~= nil
	end, 10000, what .. " visible"), what .. " should stand here")
	Assert(GetDistance(me, guid) < 5, what .. " should be within reach (" .. GetDistance(me, guid) .. " m)")
	return guid
end

--- Turns `questId` in at `npc` and waits for the reward: `check` returns true once it arrived.
local function TurnIn(npc, questId, check, what)
	TurnInQuestAt(npc, questId)
	Assert(WaitUntil(check, 15000, what), "quest " .. questId .. " should be rewarded: " .. what)
	Log("Quest " .. questId .. " turned in")
end

-- 75: Malrik in the cellar sends the character up to Old Harbin.
GoTo(CELLAR, "the inn cellar")
local malrik = Npc(MALRIK, "Malrik Duskwright")
AcceptQuestFrom(malrik, BENEATH_THE_COMMON_ROOM)

GoTo(COMMON_ROOM, "the common room")
local harbin = Npc(OLD_HARBIN, "Old Harbin")
local xp = GetXp()
TurnIn(harbin, BENEATH_THE_COMMON_ROOM, function() return GetXp() ~= xp or GetLevel(me) > 3 end, "75 grants xp")

-- 76: Harbin asks for the Bandit Mages who asked about the cellar.
AcceptQuestFrom(harbin, LOOSE_TONGUES)
Sleep(500)
for i = 1, 5 do
	local mage = GM.CreateMonster(BANDIT_MAGE)
	Assert(mage ~= nil, "Bandit Mage " .. i .. " should spawn")
	TargetUnit(mage)
	-- Damage, not GM.KillTarget: Old Harbin attacks a bandit spawned in his common room at once,
	-- and a creature mostly worn down by NPCs grants no kill credit. A GM kill deals no damage of
	-- its own, so Harbin's first swing made the kill his.
	GM.DamageTarget(GetMaxHealth(mage) * 10)
	Assert(WaitUntil(function() return not IsAlive(mage) end, 15000, "mage " .. i .. " dies"),
		"Bandit Mage " .. i .. " should die")
	GM.DestroyMonster(mage)
end
TargetUnit("0x0")

GoTo(CELLAR, "the inn cellar")
malrik = Npc(MALRIK, "Malrik Duskwright")
local money = GetMoney()
TurnIn(malrik, LOOSE_TONGUES, function() return GetMoney() >= money + 200 end, "76 pays 200 copper")

-- 77: the binding. Malrik casts Bind Duskling on the character when it is turned in.
AcceptQuestFrom(malrik, A_SHADE_OF_YOUR_OWN)
Sleep(500)
TurnIn(malrik, A_SHADE_OF_YOUR_OWN, function() return SpellGoCount(malrik, BIND_SHADOW) >= 1 end,
	"Malrik casts Bind Duskling")
Assert(WaitUntil(function() return HasSpell(SUMMON_SHADOW) and HasSpell(DISMISS_SHADOW) end, 10000, "spells taught"),
	"Bind Duskling should teach the character Summon Duskling and Dismiss Duskling")
Assert(not HasSpell(BIND_SHADOW), "the character must not learn Bind Duskling")
Assert(GetPet() == nil, "binding stores the duskling without summoning it")

-- The realm creates the stored pet asynchronously; the summon succeeds once it exists.
for attempt = 1, 6 do
	GM.ResetCooldowns()
	GM.RestorePower()
	CastSpell(SUMMON_SHADOW, me)
	if WaitUntil(function() return GetPet() ~= nil end, 5000, "duskling appears") then
		break
	end
end
Assert(GetPet() ~= nil, "Summon Duskling should call the bound duskling")
Assert(GetPetEntry() == SHADE_PET, "the summoned pet should be the Duskling")
Assert(GetOwner(GetPet()) == me, "the duskling should belong to the character")

-- Leave no pet behind for a rerun.
PetDismiss()
GM.PetRemove(SHADE_PET)
GM.Godmode(false)
