-- e2e-class: 1
-- e2e-own-character: dw
-- e2e-timeout: 240
--
-- Dual Wield (spell 181) is a level-1 base spell of the Warrior and the Scout only (classmask
-- Warrior | Scout). A class switch keeps it between those two classes, a Mage loses it (its
-- off-hand weapon stays equipped but inert), and switching back restores it.
--
-- Observed through the off-hand auto-attack: the server flags off-hand swings with
-- hit_info::LeftSwing, which OffhandSwingCount counts. A Mage can still use daggers, so its main
-- hand keeps swinging; only the off hand must fall silent.
--
-- Uses: Worn Dagger (item 77, one-handed, usable by all three classes), the class-change spells
-- Warrior (229), Mage (230) and Scout (232) (10 s casts, not in combat), Strike (8, Warrior
-- only), Wounding Strike (76, Scout only), Frost Armor (6, Mage only) and the Training Dummy
-- (creature 40), which never fights back. Own character, as it changes class.

local DUAL_WIELD = 181
local WORN_DAGGER = 77
local BECOME_WARRIOR = 229
local BECOME_MAGE = 230
local BECOME_SCOUT = 232
local STRIKE = 8
local WOUNDING_STRIKE = 76
local FROST_ARMOR = 6
local TRAINING_DUMMY = 40
local OFFHAND_SLOT = 16
local IN_COMBAT = 0x1

local me = Me()
local homeX, homeY, homeZ = GetPosX(me), GetPosY(me), GetPosZ(me)

Assert(WaitUntil(function() return HasSpell(DUAL_WIELD) end, 10000, "warrior knows Dual Wield"),
	"a fresh warrior should know Dual Wield as a level-1 class spell")

-- Two daggers: one in the main hand, one in the off hand. Clearing the backpack first and again
-- after the main-hand equip (which swaps out any starting weapon) leaves the second dagger in the
-- first backpack slot.
GM.ClearInventory()
GM.AddItem(WORN_DAGGER, 1)
Assert(WaitUntil(function() return GetItemCount(WORN_DAGGER) == 1 end, 10000, "first dagger arrives"),
	"the first dagger should arrive")
EquipFromBackpack()
Sleep(500)
GM.ClearInventory()
GM.AddItem(WORN_DAGGER, 1)
Assert(WaitUntil(function() return GetItemCount(WORN_DAGGER) == 2 end, 10000, "second dagger arrives"),
	"the second dagger should arrive while the first stays equipped")
EquipBackpackItemTo(0, OFFHAND_SLOT)
Sleep(500)

for _, spellId in ipairs({ BECOME_WARRIOR, BECOME_MAGE, BECOME_SCOUT }) do
	GM.LearnSpell(spellId)
	Assert(WaitUntil(function() return HasSpell(spellId) end, 10000, "class change " .. spellId .. " learned"),
		"class-change spell " .. spellId .. " should be learned")
end

-- Attacks a fresh dummy until the main hand has swung `mainSwings` times, returns the off-hand
-- swing count, and destroys the dummy again so the character can leave combat.
local function SwingAtDummy(mainSwings, what)
	-- The dummy spawns on top of the character, so spawn it at home and step 3 m aside each time.
	GM.Worldport(0, homeX, homeY, homeZ, 0)
	local dummy = GM.CreateMonster(TRAINING_DUMMY)
	GM.Worldport(0, homeX + 3, homeY, homeZ, 0)
	FaceUnit(dummy)
	TargetUnit(dummy)
	StartAttack(dummy)

	Assert(WaitUntil(function() return MeleeSwingCount(dummy) >= mainSwings end, 30000, what .. ": swings"),
		what .. ": auto-attack should keep swinging (" .. MeleeSwingCount(dummy) .. " swings)")
	local offhandSwings = OffhandSwingCount(dummy)
	Log(what .. ": " .. MeleeSwingCount(dummy) .. " swings, " .. offhandSwings .. " with the off hand")

	StopAttack()
	TargetUnit("0x0")
	GM.DestroyMonster(dummy)
	return offhandSwings
end

-- Class-change spells cannot be cast in combat. Destroying the dummy ends it.
local function SwitchClass(changeSpell, classSpell, className)
	Assert(WaitUntil(function() return (GetUnitFlags(me) & IN_COMBAT) == 0 end, 15000, "out of combat"),
		"the character should leave combat before switching to " .. className)
	GM.ResetCooldowns()
	Assert(CastSpell(changeSpell, me), "switch to " .. className .. " should be accepted")
	Assert(WaitUntil(function() return HasSpell(classSpell) end, 25000, "now a " .. className),
		"switching to " .. className .. " should grant its class spell " .. classSpell)
end

Assert(SwingAtDummy(6, "warrior") > 0, "a warrior with two daggers should swing the off hand")

SwitchClass(BECOME_SCOUT, WOUNDING_STRIKE, "Scout")
Assert(HasSpell(DUAL_WIELD), "a scout should keep Dual Wield")
Assert(not HasSpell(STRIKE), "a scout should not know the warrior's Strike")
Assert(SwingAtDummy(6, "scout") > 0, "a scout with two daggers should swing the off hand")

SwitchClass(BECOME_MAGE, FROST_ARMOR, "Mage")
Assert(WaitUntil(function() return not HasSpell(DUAL_WIELD) end, 10000, "mage loses Dual Wield"),
	"a mage should not know Dual Wield")
Assert(SwingAtDummy(6, "mage") == 0, "a mage must not swing the off-hand dagger")

SwitchClass(BECOME_WARRIOR, STRIKE, "Warrior")
Assert(HasSpell(DUAL_WIELD), "switching back to warrior should restore Dual Wield")
Assert(SwingAtDummy(6, "warrior again") > 0, "the off hand should swing again once back as a warrior")

GM.Worldport(0, homeX, homeY, homeZ, 0)
Assert(IsAlive(me), "the character should still be alive")
Log("Dual Wield follows the Warrior and Scout across class switches")
