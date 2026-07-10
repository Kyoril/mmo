-- Verifies the core spell pipeline end-to-end:
-- learn a spell via GM, spawn a target, cast, and observe the damage.
--
-- Uses: Fireball (spell 4, 1.5s cast, mana) against a Training Dummy (entry 40).

local FIREBALL = 4
local TRAINING_DUMMY = 40

GM.LearnSpell(FIREBALL)
Assert(WaitUntil(function() return HasSpell(FIREBALL) end, 10000, "Fireball learned"),
	"player should know Fireball after GM.LearnSpell")

local dummy = GM.CreateMonster(TRAINING_DUMMY)
Log("Training dummy spawned: " .. dummy)

local startHealth = GetHealth(dummy)
Assert(startHealth > 0, "training dummy should spawn alive (health " .. tostring(startHealth) .. ")")

-- The monster spawns exactly at the player's position; a spell with an
-- in-front requirement can never validate at zero distance. Step aside first.
local me = Me()
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "teleported away from dummy"),
	"player should be a few units away from the dummy after worldport")

TargetUnit(dummy)
Assert(CastSpell(FIREBALL, dummy), "Fireball cast request should be accepted")

Assert(WaitUntil(function()
	local result = LastCastResult()
	return result == "ok" or result:find("failed") ~= nil
end, 10000, "Fireball cast finished"), "cast should finish within 10s")

Assert(LastCastResult() == "ok", "Fireball cast should succeed, got: " .. LastCastResult())

Assert(WaitUntil(function() return GetHealth(dummy) < startHealth end, 10000, "dummy takes damage"),
	"training dummy health should drop after Fireball hit (start " .. startHealth .. ", now " .. tostring(GetHealth(dummy)) .. ")")

Log("Fireball dealt " .. (startHealth - GetHealth(dummy)) .. " damage")

-- Clean up the temporary monster.
GM.DestroyMonster(dummy)
