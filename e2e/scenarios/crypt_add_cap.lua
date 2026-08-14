-- e2e-own-character: k
-- e2e-timeout: 240
--
-- Regression: the crypt encounter must never let its adds run away.
--
-- Each Choirbound Acolyte raised a Wax-Sealed Husk on a flat timer for as long as it lived,
-- several were alive at once, and nothing counted what was already up: roughly 20 husks a minute
-- against a 120s lifetime, saturating around 40 live adds. Soloing the boss meant fighting a
-- queue rather than an encounter, and every add is also a creature whose threat list can empty,
-- which is what made the AI use-after-free so easy to hit.
--
-- The raise is now gated on LivingCreatureCount(husk) staying under a cap. That gate is what this
-- scenario checks: it is data, it is easy to get wrong, and reading the proto back only proves
-- what was written, not what the encounter does with it.
--
-- The acolytes' raise is also gated on the boss being alive, which the encounter sets when it is
-- pulled. Pulling Sevrin Wax from a scenario turned out to depend on faction, class and level
-- lining up in ways that have nothing to do with the cap, so the instance state is set directly
-- instead. What is under test is the cap, not the pull.

local WAX_SEALED_HUSK = 82
local CHOIRBOUND_ACOLYTE = 83

-- Instance variable 1002 is the encounter's "boss alive" flag (see triggers 29/35/36).
local BOSS_ALIVE_VAR = 1002

-- Must match the cap authored on triggers 32, 33 and 38.
local HUSK_CAP = 6

-- Long enough that the old behaviour would be far past the cap: three acolytes raising every 15s
-- with nothing bounding the total put well over 20 husks up inside two minutes.
local OBSERVE_MS = 100000
local SAMPLE_MS = 2000

GM.Godmode(true)

-- Trigger 38 summons husks at a fixed point mid-nave in the crypt, not next to whichever acolyte
-- raised them. The scenario has to stand there: units the client is never told about cannot be
-- counted, and a test that cannot see the adds would pass no matter how many were spawned.
GM.Worldport(1, -15, 1, 0, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < -10 end, 30000, "arrived in the crypt"),
	"player should be standing where the husks rise")

-- Give the new dungeon instance a moment to finish establishing visibility before spawning into
-- it; GM.CreateMonster waits for the client to be told about the unit.
Sleep(3000)

Assert(CountUnitsByEntry(WAX_SEALED_HUSK) == 0, "no husks should be up before the test")

-- Three acolytes, which is roughly what the encounter itself has out by phase 3.
local acolytes = {}
for i = 1, 3 do
	acolytes[i] = GM.CreateMonster(CHOIRBOUND_ACOLYTE)
end
Assert(WaitUntil(function() return CountUnitsByEntry(CHOIRBOUND_ACOLYTE) >= 3 end, 15000, "acolytes spawned"),
	"three acolytes should be up")

-- Open the gate the acolytes' raise is waiting on.
GM.SetInstanceVariable(BOSS_ALIVE_VAR, 1)

Assert(WaitUntil(function() return CountUnitsByEntry(WAX_SEALED_HUSK) > 0 end, 60000, "first husk raised"),
	"the acolytes should start raising husks once the encounter is marked active")

Log("First husk is up; watching the cap for " .. (OBSERVE_MS / 1000) .. "s")

local peakHusks = 0
local samples = 0
local elapsed = 0

while elapsed < OBSERVE_MS do
	local husks = CountUnitsByEntry(WAX_SEALED_HUSK)
	if husks > peakHusks then peakHusks = husks end
	samples = samples + 1

	-- The cap bounds when a new husk may be requested, so waves firing on the same tick can
	-- legitimately land a little above it. What must not happen is unbounded growth.
	Assert(husks <= HUSK_CAP + 3,
		"live husks ran away: " .. husks .. " up against a cap of " .. HUSK_CAP ..
		" (peak " .. peakHusks .. " over " .. samples .. " samples)")

	Sleep(SAMPLE_MS)
	elapsed = elapsed + SAMPLE_MS
end

-- Guard against passing because nothing ever spawned.
Assert(peakHusks > 0, "no husk was ever raised, so the cap was never actually exercised")

Log("Husk count stayed bounded: peak " .. peakHusks .. " against a cap of " .. HUSK_CAP ..
	" over " .. samples .. " samples")

-- Put the encounter flag back so the adds clean themselves up (trigger 39 despawns them when the
-- boss is not alive), and leave nothing behind for the next scenario.
GM.SetInstanceVariable(BOSS_ALIVE_VAR, 0)
for i = 1, #acolytes do
	GM.DestroyMonster(acolytes[i])
end

Assert(not IsDisconnected(), "world server should still be alive")
