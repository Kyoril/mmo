-- e2e-own-character: c
--
-- Regression: a unit that leaves the world must not leave a live spell cast behind.
--
-- SingleCastState keeps itself alive (m_selfHold) and holds a raw reference to its caster via
-- SpellCast&. It also connects to its *target's* despawned signal, and that connection is owned
-- by the cast state rather than by the caster. So destroying a creature mid-cast orphaned its
-- cast state: still subscribed to the player, still pointing at a caster that no longer existed.
--
-- The crash landed later and somewhere else entirely -- when the player left the world, the
-- orphaned state ran OnTargetDespawned -> StopCast -> SendEndCast and dereferenced its dead
-- caster in GameObjectS::GetWorldInstance.
--
-- Uses: Choirbound Acolyte (entry 83), whose Dirge of the Hollow Choir (spell 242) has a 2.5s
-- cast time, and Fireball (spell 4) to pull it.

local ACOLYTE = 83
local FIREBALL = 4

-- The acolyte is a level 11 caster; the test character cannot survive it and does not need to.
GM.Godmode(true)

GM.LearnSpell(FIREBALL)
Assert(WaitUntil(function() return HasSpell(FIREBALL) end, 10000, "Fireball learned"),
	"player should know Fireball")

local acolyte = GM.CreateMonster(ACOLYTE)
Log("Choirbound Acolyte spawned: " .. acolyte)

local me = Me()
GM.Worldport(0, GetPosX(me) + 6, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, acolyte) > 4 end, 10000, "stepped off the spawn point"),
	"player should be a few units from the acolyte")

local acolyteHealth = GetHealth(acolyte)
Assert(acolyteHealth > 0, "acolyte should spawn alive")

TargetUnit(acolyte)
Assert(CastSpell(FIREBALL, acolyte), "Fireball cast request should be accepted")
Assert(WaitUntil(function() return GetHealth(acolyte) < acolyteHealth end, 15000, "acolyte takes the hit"),
	"acolyte should take damage and engage")

-- The acolyte opens with Dirge, a 2.5s cast, on its first action tick after aggro. Destroying
-- it now leaves a cast state whose countdown is still running and whose caster is gone.
Sleep(1000)
GM.DestroyMonster(acolyte)
Assert(WaitUntil(function() return not UnitExists(acolyte) end, 10000, "acolyte despawned"),
	"acolyte should be gone")

-- Wait past the point the abandoned cast would have completed. Before the fix the countdown
-- fires here, OnCastFinished dereferences the dead caster and the world server dies.
Sleep(5000)

Assert(not IsDisconnected(), "world server should still be alive after the caster was destroyed mid-cast")

-- Prove the world server is still actually serving, not merely still connected.
local COPPER = 2468
local moneyBefore = GetMoney()
GM.GiveMoney(COPPER)
Assert(WaitUntil(function() return GetMoney() >= moneyBefore + COPPER end, 10000, "world server responds"),
	"world server should still answer requests after the abandoned cast would have finished")

Log("World server survived a caster being destroyed mid-cast")
