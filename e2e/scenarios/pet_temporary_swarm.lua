-- e2e-own-character: pts
-- e2e-timeout: 180
--
-- Temporary pets: a creature's swarm fights for the creature, a player's swarm for the player.
--
-- Part 1: Ilsa Mourne <Gravecaller> (creature 121) casts Raise Skeletons (spell 300: SummonPet,
-- three Risen Skeletons (creature 120), at most five, ReplaceOldest, no duration) once she is in
-- combat. Her skeletons are owned by her, hostile to the player (they attack it, and a Fireball
-- at one is accepted), and leave the world when she dies.
--
-- Part 2: the player learns Raise Skeletons with GM learnspell (classmask 0, so any class can cast
-- it) and casts it. Its skeletons are owned by the player, friendly to it (a Fireball at one is
-- refused, and none of them attacks it), follow it, and a second cast stays at the cap of five.
--
-- Own character: the player's skeletons have no duration and stay with it.

local RAISE_SKELETONS = 300
local RISEN_SKELETON = 120
local GRAVECALLER = 121
local FIREBALL = 4
local MAX_SKELETONS = 5
local FAILED_BAD_TARGETS = "failed:3" -- spell_cast_result::FailedBadTargets

local me = Me()
local homeX, homeY, homeZ = GetPosX(me), GetPosY(me), GetPosZ(me)

GM.LearnSpell(FIREBALL)
GM.LearnSpell(RAISE_SKELETONS)
Assert(WaitUntil(function() return HasSpell(FIREBALL) and HasSpell(RAISE_SKELETONS) end, 10000, "spells learned"),
	"Fireball and Raise Skeletons should be learned")

-- Waits until a cast request has resolved and returns LastCastResult.
local function CastResult(spellId, target)
	CastSpell(spellId, target)
	Assert(WaitUntil(function()
		local result = LastCastResult()
		return result == "ok" or result:find("failed") ~= nil
	end, 10000, "cast " .. spellId .. " resolves"), "the cast of " .. spellId .. " should resolve")
	return LastCastResult()
end

-- ---------------------------------------------------------------------------------------------
-- Part 1: the Gravecaller's skeletons
-- ---------------------------------------------------------------------------------------------

-- The level 10 caster and her skeletons would kill the test character.
GM.Godmode(true)

local gravecaller = GM.CreateMonster(GRAVECALLER)
Log("Gravecaller spawned: " .. gravecaller)
Assert(GetOwner(gravecaller) == "0x0", "the Gravecaller herself has no owner")

-- Pull her: auto-attack from where she spawned (on top of the character, so in melee range).
TargetUnit(gravecaller)
FaceUnit(gravecaller)
StartAttack(gravecaller)

Assert(WaitUntil(function() return CountUnitsOwnedBy(gravecaller, RISEN_SKELETON) >= 3 end, 20000, "skeletons raised"),
	"the Gravecaller should raise three skeletons once she is in combat (has " ..
	CountUnitsOwnedBy(gravecaller, RISEN_SKELETON) .. ")")
Assert(SpellGoCount(gravecaller, RAISE_SKELETONS) >= 1, "the skeletons should come from her Raise Skeletons cast")

