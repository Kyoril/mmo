-- e2e-own-character: g
--
-- GM observation: invisibility and godmode survive a cross-map port and a relog, and an
-- invisible GM is ignored by a hostile creature standing next to it.
--
-- Uses: Choirbound Acolyte (entry 83, hostile level 11 caster), map 1 (Hollow Choir crypt).

local ACOLYTE = 83
local CRYPT_MAP = 1

-- Spawns a hostile acolyte, steps 6 yards away (well inside aggro range) and checks that the
-- GM is neither attacked nor pulled. Godmode is switched off for the check so that an attack
-- would actually show up as lost health.
local function assertIgnoredByAcolyte(label, mapId)
	local me = Me()
	local acolyte = GM.CreateMonster(ACOLYTE)
	GM.Worldport(mapId, GetPosX(me) + 6, GetPosY(me), GetPosZ(me), 0)
	GM.Godmode(false)
	TargetUnit("0x0")
	GM.Heal()
	Assert(WaitUntil(function() return GetHealth(me) == GetMaxHealth(me) end, 5000, "healed"), label .. ": heal")
	Sleep(6000)
	Assert(GetHealth(me) == GetMaxHealth(me), label .. ": invisible GM must not be attacked")
	GM.Godmode(true)
	GM.DestroyMonster(acolyte)
end

local function assertGodmode(label)
	TargetUnit("0x0")
	local me = Me()
	local health = GetHealth(me)
	GM.DamageTarget(50)
	Sleep(1500)
	Assert(GetHealth(me) == health, label .. ": godmode must absorb damage")
end

GM.Invisible(true)
GM.Godmode(true)
assertGodmode("same map")

-- Cross-map: into the dungeon (a new world instance on the world node).
GM.Worldport(CRYPT_MAP, 4.0, 0.5, -24.0, 0)
assertGodmode("after worldport")
assertIgnoredByAcolyte("after worldport", CRYPT_MAP)

-- Relog: flags come back from the realm database. Reconnect only works on a dropped session, so
-- drop it the way a real player gets dropped: a second login on the same account displaces it.
ExpectDisconnect()
Assert(LoginElsewhere(15000), "second login should be accepted")
Assert(WaitUntil(function() return IsDisconnected() end, 15000, "session displaced"), "first session should be kicked")
Assert(Reconnect(30000), "should log back in")
assertGodmode("after relog")
assertIgnoredByAcolyte("after relog", CRYPT_MAP)

-- Positive control: once visible and mortal, the same acolyte does engage. Without this the
-- checks above would also pass if the acolyte simply never aggroed on anyone.
local me = Me()
local acolyte = GM.CreateMonster(ACOLYTE)
GM.Worldport(CRYPT_MAP, GetPosX(me) + 6, GetPosY(me), GetPosZ(me), 0)
GM.Godmode(false)
GM.Invisible(false)
Assert(WaitUntil(function() return GetHealth(me) < GetMaxHealth(me) end, 15000, "acolyte engages a visible GM"),
	"a visible GM should be attacked by the acolyte")
GM.Invisible(true)
GM.Godmode(true)
GM.DestroyMonster(acolyte)
if not IsAlive(me) then
	GM.Revive()
end

-- Leave the character clean: the flags are persisted per character.
GM.Invisible(false)
GM.Godmode(false)
Log("GM observer state persisted across port and relog")
