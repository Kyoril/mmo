-- e2e-own-character: trg
-- Gliding through the real server: equip Elowen's Prototype, jump from 20 m above the ground next to
-- the Collision map's spawn updraft (area trigger 13), glide through the updraft column and land
-- without fall damage. The server validates every glide packet (speed, rise without lift), so a
-- finished glide with the bot still connected means the bot's glide matched the server's model.

local GLIDER_ITEM = 181
local COLLISION_MAP = 2
-- Updraft 13: a 10 x 60 x 10 box centred at (25, 30, -40), so it spans x 20..30 and z -45..-35.
local START_X, START_Z = 40.0, -40.0
local HEIGHT = 20.0

local me = Me()

GM.AddItem(GLIDER_ITEM, 1)
Assert(WaitUntil(function() return GetItemCount(GLIDER_ITEM) > 0 end, 10000, "glider in the bags"), "GM.AddItem should give the glider")
EquipFromBackpack()
Assert(WaitUntil(function() return GetMaxVigor() > 0 end, 10000, "glider equipped"),
	"equipping the glider should grant a Vigor pool (the glider spell is a base spell)")
Assert(WaitUntil(function() return GetVigor() >= GetMaxVigor() end, 30000, "Vigor full"), "Vigor should be full before the glide")

-- Find the ground next to the updraft: walk there on the nav mesh.
GM.Worldport(COLLISION_MAP, START_X + 2, 5, START_Z, 0)
Assert(MoveTo(START_X, 0, START_Z, 15000), "the bot should reach the start point on the ground")
local ground = GetPosY(me)
Log(string.format("ground next to the updraft at %.2f, Vigor %.1f / %.1f", ground, GetVigor(), GetMaxVigor()))

GM.Heal()
Assert(WaitUntil(function() return GetHealth(me) == GetMaxHealth(me) end, 5000, "full health"), "the bot should be at full health")

GM.Worldport(COLLISION_MAP, START_X, ground + HEIGHT, START_Z, 0)
Sleep(500)

SetLandingHeight(ground)
Assert(Jump(), "the bot should jump")
Sleep(300)
Assert(OpenGlider(), "the glider should open in the air")
SetGlideDirection(-1, 0)
Assert(WaitUntil(function() return IsGliding() end, 1000, "gliding"), "the bot should glide")

-- Into the column (x <= 30) and measure the climb inside it.
Assert(WaitUntil(function() return GetPosX(me) <= 29 end, 5000, "inside the updraft"), "the glider should reach the updraft")
local enterY = GetPosY(me)
Sleep(600)
local insideY = GetPosY(me)
Log(string.format("Vigor in the updraft %.1f / %.1f", GetVigor(), GetMaxVigor()))
Log(string.format("updraft lifted the glider from %.2f to %.2f", enterY, insideY))
Assert(insideY > enterY, string.format("the updraft should lift the glider (%.2f -> %.2f)", enterY, insideY))
Assert(IsGliding(), "the server should not have ended the glide in the updraft")

Assert(WaitUntilLanded(30), "the glider should land")
Sleep(1000)
Log(string.format("landed at %.2f with %d / %d health, Vigor %.1f / %.1f", GetPosY(me), GetHealth(me), GetMaxHealth(me), GetVigor(), GetMaxVigor()))
Assert(GetHealth(me) == GetMaxHealth(me), "a glide landing should not deal fall damage, health " .. tostring(GetHealth(me)) .. " / " .. tostring(GetMaxHealth(me)))
Assert(GetVigor() < GetMaxVigor(), "the glide should have drained Vigor")
