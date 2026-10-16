-- Hourly town bells. The chain under test is:
--
--   GM settime to a full hour (realm TimeOfDayManager -> world node -> WorldInstance::SetTimeOfDay)
--     -> HourChime sees the first minute of an hour -> OnHourOfDay instance trigger of map 0
--     -> PlaySound action: SoundEntry 261 at the town square, once per hour on a 12-hour clock,
--        every 3.5 s, to the players inside the radius at the moment of each strike.
--
-- Counting strikes needs SoundCount, not SoundSource: the whole point of the feature is that the
-- number of PlaySoundById packets matches the hour.
--
-- The time of day is realm state that outlives this scenario, so it is always reset at the end.

local BELL_SOUND = 261
local STRIKE_INTERVAL_MS = 3500

-- Oakenshire town square (a guard spawn); the trigger rings from (295, 5, 570) with radius 220.
local function GoToOakenshire()
	GM.Worldport(0, 271.6, 1.5, 562.0, 0)
	Sleep(1000)
end

-- Well outside both Oakenshire's and Haven's bell radius.
local function GoFarAway()
	GM.Worldport(0, -296.0, -2.0, 442.0, 0)
	Sleep(1000)
end

GoToOakenshire()

-- 14:00 strikes twice, with the interval between the two.
ClearHeardSounds()
GM.SetTimeOfDay("14:00", 1)
Assert(WaitUntil(function() return SoundCount(BELL_SOUND) >= 1 end, 3000, "first strike"),
	"setting the time to 14:00 should strike the bell right away")
Assert(SoundCount(BELL_SOUND) == 1, "the second strike must wait for the interval, heard " .. SoundCount(BELL_SOUND))
Assert(WaitUntil(function() return SoundCount(BELL_SOUND) >= 2 end, STRIKE_INTERVAL_MS + 2000, "second strike"),
	"14:00 should strike a second time")
Sleep(STRIKE_INTERVAL_MS + 1500)
Assert(SoundCount(BELL_SOUND) == 2, "14:00 should strike exactly twice, heard " .. SoundCount(BELL_SOUND))
Assert(SoundSource(BELL_SOUND) == "0x0", "the bell is a positional sound, not a unit's")
Log("14:00 struck twice")

-- Not a full hour: silence.
ClearHeardSounds()
GM.SetTimeOfDay("14:30", 1)
Sleep(STRIKE_INTERVAL_MS + 1000)
Assert(SoundCount(BELL_SOUND) == 0, "14:30 is not a full hour, heard " .. SoundCount(BELL_SOUND))

-- Outside the radius nobody hears it.
GoFarAway()
ClearHeardSounds()
GM.SetTimeOfDay("13:00", 1)
Sleep(STRIKE_INTERVAL_MS)
Assert(SoundCount(BELL_SOUND) == 0, "a player outside the town should not hear the bell, heard " .. SoundCount(BELL_SOUND))

-- A player walking in while the bell is striking hears the remaining strikes, not the missed one.
-- 03:00 strikes three times; the first one rings while we are still far away.
GM.SetTimeOfDay("14:30", 1)
ClearHeardSounds()
GM.SetTimeOfDay("03:00", 1)
GoToOakenshire()
Sleep(3 * STRIKE_INTERVAL_MS + 1000)
local lateStrikes = SoundCount(BELL_SOUND)
Assert(lateStrikes >= 1 and lateStrikes <= 2,
	"a late arrival should hear the remaining strikes of 03:00 but not the first, heard " .. lateStrikes)
Log("Late arrival heard " .. lateStrikes .. " of 3 strikes")

GM.ResetTimeOfDay(1)
Log("Hourly bells verified")