local enemySkeletons = GetOwnedUnits(gravecaller, RISEN_SKELETON)
Assert(#enemySkeletons >= 3 and #enemySkeletons <= MAX_SKELETONS,
	"the Gravecaller should have three to five skeletons, has " .. #enemySkeletons)
for _, skeleton in ipairs(enemySkeletons) do
	Assert(GetOwner(skeleton) == gravecaller, "each skeleton should be owned by the Gravecaller")
end

-- Hostile: they join their owner's fight against the player.
Assert(WaitUntil(function()
	for _, skeleton in ipairs(enemySkeletons) do
		if UnitExists(skeleton) and GetTarget(skeleton) == me then return true end
	end
	return false
end, 15000, "a skeleton attacks the player"), "the Gravecaller's skeletons should attack the player")

-- Hostile to the server's friendliness check too: a damaging spell at one is not refused as a
-- spell at a friend. Step aside first - a target at zero distance fails the in-front check.
GM.Worldport(0, homeX + 8, homeY, homeZ, 0)
Assert(WaitUntil(function() return GetDistance(me, enemySkeletons[1]) > 3 end, 10000, "stepped aside"),
	"the character should step away from the skeleton")
GM.ResetCooldowns()
local enemyCast = CastResult(FIREBALL, enemySkeletons[1])
Assert(enemyCast == "ok", "a Fireball at the Gravecaller's skeleton should be accepted, got " .. enemyCast)

-- When she dies, her skeletons go with her.
StopAttack()
TargetUnit(gravecaller)
GM.KillTarget()
Assert(WaitUntil(function() return not IsAlive(gravecaller) end, 10000, "Gravecaller dies"),
	"the Gravecaller should die")
-- Gone, not merely dead: a despawned skeleton leaves no corpse behind.
Assert(WaitUntil(function()
	for _, skeleton in ipairs(enemySkeletons) do
		if UnitExists(skeleton) then return false end
	end
	return true
end, 15000, "skeletons gone"), "the Gravecaller's skeletons should leave the world when she dies")
Assert(CountUnitsOwnedBy(gravecaller, RISEN_SKELETON) == 0, "no living skeleton of the Gravecaller should remain")
Log("The Gravecaller's skeletons attacked the player and despawned with her")

TargetUnit("0x0")
GM.DestroyMonster(gravecaller)

-- ---------------------------------------------------------------------------------------------
-- Part 2: the player's own skeletons
-- ---------------------------------------------------------------------------------------------

GM.Godmode(false)
GM.Heal()
Assert(WaitUntil(function() return (GetUnitFlags(me) & 0x1) == 0 end, 20000, "out of combat"),
	"the character should leave combat")

GM.Worldport(0, homeX, homeY, homeZ, 0)
GM.ResetCooldowns()
GM.RestorePower()
local raiseCast = CastResult(RAISE_SKELETONS, me)
Assert(raiseCast == "ok", "the player should be able to cast Raise Skeletons, got " .. raiseCast)

Assert(WaitUntil(function() return CountUnitsOwnedBy(me, RISEN_SKELETON) == 3 end, 10000, "own skeletons raised"),
	"the player should raise three skeletons (has " .. CountUnitsOwnedBy(me, RISEN_SKELETON) .. ")")
local ownSkeletons = GetOwnedUnits(me, RISEN_SKELETON)
for _, skeleton in ipairs(ownSkeletons) do
	Assert(GetOwner(skeleton) == me, "each skeleton should be owned by the player")
	Assert(GetLevel(skeleton) == GetLevel(me), "Raise Skeletons uses the character level (pet_level_source CharacterLevel)")
end

-- Friendly: the undead skeletons keep their own (hostile) faction template, but act with their
-- owner's side. None of them turns on the player...
local health = GetHealth(me)
Sleep(3000)
for _, skeleton in ipairs(ownSkeletons) do
	Assert(GetTarget(skeleton) ~= me, "the player's skeleton should not attack its owner")
end
Assert(GetHealth(me) >= health, "the player's skeletons should not damage their owner")

-- ...and the server refuses a damaging spell at one as a spell at a friend.
GM.Worldport(0, homeX + 8, homeY, homeZ, 0)
Assert(WaitUntil(function() return GetDistance(me, ownSkeletons[1]) > 3 end, 10000, "stepped away"),
	"the character should step away from its skeletons")
GM.ResetCooldowns()
local friendlyCast = CastResult(FIREBALL, ownSkeletons[1])
Assert(friendlyCast == FAILED_BAD_TARGETS,
	"a Fireball at the player's own skeleton should be refused, got " .. friendlyCast)
Assert(UnitExists(ownSkeletons[1]) and IsAlive(ownSkeletons[1]), "the refused Fireball should not hurt the skeleton")

-- They follow their owner.
local destX = homeX + 15
Assert(MoveTo(destX, homeY, homeZ, 30000), "the player should walk 15 m")
Assert(WaitUntil(function()
	for _, skeleton in ipairs(ownSkeletons) do
		if GetDistance(me, skeleton) > 6 then return false end
	end
	return true
end, 20000, "skeletons follow"), "the player's skeletons should follow it")

-- A second cast stays at the cap: 3 + 3 is one too many, so the oldest skeleton is replaced and
-- five remain, two of them from the first cast.
GM.ResetCooldowns()
GM.RestorePower()
local secondCast = CastResult(RAISE_SKELETONS, me)
Assert(secondCast == "ok", "the second Raise Skeletons should be cast, got " .. secondCast)
Assert(WaitUntil(function()
	local survivors = 0
	for _, skeleton in ipairs(ownSkeletons) do
		-- The replaced skeleton leaves the world.
		if UnitExists(skeleton) then survivors = survivors + 1 end
	end
	return CountUnitsOwnedBy(me, RISEN_SKELETON) == MAX_SKELETONS and survivors == 2
end, 10000, "capped at five"), "a second cast should keep five skeletons, replacing the oldest (has " ..
	CountUnitsOwnedBy(me, RISEN_SKELETON) .. ")")
Sleep(1500)
Assert(CountUnitsOwnedBy(me, RISEN_SKELETON) == MAX_SKELETONS, "the swarm should stay at five")

Assert(IsAlive(me), "the character should still be alive")
Log("The player's skeletons are friendly, follow it and stay at the cap of five")
