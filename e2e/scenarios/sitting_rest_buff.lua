-- Verifies the sitting rest buff end-to-end: the stand-state trigger casts it, and every
-- documented break condition removes it. Also covers the eat/drink seated rule through the
-- spell path.
--
-- Uses: emote 4 Sit (pose, known by default), spell 247 Resting (trigger-granted, never
-- learned), spell 59 Drink (SitsCaster + CastableWhileSitting).

local SIT = 4
local RESTING = 247
local DRINK = 59

local STAND_STATE_STAND = 0
local STAND_STATE_SIT = 1

local me = Me()

Assert(GetStandState(me) == STAND_STATE_STAND, "player should spawn standing")
Assert(not HasAura(me, RESTING), "player should not be resting while standing")

-- 1. Sitting down grants the buff. The player never learns spell 247 - the trigger casts it
--    server-side, which is why the trigger CastSpell action bypasses the known-spell check.
Assert(not HasSpell(RESTING), "the resting spell must not be a learnable player spell")

DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_SIT end, 10000, "sitting"),
	"stand state should become Sit after the /sit emote")
Assert(WaitUntil(function() return HasAura(me, RESTING) end, 10000, "resting aura applied"),
	"sitting down should grant the resting buff")

-- 2. Moving breaks it (both by the Move interrupt flag and by standing the character up).
Assert(MoveTo(GetPosX(me) + 5, GetPosY(me), GetPosZ(me), 30000), "player should be able to walk away")
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_STAND end, 10000, "stood up by moving"),
	"starting to move should stand the character up")
Assert(WaitUntil(function() return not HasAura(me, RESTING) end, 10000, "resting aura removed"),
	"moving should remove the resting buff")

-- 3. Sitting again grants it again: the trigger is not a one-shot.
DoEmote(SIT)
Assert(WaitUntil(function() return HasAura(me, RESTING) end, 10000, "resting aura re-applied"),
	"sitting down a second time should grant the resting buff again")

-- 4. Standing up voluntarily (the pose emote toggles) removes it via the NotSeated interrupt.
DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_STAND end, 10000, "stood up"),
	"performing /sit again should stand the character up")
Assert(WaitUntil(function() return not HasAura(me, RESTING) end, 10000, "resting aura removed on stand"),
	"standing up should remove the resting buff")

-- 5. Drinking seats the character, so it both applies its own aura and trips the sit trigger.
--    This goes through the spell path rather than item use: the harness has no UseItem
--    binding, and the item path calls the same stand-state helper one line later.
GM.LearnSpell(DRINK)
Assert(WaitUntil(function() return HasSpell(DRINK) end, 10000, "Drink learned"),
	"player should know Drink after GM.LearnSpell")

Assert(CastSpell(DRINK, me), "Drink cast request should be accepted")
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_SIT end, 10000, "seated by drinking"),
	"casting Drink should seat the character")
Assert(WaitUntil(function() return HasAura(me, DRINK) end, 10000, "drink aura applied"),
	"casting Drink should apply the drink aura")
Assert(WaitUntil(function() return HasAura(me, RESTING) end, 10000, "resting aura from drinking"),
	"being seated by Drink should also grant the resting buff")

-- 6. Moving breaks both: the drink aura by its own NotSeated/Move flags, the resting buff by
--    the same rule. This is the seated requirement that was dead data before.
Assert(MoveTo(GetPosX(me) + 5, GetPosY(me), GetPosZ(me), 30000), "player should be able to walk away again")
Assert(WaitUntil(function() return not HasAura(me, DRINK) end, 10000, "drink aura removed"),
	"moving while drinking should remove the drink aura")
Assert(WaitUntil(function() return not HasAura(me, RESTING) end, 10000, "resting aura removed again"),
	"moving should remove the resting buff")

Log("Sitting rest buff passed")
