-- e2e-own-character: mrv
-- e2e-timeout: 300
--
-- Sister Mereth and Cantor Veyr in the Hollow Choir (map 1), driven through their triggers:
--
--   Mereth  * at 65 % two Mourning Choristers rise and Mourning Chorus (257) shields her,
--           * the shield drops when the last chorister dies (trigger 65),
--           * her death opens the G2 seals (trigger 66).
--   Veyr    * at 60 % two Hollow Choristers rise and keep Choral Resonance (265) on him,
--           * his choristers leave once he is dead.
--
-- Their yells carry voice lines (SoundEntry 160-168) the clients play from the speaking boss; Veyr
-- taunts from the apse when Mereth dies.
--
-- Godmode keeps the level-1 test character alive; GM.DamageTarget walks the thresholds.

local MERETH, VEYR = 87, 88
local MOURNING_CHORISTER, HOLLOW_CHORISTER = 90, 91
local MOURNING_CHORUS, CHORAL_RESONANCE = 257, 265
local MERETH_AGGRO, MERETH_VOICES, MERETH_DEATH = 160, 161, 163
local VEYR_MERETH_FALLS, VEYR_AGGRO, VEYR_CHOIR_RISES, VEYR_DEATH = 164, 165, 166, 168

local function AssertVoice(soundId, speaker, label)
	Assert(WaitUntil(function() return SoundSource(soundId) == speaker end, 10000, label),
		label .. ": the yell should play its voice line from the boss (source '" .. SoundSource(soundId) .. "')")
end
-- The stair seals G2_North/South and the arch seals beside the stair (G2_NorthArch/SouthArch) are
-- the magic barrier copy of the seal object (14), all on one line at x -85.5.
local SEAL = 15
local G2_X, G2_Y = -85.5, 5.2
local G2_SEALS = { { "G2_North", -2.85 }, { "G2_South", 2.85 }, { "G2_NorthArch", -9.0 }, { "G2_SouthArch", 9.0 } }

local SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z = 0, 292.267, 5.33, 552.571

local function Pull(entry, label)
	local boss = nil
	Assert(WaitUntil(function() boss = FindUnitByEntry(entry); return boss ~= nil end, 30000, label .. " visible"),
		label .. " should be spawned")
	local maxHealth = GetMaxHealth(boss)
	Assert(WaitUntil(function() return GetHealth(boss) > 0 end, 30000, label .. " health arrives"),
		label .. ": current health never replicated")
	Assert(WaitUntil(function() return GetHealth(boss) >= maxHealth end, 60000, label .. " at full health"),
		label .. " should be at full health before the pull")

	Assert(MoveTo(GetPosX(boss), GetPosY(boss), GetPosZ(boss), 25000) or GetDistance(Me(), boss) <= 5.0,
		label .. ": should be able to walk up")
	TargetUnit(boss)
	FaceUnit(boss)
	StartAttack(boss)
	Assert(WaitUntil(function() return GetHealth(boss) < maxHealth end, 30000, label .. " pulled"),
		label .. ": auto attacks should open the encounter")
	Sleep(6000)
	return boss, maxHealth
end

local function DamageBelow(boss, maxHealth, pct, label)
	TargetUnit(boss)
	local target = math.floor(maxHealth * (pct - 1) / 100)
	local current = GetHealth(boss)
	Assert(current > target, label .. " should still be above " .. pct .. " %")
	GM.DamageTarget(current - target)
end

-- FindUnitByEntry may hand back a corpse, so each kill also removes the corpse: the next lookup
-- then finds the next living one. The death itself has already raised the boss's trigger.
local function KillAll(entry, label)
	for _ = 1, 4 do
		local unit = FindUnitByEntry(entry)
		if not unit then
			return
		end
		if IsAlive(unit) then
			TargetUnit(unit)
			GM.KillTarget()
			Assert(WaitUntil(function() return not IsAlive(unit) end, 10000, label .. " dies"), label .. " should die")
		end
		GM.DestroyMonster(unit)
		WaitUntil(function() return not UnitExists(unit) end, 5000, label .. " corpse removed")
	end
end

GM.Godmode(true)
GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "at the spawn point"),
	"player should start from the default spawn")

-- Mereth, in the crypt below the south-west stair: land in the corridor and walk into the hall.
GM.Worldport(1, -127.0, -2.5, 10.0, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < 100 end, 30000, "arrived in the crypt"),
	"the worldport into the crypt should land")
