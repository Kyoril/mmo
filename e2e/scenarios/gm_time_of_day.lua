-- Regression: the realm-wide time of day. The chain under test is:
--
--   client CheatSetTimeOfDay (GM level >= 1, handled by the realm, not proxied)
--     -> realm TimeOfDayManager stores the time as an offset to its system clock
--     -> realm_world_packet::TimeOfDay to every world node
--     -> WorldInstanceManager retimes every hosted instance
--     -> GameTimeInfo with a non-zero transition to every player, so clients blend the sky.
--
-- The time of day is realm state that outlives this scenario, so it is always reset at the
-- end: every later scenario on the same stack would otherwise run at 03:00.

local function startsWith(text, prefix)
	return string.sub(text, 1, #prefix) == prefix
end

-- The received time is stamped when the world node broadcasts it, a few milliseconds after the
-- change, so only the minute is exact.
local received = GM.SetTimeOfDay("03:00")
Assert(startsWith(received, "03:00:0"), "world node should report the new time of day, got " .. received)

-- A second change with a custom transition length: the transition travels with the packet.
received = GM.SetTimeOfDay("22:15", 2)
Assert(startsWith(received, "22:15:0"), "world node should report the second time of day, got " .. received)

-- Reset goes back to the realm's UTC system clock. Which time that is depends on when the
-- suite runs, so only assert that the override is gone.
received = GM.ResetTimeOfDay(1)
Assert(not startsWith(received, "22:15:0"), "reset should leave the overridden time of day, got " .. received)

Log("Time of day verified: set, changed with custom transition, reset to system time " .. received)
