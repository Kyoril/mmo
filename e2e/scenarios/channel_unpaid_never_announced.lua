-- e2e-own-character: chn
-- A channel the caster cannot pay for must never be announced to clients.
--
-- The server used to send ChannelStart before taking the channel's mana, so a channel that
-- could not be paid for went out as ChannelStart followed by a bare SpellFailure. Clients only
-- ever learn that a channel is over from ChannelUpdate(0), so they kept showing it: the
-- nameplate channel bar stayed up and the caster held its channel pose until a client-side
-- fallback timed it out.
--
-- Uses: Fire Barrage (spell 150, mage channel, 1.5s, 45 mana, 10s cooldown, spell level 4),
-- Fireball (spell 4, 30 mana) to drain mana, Training Dummy (creature 40, immortal).
-- Own character: the scenario levels up and must start from a known level.

local FIREBALL = 4
local FIRE_BARRAGE = 150
local FIRE_BARRAGE_COST = 45
local FIREBALL_COST = 30
local TRAINING_DUMMY = 40
local FAILED_NO_POWER = "failed:11"

local me = Me()

-- Fire Barrage needs level 4.
GM.LevelUp(3)
Assert(WaitUntil(function() return GetLevel(me) >= 4 end, 10000, "reached level 4"),
	"character should be at least level 4, is " .. tostring(GetLevel(me)))

GM.LearnSpell(FIREBALL)
GM.LearnSpell(FIRE_BARRAGE)
Assert(WaitUntil(function() return HasSpell(FIREBALL) and HasSpell(FIRE_BARRAGE) end, 10000, "spells learned"),
	"player should know Fireball and Fire Barrage")

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- The monster spawns at the player's position; step aside so facing checks can pass.
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "teleported away from dummy"),
	"player should be a few units away from the dummy after worldport")
TargetUnit(dummy)

-- Positive control: a paid channel is announced and then ended. This proves the client side
-- of the check actually sees ChannelStart / ChannelUpdate(0), so the assertions below cannot
-- pass merely because the packets go unnoticed.
Assert(CastSpell(FIRE_BARRAGE, dummy), "first Fire Barrage request should be accepted")
Assert(WaitUntil(function() return IsChanneling(me) end, 5000, "channel announced"),
	"a paid Fire Barrage should start a channel, last cast result: " .. LastCastResult())
Assert(WaitUntil(function() return not IsChanneling(me) end, 5000, "channel ended"),
	"the channel should end with ChannelUpdate(0) once its 1.5s have run out")
Assert(ChannelStartCount(me) == 1, "expected exactly one ChannelStart, got " .. ChannelStartCount(me))

-- Wait out Fire Barrage's cooldown before draining: once drained, mana must not regenerate past
-- the channel's cost again, and only the five seconds after spending it keep it from doing so.
Sleep(10500)

-- Drain mana with Fireballs until Fire Barrage can no longer be paid for.
local guard = 0
while GetPower(me) >= FIRE_BARRAGE_COST do
	guard = guard + 1
	Assert(guard <= 30, "mana never dropped below " .. FIRE_BARRAGE_COST .. ", still " .. GetPower(me))
	Assert(GetPower(me) >= FIREBALL_COST, "cannot drain further with Fireball")

	Assert(CastSpell(FIREBALL, dummy), "Fireball request should be accepted")
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "Fireball finished"), "Fireball should finish, last result: " .. LastCastResult())
	Assert(LastCastResult() == "ok", "Fireball should succeed while draining, got " .. LastCastResult())

	-- Fireball's cooldown and the global cooldown; a request inside either is refused with a
	-- packet the headless client does not track.
	Sleep(1700)
end
Log("Mana drained to " .. GetPower(me))

-- The unpaid channel: the cast fails for lack of power, and no channel may have been announced.
Assert(CastSpell(FIRE_BARRAGE, dummy), "second Fire Barrage request should be accepted")
Assert(WaitUntil(function() return LastCastResult():find("failed") ~= nil end, 5000, "unpaid cast failed"),
	"Fire Barrage without mana should fail, last result: " .. LastCastResult())
Assert(LastCastResult() == FAILED_NO_POWER, "expected " .. FAILED_NO_POWER .. ", got " .. LastCastResult())

-- Give a stray ChannelStart time to arrive before checking that none did.
Sleep(1000)
Assert(ChannelStartCount(me) == 1,
	"the unpaid channel was announced: " .. ChannelStartCount(me) .. " ChannelStart packets in total")
Assert(not IsChanneling(me), "clients still believe the caster is channeling")

GM.DestroyMonster(dummy)