Sleep(3000)
GM.Godmode(true)

local mereth, merethMax = Pull(MERETH, "Mereth")
AssertVoice(MERETH_AGGRO, mereth, "Mereth aggro voice")
Assert(not HasAura(mereth, MOURNING_CHORUS), "Mereth should not be shielded before 65 %")
DamageBelow(mereth, merethMax, 65, "Mereth")
Assert(WaitUntil(function() return CountUnitsByEntry(MOURNING_CHORISTER) == 2 end, 15000, "choristers rise"),
	"Mourning Voices should raise two choristers")
AssertVoice(MERETH_VOICES, mereth, "Mereth Mourning Voices voice")
Assert(WaitUntil(function() return HasAura(mereth, MOURNING_CHORUS) end, 10000, "chorus shields"),
	"Mourning Chorus should shield Mereth while her choristers live")
KillAll(MOURNING_CHORISTER, "Mourning Chorister")
Assert(WaitUntil(function() return not HasAura(mereth, MOURNING_CHORUS) end, 10000, "chorus broken"),
	"Mourning Chorus should drop with the last chorister")
Log("Mereth's chorus rose and broke")

TargetUnit(mereth)
GM.DamageTarget(GetHealth(mereth) + 100)
Assert(WaitUntil(function() return not IsAlive(mereth) end, 20000, "Mereth dies"), "Mereth's killing blow should land")
StopAttack()
AssertVoice(MERETH_DEATH, mereth, "Mereth death voice")
-- Veyr is out of sight here, so only check that the taunt came from a unit other than Mereth.
Assert(WaitUntil(function() local s = SoundSource(VEYR_MERETH_FALLS); return s ~= "" and s ~= "0x0" and s ~= mereth end,
	10000, "Veyr taunt"), "Veyr's taunt at Mereth's death should play its voice line from Veyr")
local tauntSource = SoundSource(VEYR_MERETH_FALLS)

-- Veyr, in the apse. Landing next to the G2 seals also lets us read their state.
GM.Worldport(1, -98.0, 5.7, 0.0, 0)
Sleep(3000)
GM.Godmode(true)
for _, entry in ipairs(G2_SEALS) do
	local name, z = entry[1], entry[2]
	local seal = nil
	Assert(WaitUntil(function() seal = FindNearestObjectByEntry(SEAL, G2_X, G2_Y, z); return seal ~= nil end,
		20000, name .. " visible"), "the " .. name .. " seal should be spawned")
	Assert(WaitUntil(function() return GetObjectState(seal) == 1 end, 10000, name .. " open"),
		"Mereth's death should open the " .. name .. " seal")
end
Log("G2 is open")

local veyr, veyrMax = Pull(VEYR, "Veyr")
Assert(tauntSource == veyr, "the taunt at Mereth's death should have come from Veyr")
AssertVoice(VEYR_AGGRO, veyr, "Veyr aggro voice")
DamageBelow(veyr, veyrMax, 60, "Veyr")
Assert(WaitUntil(function() return CountUnitsByEntry(HOLLOW_CHORISTER) == 2 end, 15000, "choir rises"),
	"The Choir Rises should raise two Hollow Choristers")
AssertVoice(VEYR_CHOIR_RISES, veyr, "Veyr Choir Rises voice")
Assert(WaitUntil(function() return HasAura(veyr, CHORAL_RESONANCE) end, 10000, "resonance"),
	"the Hollow Choristers should keep Choral Resonance on Veyr")
Log("Veyr's choir rose and resonates")

TargetUnit(veyr)
GM.DamageTarget(GetHealth(veyr) + 100)
Assert(WaitUntil(function() return not IsAlive(veyr) end, 20000, "Veyr dies"), "Veyr's killing blow should land")
StopAttack()
AssertVoice(VEYR_DEATH, veyr, "Veyr death voice")
Assert(WaitUntil(function() return CountUnitsByEntry(HOLLOW_CHORISTER) == 0 end, 15000, "choir leaves"),
	"the Hollow Choristers should not outlive Veyr")

GM.Worldport(SPAWN_MAP, SPAWN_X, SPAWN_Y, SPAWN_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) > 0 end, 30000, "back at the spawn point"),
	"player should be back at the default spawn")
GM.Godmode(false)
Assert(IsAlive(Me()), "the character should survive the scenario")
