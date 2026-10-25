-- e2e-own-character: hvn
--
-- Haven's day/night routine. The chain under test is:
--
--   GM settime to a full hour -> OnHourOfDay instance trigger "Haven - Day/Night Routine"
--     -> Daytime / Nighttime / Dusk triggers, each gated on the HourOfDay condition
--     -> SetSpawnState with DESPAWN: day folk (town crier, citizens) leave at night, the night
--        watchman, the night patrols and the tipsy patrons leave at dawn, the lamplighter walks at dusk
--   and, at night, the "Night Watchman - Calls" instance triggers make the watchman yell the hour
--   with a voice line (SoundEntry 416 at 22:00).
--
-- The services (binder, bankers, trainers) must be there at any hour.
--
-- The time of day is realm state that outlives this scenario, so it is always reset at the end.
-- The character position outlives it too, and Haven is a thousand units from where the shared
-- character works: this scenario therefore takes its own, so that every later scenario of the
-- same class does not find itself standing in Haven.

local HERALD, CITIZEN, WATCHMAN, LAMPLIGHTER, PATRON = 106, 109, 107, 108, 110
local CALLORAN, BANKER, WARRIOR_TRAINER = 25, 104, 111
local WATCHMAN_CALLS_TEN = 416
local LAMPLIGHTER_LAMPS_LIT = 420

local function Present(entry)
	return FindUnitByEntry(entry) ~= nil
end

local function ExpectPresent(entry, what, timeoutMs)
	Assert(WaitUntil(function() return Present(entry) end, timeoutMs or 8000, what), what .. " should be in Haven")
end

local function ExpectGone(entry, what)
	Assert(WaitUntil(function() return not Present(entry) end, 8000, what .. " gone"), what .. " should have left Haven")
end

-- Haven's upper square, next to the brazier.
GM.Worldport(0, -689.0, 3.0, 1008.0, 0)
Sleep(2000)

-- Noon: market life, no night folk.
GM.SetTimeOfDay("12:00", 1)
ExpectPresent(HERALD, "the town crier")
ExpectPresent(CITIZEN, "a citizen")
ExpectGone(WATCHMAN, "the night watchman")
ExpectGone(PATRON, "the tipsy patrons")
ExpectGone(LAMPLIGHTER, "the lamplighter")
ExpectPresent(CALLORAN, "the binder")
ExpectPresent(BANKER, "the banker")
ExpectPresent(WARRIOR_TRAINER, "the warrior trainer")
Log("Noon: crier and citizens out, night folk away")

-- Dusk: the lamplighter comes out and calls the lamps. That is a Say, heard within 25 m, so wait
-- in the lower court where his round starts.
GM.Worldport(0, -688.0, 0.5, 970.0, 0)
Sleep(1500)
ClearHeardSounds()
GM.SetTimeOfDay("20:00", 1)
ExpectPresent(LAMPLIGHTER, "the lamplighter")
Assert(WaitUntil(function() return SoundCount(LAMPLIGHTER_LAMPS_LIT) >= 1 end, 5000, "lamps lit"),
	"the lamplighter should call out the lamps when he comes out")
ExpectPresent(HERALD, "the town crier at 20:00")
Log("Dusk: lamplighter out")

-- Night: day folk go home, the watch takes over and the watchman calls the hour.
ClearHeardSounds()
GM.SetTimeOfDay("22:00", 1)
ExpectGone(HERALD, "the town crier")
ExpectGone(CITIZEN, "the citizens")
ExpectGone(LAMPLIGHTER, "the lamplighter")
ExpectPresent(WATCHMAN, "the night watchman")
ExpectPresent(PATRON, "a tipsy patron")
Assert(WaitUntil(function() return SoundCount(WATCHMAN_CALLS_TEN) >= 1 end, 5000, "ten o'clock call"),
	"the night watchman should call ten o'clock")
Assert(SoundSource(WATCHMAN_CALLS_TEN) == FindUnitByEntry(WATCHMAN), "the call should come from the watchman himself")
ExpectPresent(CALLORAN, "the binder at night")
ExpectPresent(BANKER, "the banker at night")
Log("Night: watch out, crier and citizens home")

-- Dawn: back to day.
GM.SetTimeOfDay("07:00", 1)
ExpectGone(WATCHMAN, "the night watchman")
ExpectGone(PATRON, "the tipsy patrons")
ExpectPresent(HERALD, "the town crier", 8000)
ExpectPresent(CITIZEN, "a citizen", 8000)
Log("Dawn: day folk back")

GM.ResetTimeOfDay(1)
Log("Haven day/night routine verified")
