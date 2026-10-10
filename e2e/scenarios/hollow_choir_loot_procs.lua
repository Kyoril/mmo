-- e2e-own-character: hlp
-- e2e-timeout: 240
--
-- The Coffin Nail Dagger (117) from the Hollow Choir carries an equip proc (273): melee hits
-- eventually drive Coffin Rot (274, a shadow damage over time) into the target. Equip auras are
-- hidden client-side, so the visible Coffin Rot on the target is what proves the chain works.
--
-- The proc chance is 12 % per hit; up to 120 s of swings at a 1.6 s dagger make a miss of every
-- swing vanishingly unlikely.

local DAGGER, COFFIN_ROT = 117, 274
local TRAINING_DUMMY = 40

GM.Godmode(true)
GM.LevelUp(9)
Assert(WaitUntil(function() return GetLevel(Me()) >= 10 end, 10000, "level 10"),
	"the character should reach level 10 for the level-10 items")

GM.AddItem(DAGGER, 1)
Assert(WaitUntil(function() return GetItemCount(DAGGER) == 1 end, 10000, "dagger granted"),
	"the dagger should arrive in the backpack")
EquipFromBackpack()
Sleep(1000)

local me = Me()

local dummy = GM.CreateMonster(TRAINING_DUMMY)
-- The dummy spawns on our position; step aside so it stands in front, inside melee range.
GM.Worldport(0, GetPosX(me) + 2, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 1.5 end, 10000, "stepped aside"),
	"the dummy should stand a little away")
TargetUnit(dummy)
StartAttack(dummy)
-- Re-open the attack whenever the server dropped it; facing it on every poll kept interrupting
-- the swings instead.
Assert(WaitUntil(function()
		if not IsAutoAttacking() then
			FaceUnit(dummy)
			StartAttack(dummy)
		end
		return HasAura(dummy, COFFIN_ROT)
	end, 120000, "coffin rot"),
	"melee hits with the Coffin Nail Dagger should eventually apply Coffin Rot (after "
		.. MeleeSwingCount(dummy) .. " swings)")
Log("Coffin Rot applied by the dagger proc")
StopAttack()
GM.DestroyMonster(dummy)
