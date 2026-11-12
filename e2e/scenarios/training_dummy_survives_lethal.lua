-- e2e-class: 3
-- e2e-own-character: dm
-- e2e-timeout: 120
--
-- Regression: a Training Dummy (creature 40) in combat that takes a lethal hit is refilled
-- WITHOUT dying first. It used to die for a moment before its combat script restored it: every
-- cast or channel aimed at it was interrupted (pets reported "Invalid target."), its debuffs
-- were stripped and death procs fired. Now the hit leaves the dummy alive, its debuffs stay and
-- a Drain Essence channel at it runs to the end.

local CORRUPTING_TOUCH = 24
local DRAIN_ESSENCE, DRAIN_TICK = 1331, 1332
local TRAINING_DUMMY = 40

local me = Me()

GM.LevelUp(5)
Assert(WaitUntil(function() return GetLevel(me) >= 4 end, 15000, "levelled up"),
	"the acolyte should reach level 4 for Drain Essence, is " .. GetLevel(me))

for _, spellId in ipairs({ CORRUPTING_TOUCH, DRAIN_ESSENCE }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "spell " .. spellId .. " learned"),
		"the acolyte should know spell " .. spellId)
end

local dummy = GM.CreateMonster(TRAINING_DUMMY)
GM.Worldport(0, GetPosX(me) + 10, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 8 end, 10000, "stepped away from the dummy"),
	"the character should stand ten yards from the dummy")

--- Casts spellId at the dummy with fresh cooldowns and full mana, and waits for it to succeed.
local function Cast(spellId)
	TargetUnit("0x0")
	GM.ResetCooldowns()
	GM.RestorePower()
	TargetUnit(dummy)
	Assert(CastSpell(spellId, dummy), "cast request for " .. spellId .. " should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "cast " .. spellId .. " resolves"), "the cast of " .. spellId .. " should resolve")
	Assert(LastCastResult() == "ok", "cast " .. spellId .. " should succeed, got: " .. LastCastResult())
end

-- Corrupting Touch puts the dummy in combat (its combat script only exists then) and is the
-- debuff that must survive the lethal hit.
for attempt = 1, 5 do
	Cast(CORRUPTING_TOUCH)
	if WaitUntil(function() return HasAura(dummy, CORRUPTING_TOUCH) end, 3000, "touch lands (try " .. attempt .. ")") then
		break
	end
end
Assert(HasAura(dummy, CORRUPTING_TOUCH), "Corrupting Touch should afflict the dummy")
Assert(WaitUntil(function() return GetHealth(dummy) < GetMaxHealth(dummy) end, 10000, "dummy in combat"),
	"the dummy should take damage and enter combat")

local ticksBefore = SpellGoCount(me, DRAIN_TICK)
Cast(DRAIN_ESSENCE)
Assert(WaitUntil(function() return IsChanneling(me) end, 3000, "drain channels"), "Drain Essence should channel")

-- A hit worth ten times its health, while the channel runs.
GM.DamageTarget(GetMaxHealth(dummy) * 10)
Sleep(500)
Assert(IsAlive(dummy), "the training dummy must not die")
Assert(GetHealth(dummy) >= GetMaxHealth(dummy) * 0.9,
	"the dummy should be refilled (" .. GetHealth(dummy) .. "/" .. GetMaxHealth(dummy) .. ")")
Assert(HasAura(dummy, CORRUPTING_TOUCH), "the lethal hit must not strip the dummy's debuffs")
Assert(IsChanneling(me), "the lethal hit must not end a channel aimed at the dummy")

Assert(WaitUntil(function() return not IsChanneling(me) end, 6000, "drain ends"), "the drain channel should end")
Sleep(300)
local ticks = SpellGoCount(me, DRAIN_TICK) - ticksBefore
Assert(ticks == 3, "Drain Essence should tick three times through the lethal hit, ticked " .. ticks)
Log("The dummy survived a lethal hit with its debuff, and the drain ran " .. ticks .. " ticks")

TargetUnit("0x0")
GM.DestroyMonster(dummy)
