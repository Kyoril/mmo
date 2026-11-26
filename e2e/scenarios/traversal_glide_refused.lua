-- e2e-own-character: trr
-- Without a glider the server refuses the glide (ForceStopGlide): the bot closes it again, falls
-- the full height and takes fall damage.

local COLLISION_MAP = 2
local START_X, START_Z = 40.0, -40.0
local HEIGHT = 30.0

local me = Me()

GM.Worldport(COLLISION_MAP, START_X + 2, 5, START_Z, 0)
Assert(MoveTo(START_X, 0, START_Z, 15000), "the bot should reach the start point on the ground")
local ground = GetPosY(me)

GM.Heal()
Assert(WaitUntil(function() return GetHealth(me) == GetMaxHealth(me) end, 5000, "full health"), "the bot should be at full health")

GM.Worldport(COLLISION_MAP, START_X, ground + HEIGHT, START_Z, 0)
Sleep(500)

SetLandingHeight(ground)
Assert(Jump(), "the bot should jump")
Sleep(300)
OpenGlider()
Assert(WaitUntil(function() return not IsGliding() end, 2000, "glide refused"),
	"without a glider the server should end the glide at once")

Assert(WaitUntilLanded(10), "the bot should land")
Sleep(1000)
Log(string.format("landed with %d / %d health", GetHealth(me), GetMaxHealth(me)))
Assert(GetHealth(me) < GetMaxHealth(me), "falling 30 m without a glider should deal fall damage")
