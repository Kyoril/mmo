-- e2e-own-character: ff

-- The server must refuse a damaging spell aimed at a friendly unit.
--
-- Found by the bot swarm: bots were attacking town guards, which stood there taking it without
-- ever entering combat, because they cannot fight back against their own faction. Melee was
-- already refused - GameUnitS::StartAttack checks UnitIsFriendly - but a directly named unit
-- target went through spell validation unexamined, so the cast landed.
--
-- Guard (entry 4) carries faction template 3, "Human Race Npcs", whose faction the player
-- template lists as a friend. Nothing but that list says so: every friendmask and selfmask in
-- this data is zero, so a rule written against the masks alone would call them strangers.

local FIREBALL = 4
local GUARD = 4
local TRAINING_DUMMY = 40

GM.LearnSpell(FIREBALL)
Assert(WaitUntil(function() return HasSpell(FIREBALL) end, 10000, "Fireball learned"),
	"the scenario needs a damaging spell to aim at a friend")

local guard = GM.CreateMonster(GUARD)
Log("Guard spawned: " .. guard)

-- The monster spawns at the caster's exact position, and a target at zero distance fails the
-- in-front check every damage spell makes - which would end the cast for the wrong reason and
-- let this scenario pass without testing anything.
local me = Me()
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, guard) > 5 end, 10000, "stepped away from the guard"),
	"player should be a few units away from the guard")

local guardHealth = GetHealth(guard)
Assert(guardHealth > 0, "the guard should be alive before the cast")

TargetUnit(guard)
CastSpell(FIREBALL, guard)

Assert(WaitUntil(function()
	local result = LastCastResult()
	return result == "ok" or result:find("failed") ~= nil
end, 10000, "cast resolves"), "the cast should resolve one way or the other")

Assert(LastCastResult():find("failed") ~= nil,
	"casting a damaging spell at a friendly guard should be refused, got: " .. LastCastResult())

-- The refusal has to be the point, not a side effect of the guard being out of range or the
-- caster facing the wrong way, so check the guard is untouched as well.
Sleep(1000)
Assert(GetHealth(guard) == guardHealth,
	"the guard should not have lost health (was " .. guardHealth .. ", now " .. GetHealth(guard) .. ")")

Log("Friendly guard refused the spell: " .. LastCastResult())
GM.DestroyMonster(guard)

-- The same spell must still work on something that is a legitimate target, or this scenario
-- would pass just as happily against a server that had stopped casting altogether.
local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "stepped away from the dummy"),
	"player should be a few units away from the dummy")

local dummyHealth = GetHealth(dummy)
TargetUnit(dummy)
Assert(CastSpell(FIREBALL, dummy), "the cast request at a valid target should be accepted")

Assert(WaitUntil(function() return GetHealth(dummy) < dummyHealth end, 10000, "dummy takes damage"),
	"the same spell should still damage a valid target")

Log("Valid target still takes damage")
GM.DestroyMonster(dummy)
