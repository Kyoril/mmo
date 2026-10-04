-- Regression: scheduled realm shutdown, schedule and cancel. The chain under test is:
--
--   client GmShutdown (gm_level 3, handled by the realm, not proxied)
--     -> realm ShutdownManager schedules and announces the exact remaining time
--     -> ShutdownCountdown to every player in the world
--   and the same for a cancel, which announces 0xFFFFFFFF.
--
-- The shutdown is never allowed to run out: it would stop the shared stack under every later
-- scenario. Both delays are far longer than this scenario, and both are cancelled. A failed
-- Assert raises a Lua error (the engine converts its C++ abort into one), so the body runs under
-- pcall: whatever fails, a cleanup cancels a possibly still pending shutdown and then re-raises
-- the original error so the scenario still fails.

local ok, err = pcall(function()
	local announced = GM.ScheduleShutdown(600)
	Assert(announced == 600, "the realm should announce the full delay right away, got " .. tostring(announced))
	Assert(GM.CancelShutdown(), "the realm should announce the cancellation")

	-- Scheduling again works after a cancel, and a reschedule replaces the pending shutdown.
	announced = GM.ScheduleShutdown(1800)
	Assert(announced == 1800, "a second schedule should be announced, got " .. tostring(announced))
	announced = GM.ScheduleShutdown(900)
	Assert(announced == 900, "a reschedule should announce its own delay, got " .. tostring(announced))
	Assert(GM.CancelShutdown(), "the rescheduled shutdown should be cancellable")
end)

if not ok then
	-- Ignore the result: the cancel times out when nothing is pending.
	pcall(GM.CancelShutdown)
	error(err, 0)
end

Log("Realm shutdown verified: schedule, cancel, reschedule, cancel")
