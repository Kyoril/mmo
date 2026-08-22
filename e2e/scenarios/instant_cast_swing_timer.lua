-- e2e-own-character: swi
-- e2e-timeout: 180
--
-- Regression: an instant cast must not reset the melee swing timer.
--
-- GameUnitS::OnSpellCastEnded resets the swing when a cast finishes, so that a cast which occupied
-- the attacker is followed by a full swing interval rather than resuming part-way through one. That
-- reset used to run on the instant-cast path too, and instants come off the global cooldown (1500ms)
-- faster than a weapon swings -- so a character pressing instants on cooldown pushed its own swing
-- timer past the horizon on every press and never auto-attacked at all. Creature rotations fire
-- instants on cooldown as a matter of course, so the same held for them.
--
-- Nothing in the suite measured swing cadence before this, which is how the bug survived a green
-- gate: every other combat scenario either takes a single opening swing or walks health down with
-- GM.DamageTarget cheat damage.
--
-- Uses: Training Dummy (entry 40) and Frost Armor (spell 6). The dummy never fights back and cannot
-- die while in combat, and Frost Armor is an instant self-buff that deals no damage.
--
-- What is measured, and why it is not damage: MeleeSwingCount counts swings the server resolved,
-- hit or not. Health cannot stand in for that. A swing can be dodged, and TrainingDummyCombatScript
-- resets the encounter -- healing the dummy to full -- after 5000ms without *taking damage*, while
-- the swing interval here is around 2900ms. So a single dodge opens a gap wide enough to trip the
-- reset, and a health-based assertion then reads the heal instead of the swings. That is what made
-- the first version of this scenario fail roughly one run in six with the swing timer working
-- perfectly: every failing transcript still showed five swings landing on a metronome.

local TRAINING_DUMMY = 40
local FROST_ARMOR = 6

-- One press per iteration, spaced just over the global cooldown so the press is accepted, and under
-- the swing interval so a swing reset would always land before the swing itself could.
local PRESSES = 8
local PRESS_INTERVAL_MS = 1600

-- Swings expected inside the press window at the ~2900ms interval an unarmed level-10 character
-- swings at: 12800 / 2900 = 4. Asserting three leaves a full swing of slack for scheduling while
-- staying far away from the nought a reset-on-instant regression produces.
local MIN_SWINGS_IN_WINDOW = 3

-- Own character, so the mana pool does not depend on what other scenarios levelled the shared mage
-- to. Level 10 is comfortably enough for the presses below.
GM.LevelUp(9)
GM.LearnSpell(FROST_ARMOR)
Assert(HasSpell(FROST_ARMOR), "the test character should know Frost Armor")

local dummy = GM.CreateMonster(TRAINING_DUMMY)

-- The monster spawns on top of us; melee needs the in-front check to pass, so step aside while
-- staying well inside melee reach.
local me = Me()
GM.Worldport(0, GetPosX(me) + 3, GetPosY(me), GetPosZ(me), 0)
FaceUnit(dummy)
TargetUnit(dummy)

Assert(GetHealth(dummy) > 0, "dummy should be alive before the attack")

StartAttack(dummy)

-- Baseline first: melee has to connect at all, or the real assertion below would pass or fail for
-- reasons that have nothing to do with casting.
Assert(WaitUntil(function() return MeleeSwingCount(dummy) > 0 end, 15000, "opening swing lands"),
	"auto-attack should connect before the instant casts start")
Log("Opening swing resolved, dummy at " .. GetHealth(dummy) .. " of " .. GetMaxHealth(dummy))

-- The window under test: keep pressing an instant while auto-attacking.
local swingsBeforeWindow = MeleeSwingCount(dummy)
local healthBeforeWindow = GetHealth(dummy)
local accepted = 0

for i = 1, PRESSES do
	CastSpell(FROST_ARMOR)
	Sleep(200)

	local result = LastCastResult()
	if result == "ok" or result == "started" then
		accepted = accepted + 1
	else
		Log("Frost Armor press " .. i .. " was not accepted: " .. result)
	end

	Sleep(PRESS_INTERVAL_MS - 200)
end

-- If the presses did not actually reach the server there was no swing-reset pressure and a passing
-- check below would mean nothing. Allow one straggler for cast-result timing.
Assert(accepted >= PRESSES - 1,
	"the instant should have been accepted on nearly every press (" .. accepted .. " of " .. PRESSES .. ")")

local swingsDuringWindow = MeleeSwingCount(dummy) - swingsBeforeWindow

Assert(swingsDuringWindow >= MIN_SWINGS_IN_WINDOW,
	"auto-attacks must keep swinging while instants are pressed on cooldown -- only "
		.. swingsDuringWindow .. " swings resolved across " .. accepted .. " presses over "
		.. (PRESSES * PRESS_INTERVAL_MS) .. "ms (expected at least " .. MIN_SWINGS_IN_WINDOW
		.. "), so the casts were resetting the swing timer")

Log("Auto-attacks resolved " .. swingsDuringWindow .. " swings across " .. accepted
	.. " instant casts (" .. (PRESSES * PRESS_INTERVAL_MS) .. "ms window), dummy from "
	.. healthBeforeWindow .. " to " .. GetHealth(dummy) .. " health")

StopAttack()
GM.DestroyMonster(dummy)
