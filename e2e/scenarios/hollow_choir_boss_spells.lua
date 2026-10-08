-- e2e-timeout: 180
--
-- The Hollow Choir boss spells run on three spell mechanics that did not exist before them:
-- ground zones (PersistentAreaAura), summons from a spell (Summon) and frontal cones (ConeEnemy).
-- Unit tests cover their bookkeeping and geometry; this proves they act in a real world:
--
--   * Guttering Candle (253) marks the ground under its target and hits it only after 2 s.
--   * Silent Place (258) keeps pulsing its damage-and-slow spell (259) on whoever stands in it.
--   * Grave Strike (251) lands its cone on the unit in front of the caster.
--   * Last Vigil (252) raises Risen Novices (89) next to the caster, already in the fight.
--   * Dirge of the Grave (262) is a channel; cancelling it must take its caster aura away too,
--     or the periodic damage keeps ticking for the full 5 s (it used to).
--
-- The target is a Training Dummy: it takes damage but never fights back, so nothing but the
-- spells under test moves its health.

local GRAVE_STRIKE = 251
local LAST_VIGIL = 252
local GUTTERING_CANDLE = 253
local SILENT_PLACE = 258
local SILENT_PLACE_PULSE = 259
local DIRGE_OF_THE_GRAVE = 262
local TRAINING_DUMMY = 40
local RISEN_NOVICE = 89

for _, spell in ipairs({ GRAVE_STRIKE, LAST_VIGIL, GUTTERING_CANDLE, SILENT_PLACE, DIRGE_OF_THE_GRAVE }) do
	GM.LearnSpell(spell)
	Assert(WaitUntil(function() return HasSpell(spell) end, 10000, "spell " .. spell .. " learned"),
		"player should know spell " .. spell)
end

local me = Me()
local dummy = GM.CreateMonster(TRAINING_DUMMY)
Log("Training dummy spawned: " .. dummy)

-- The dummy spawns on the player's position; step back so it stands in front, inside melee and
-- cone range (Grave Strike reaches 8 units).
GM.Worldport(0, GetPosX(me) + 4, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 3 end, 10000, "stepped away from the dummy"),
	"player should stand a few units from the dummy")
TargetUnit(dummy)

-- The dummy cannot die: low on health it is topped back up, which would read as a heal in the
-- middle of a measurement. Start every measurement from full health.
local function healDummy()
	GM.Heal()
	Assert(WaitUntil(function() return GetHealth(dummy) == GetMaxHealth(dummy) end, 5000, "dummy healed"),
		"the dummy should be back at full health")
end

-- With an enemy selected GM.ResetCooldowns resets the enemy's cooldowns, so clear the target
-- for it and select the dummy again afterwards.
local function resetMyCooldowns()
	TargetUnit("0x0")
	GM.ResetCooldowns()
	TargetUnit(dummy)
end

local function castAndWait(spell, target)
	Assert(CastSpell(spell, target), "cast request for " .. spell .. " should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "cast " .. spell .. " finished"), "cast " .. spell .. " should finish")
	Assert(LastCastResult() == "ok", "cast " .. spell .. " should succeed, got: " .. LastCastResult())
end

-- Guttering Candle: a warning first, the damage only when the zone runs out.
healDummy()
local before = GetHealth(dummy)
castAndWait(GUTTERING_CANDLE, dummy)
Sleep(1000)
Assert(GetHealth(dummy) == before,
	"Guttering Candle must not hit before its 2 s warning ran out (" .. before .. " -> " .. GetHealth(dummy) .. ")")
Assert(WaitUntil(function() return GetHealth(dummy) < before end, 4000, "candle detonates"),
	"Guttering Candle should hit the dummy standing in it")
Log("Guttering Candle dealt " .. (before - GetHealth(dummy)))

-- Grave Strike: the dummy stands in front, so the cone has to find it.
resetMyCooldowns()
healDummy()
before = GetHealth(dummy)
castAndWait(GRAVE_STRIKE, dummy)
Assert(WaitUntil(function() return GetHealth(dummy) < before - 60 end, 3000, "cone hits"),
	"Grave Strike should hit the dummy in front of the caster (" .. before .. " -> " .. GetHealth(dummy) .. ")")

-- Silent Place: a lingering zone that pulses damage and a slow every second.
resetMyCooldowns()
healDummy()
before = GetHealth(dummy)
castAndWait(SILENT_PLACE, dummy)
Assert(WaitUntil(function() return HasAura(dummy, SILENT_PLACE_PULSE) end, 3000, "silence slows"),
	"Silent Place should put its slow on the dummy")
local afterFirstPulse = GetHealth(dummy)
Assert(afterFirstPulse < before, "Silent Place should damage the dummy")
Assert(WaitUntil(function() return GetHealth(dummy) < afterFirstPulse end, 3000, "silence pulses again"),
	"Silent Place should keep pulsing")

-- Dirge of the Grave: cancel the channel early, its caster aura must go with it.
resetMyCooldowns()
Assert(CastSpell(DIRGE_OF_THE_GRAVE, dummy), "Dirge cast request should be accepted")
Assert(WaitUntil(function() return IsChanneling(me) end, 5000, "dirge channels"),
	"Dirge of the Grave should start a channel")
Assert(WaitUntil(function() return HasAura(me, DIRGE_OF_THE_GRAVE) end, 3000, "dirge aura"),
	"the channel should put its periodic aura on the caster")
CancelCast()
Assert(WaitUntil(function() return not IsChanneling(me) end, 3000, "dirge ends"),
	"cancelling should end the channel")
Assert(WaitUntil(function() return not HasAura(me, DIRGE_OF_THE_GRAVE) end, 3000, "dirge aura gone"),
	"cancelling the channel must remove its caster aura")

-- Candlebearer: its death leaves Spilled Wax on its corpse, which burns whoever stands in it. The
-- corpse casts it (the spell is castable while dead), and the zone must outlive its dead caster.
local CANDLEBEARER = 94
local bearer = GM.CreateMonster(CANDLEBEARER)
TargetUnit(bearer)
GM.KillTarget()
Assert(WaitUntil(function() return not IsAlive(bearer) end, 5000, "candlebearer dies"),
	"the Candlebearer should die")
-- Undo any swing it landed before dying, so only the wax can move our health from here.
TargetUnit("0x0")
GM.Heal()
Assert(WaitUntil(function() return GetHealth(me) == GetMaxHealth(me) end, 5000, "healed"), "should be at full health")
Assert(WaitUntil(function() return GetHealth(me) < GetMaxHealth(me) end, 4000, "wax burns"),
	"standing in the Candlebearer's Spilled Wax should burn")
GM.DestroyMonster(bearer)
GM.Heal()

-- Last Vigil: the novices are hostile to us as they are to the boss's enemies, so stay safe.
GM.Godmode(true)
resetMyCooldowns()
castAndWait(LAST_VIGIL, nil)
Assert(WaitUntil(function() return FindUnitByEntry(RISEN_NOVICE) ~= nil end, 5000, "novices rise"),
	"Last Vigil should raise a Risen Novice")
local novice = FindUnitByEntry(RISEN_NOVICE)
Assert(GetDistance(me, novice) < 12, "the novices should rise next to the caster")

-- Clean up both novices and the dummy, so later scenarios on the shared map 0 do not find them.
for _ = 1, 2 do
	local guid = FindUnitByEntry(RISEN_NOVICE)
	if guid then
		GM.DestroyMonster(guid)
	end
end
GM.DestroyMonster(dummy)
GM.Godmode(false)
