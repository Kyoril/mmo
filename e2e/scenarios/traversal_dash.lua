-- e2e-own-character: trd
-- Dash (traversal sprint): learning it grants a full Vigor pool, casting it raises the run speed by
-- 35 % and drains Vigor, and entering combat (Fireball on a Training Dummy) ends it.
--
-- The server replicates Vigor only on transitions, so the drain shows once Dash has ended: the
-- removal settles the pool and writes what is left.

local DASH = 1437
local FIREBALL = 4
local TRAINING_DUMMY = 40
local DASH_SPEED_BONUS = 1.35

local me = Me()

GM.LearnSpell(DASH)
GM.LearnSpell(FIREBALL)
Assert(WaitUntil(function() return HasSpell(DASH) and HasSpell(FIREBALL) end, 10000, "Dash and Fireball learned"),
	"the character should know Dash and Fireball after GM.LearnSpell")

Assert(WaitUntil(function() return GetMaxVigor() > 0 end, 10000, "Vigor pool granted"),
	"knowing Dash should grant a Vigor pool, max is " .. tostring(GetMaxVigor()))
local maxVigor = GetMaxVigor()
Assert(WaitUntil(function() return GetVigor() >= maxVigor end, 30000, "Vigor full"),
	"Vigor should be full before dashing, it is " .. tostring(GetVigor()) .. " / " .. tostring(maxVigor))

local baseSpeed = GetRunSpeed(me)
Log(string.format("Vigor %.1f / %.1f, run speed %.2f", GetVigor(), maxVigor, baseSpeed))
Assert(baseSpeed > 0, "the character should have a run speed, it has " .. tostring(baseSpeed))

Assert(CastSpell(DASH), "the Dash cast request should be accepted")
Assert(WaitUntil(function() return HasAura(me, DASH) end, 5000, "Dash aura applied"),
	"Dash should apply its aura, last cast result: " .. LastCastResult())

local expected = baseSpeed * DASH_SPEED_BONUS
Assert(WaitUntil(function() return math.abs(GetRunSpeed(me) - expected) < 0.05 end, 5000, "run speed raised"),
	string.format("Dash should raise the run speed to %.2f, it is %.2f", expected, GetRunSpeed(me)))
Log(string.format("Dashing at %.2f", GetRunSpeed(me)))

-- Let Dash drain a measurable amount (12 Vigor per second).
Sleep(2000)
Assert(HasAura(me, DASH), "Dash should still be active after two seconds of a full pool")

-- Step aside: the dummy spawns at the player's position and Fireball needs it in front.
local dummy = GM.CreateMonster(TRAINING_DUMMY)
Assert(dummy ~= nil, "the training dummy should spawn")
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "stepped away from the dummy"),
	"the character should stand a few yards from the dummy")

TargetUnit(dummy)
Assert(CastSpell(FIREBALL, dummy), "the Fireball cast request should be accepted")
Assert(WaitUntil(function() return not HasAura(me, DASH) end, 10000, "Dash removed by combat"),
	"attacking the dummy should end Dash, last cast result: " .. LastCastResult())

Assert(WaitUntil(function() return math.abs(GetRunSpeed(me) - baseSpeed) < 0.05 end, 5000, "run speed restored"),
	string.format("the run speed should return to %.2f, it is %.2f", baseSpeed, GetRunSpeed(me)))

local vigorLeft = GetVigor()
Log(string.format("Vigor after Dash: %.1f / %.1f", vigorLeft, maxVigor))
Assert(vigorLeft < maxVigor - 10, string.format("Dash should have drained Vigor, %.1f of %.1f left", vigorLeft, maxVigor))
Assert(vigorLeft > 0, "two seconds of Dash should not empty the pool")

Assert(WaitUntil(function()
	local result = LastCastResult()
	return result ~= "pending" and result ~= "started"
end, 10000, "Fireball finished"), "the Fireball cast should finish")

TargetUnit("0x0")
GM.DestroyMonster(dummy)
