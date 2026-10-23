-- e2e-own-character: s
--
-- Regression: a creature whose only victim becomes undetectable must not crash the server.
--
-- CreatureAICombatState::UpdateVictim() drops a victim it can no longer see via
-- RemoveThreat(). When that victim was the last threatener, RemoveThreat() calls
-- ChooseNextAction() re-entrantly, which reaches GetAI().Reset() and destroys the combat
-- state -- while the outer ChooseNextAction() frame is still running on it. The outer frame
-- then re-arms m_nextActionCountdown on freed memory and the world server dies with an
-- access violation in Countdown::Impl::SetEnd.
--
-- Solo play is the dangerous case: the player is the only threatener, so the threat list
-- really does empty. Stealth is how a victim becomes undetectable without dying.
--
-- This scenario takes its own character on purpose. Stealth is a cast-interrupt aura, so
-- it is stripped by an auto-attack swing, and the swing timing depends on character level --
-- sharing the class character with the scenarios that level it up made this flaky.
--
-- Uses: Stealth Test (spell 234, instant self-buff, no class restriction), Fireball
-- (spell 4) and a Forest Bandit (entry 37).

local STEALTH = 234
local FIREBALL = 4
local FOREST_BANDIT = 37

-- Survive the bandit while we set the fight up; the scenario asserts nothing about damage.
GM.Godmode(true)

GM.LearnSpell(STEALTH)
GM.LearnSpell(FIREBALL)
Assert(WaitUntil(function() return HasSpell(STEALTH) and HasSpell(FIREBALL) end, 10000, "spells learned"),
	"player should know Stealth and Fireball after GM.LearnSpell")

local bandit = GM.CreateMonster(FOREST_BANDIT)
Log("Forest Bandit spawned: " .. bandit)

-- The monster spawns on top of us; a spell with an in-front requirement cannot validate at
-- zero distance, so step aside first.
local me = Me()
GM.Worldport(0, GetPosX(me) + 5, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, bandit) > 3 end, 10000, "stepped off the spawn point"),
	"player should be a few units from the bandit")

local banditHealth = GetHealth(bandit)
Assert(banditHealth > 0, "bandit should spawn alive")

-- Put ourselves on the bandit's threat list as its ONLY threatener. A single spell cast is
-- used rather than auto-attack: it does not depend on faction hostility, and it leaves no
-- swing timer running that would strip the stealth aura a moment later.
TargetUnit(bandit)
Assert(CastSpell(FIREBALL, bandit), "Fireball cast request should be accepted")

-- Fireball has a cast time, and the server refuses a second cast while one is in progress.
-- Waiting for the bandit to lose health is not the same as waiting for this cast: anything
-- else already fighting the bandit takes it down too, and the stealth request then went out
-- mid-cast, was refused as "spell in progress" and no aura ever appeared.
Assert(WaitUntil(function() return LastCastResult() == "ok" end, 15000, "fireball cast finishes"),
	"Fireball should finish casting, got: " .. LastCastResult())
Assert(WaitUntil(function() return GetHealth(bandit) < banditHealth end, 15000, "bandit takes the hit"),
	"bandit should take damage, which puts us on its threat list")

-- A dead creature leaves combat through a different path and would not exercise the bug.
Assert(IsAlive(bandit), "bandit must survive the pull for this regression")

-- Go undetectable while still being its only threatener. The Fireball's global cooldown may
-- still have a moment to run and the pulled bandit is already swinging at us, so ask again
-- until the aura sticks rather than betting the regression on a single try.
local stealthed = false
for _ = 1, 10 do
	Assert(CastSpell(STEALTH), "stealth cast request should be accepted")
	if WaitUntil(function() return HasAura(me, STEALTH) end, 1000, "stealth applied") then
		stealthed = true
		break
	end
end
Assert(stealthed, "player should have the stealth aura, last cast result: " .. LastCastResult())

-- Put real distance between us so detection cannot succeed on any level spread
-- (detection range is clamped to 25 units at the very most).
GM.Worldport(0, GetPosX(me) + 40, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, bandit) > 30 end, 10000, "moved out of detection range"),
	"player should be far outside the bandit's detection range")

-- The bandit re-evaluates its victim on its own action timer. Before the fix the world
-- server dies here and the scenario ends as a lost connection.
Sleep(3000)

Assert(not IsDisconnected(), "world server should still be alive after the bandit lost its only victim")

-- Prove the world server is still actually serving, not merely still connected.
local COPPER = 1234
local moneyBefore = GetMoney()
GM.GiveMoney(COPPER)
Assert(WaitUntil(function() return GetMoney() >= moneyBefore + COPPER end, 10000, "world server responds"),
	"world server should still answer requests after the threat list emptied")

Log("World server survived the last threatener going undetectable")

GM.DestroyMonster(bandit)
