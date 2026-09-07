-- e2e-timeout: 180
-- e2e-own-character: kc

-- Regression: a character kicked in the middle of a cast, who then rejoins, must not take the
-- world server with it.
--
-- Written after the bot swarm crashed the world server with an access violation inside
-- SingleCastState::OnCastFinished: the cast-time countdown fired and reached through a
-- SpellCast& into a caster that had already been destroyed. The world log shows the character
-- leaving and rejoining a second apart, which is what a duplicate-login kick produces.
--
-- HONESTY NOTE: this scenario does NOT reproduce that crash. It was written to, and it passes
-- against the build that crashed. The crash needs the old and new character objects to overlap,
-- and a scripted reconnect is far too slow for that. What this covers is the shape - a cast
-- interrupted by a kick, followed by an immediate rejoin - which was untested before and is
-- worth holding onto either way. The crash itself is still unreproduced; the report is kept at
-- artifacts/crash-reports/swarm-20260907/.

local FIREBALL = 4
local TRAINING_DUMMY = 40

GM.LearnSpell(FIREBALL)
Assert(WaitUntil(function() return HasSpell(FIREBALL) end, 10000, "Fireball learned"),
	"this scenario needs a spell with a cast time to interrupt")

local dummy = GM.CreateMonster(TRAINING_DUMMY)
Log("Training dummy spawned: " .. dummy)

-- The monster spawns at the caster's exact position, and a target at zero distance fails the
-- in-front check every damage spell makes. A rejected cast leaves no countdown to outlive
-- anything, which would make this scenario pass without testing what it claims to.
local me = Me()
GM.Worldport(0, GetPosX(me) + 8, GetPosY(me), GetPosZ(me), 0)
Assert(WaitUntil(function() return GetDistance(me, dummy) > 5 end, 10000, "teleported away from dummy"),
	"player should be a few units away from the dummy")

TargetUnit(dummy)
Log("Starting a cast, then getting displaced before it lands")
Assert(CastSpell(FIREBALL, dummy), "Fireball cast request should be accepted")

-- The kick has to arrive while the cast is still running. Fireball takes 1.5s, so this leaves
-- most of the cast ahead of us - do not wait for it to finish.
Sleep(200)

ExpectDisconnect()
Assert(LoginElsewhere(15000), "the second login should be accepted")

Assert(WaitUntil(function() return IsDisconnected() end, 15000, "first session is dropped"),
	"the casting session should be kicked when the account logs in again")

Log("Displaced mid-cast; rejoining immediately")

-- Immediately, on purpose. The bug needs the new character object to arrive while the old one
-- is still registered under the same guid.
Assert(Reconnect(30000), "the displaced player should be able to log straight back in")

Assert(UnitExists(Me()), "the character should be back in the world after reconnecting")
Assert(GetHealth(Me()) > 0, "the reconnected character should be alive")

-- The crash was the cast-time countdown firing after its caster was gone, so the world has to
-- survive well past the cast time of the spell that was interrupted.
Sleep(3000)

Assert(GetHealth(Me()) > 0, "the world server should still be serving this character")

Log("Survived a kick mid-cast and a reconnect")
