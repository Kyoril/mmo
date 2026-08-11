-- e2e-timeout: 180

-- An account may hold only one live session. Logging in again must displace the session that
-- is already in the world, and the displaced client must be told why rather than just seeing
-- its connection drop.
--
-- The scenario then logs back in over the same connector objects, which is what a real player
-- does after being kicked. That half is not decoration: a connector carries session state, and
-- a stale packet cipher left behind by the first session turned the second session's first
-- packet into garbage -- a crash that shipped because this scenario used to stop at the kick.

-- Losing the connection is the point of this scenario, so the harness must not treat it as a
-- failure. This has to be armed before the second login, or the kick would abort the run.
ExpectDisconnect()

Log("Signing in a second time on the same account")
Assert(LoginElsewhere(15000), "the second login should be accepted by the login server")

Assert(WaitUntil(function() return IsDisconnected() end, 15000, "first session is dropped"),
	"the session that was already in the world should be kicked when the account logs in again")

Assert(LastKickReason() == "logged_in_elsewhere",
	"the kick should be reported as logged_in_elsewhere, got: " .. LastKickReason())

Log("Displaced session was told why; logging back in")

-- Reuses the same connectors, so anything the previous session left installed on them is still
-- there. A stale cipher shows up as a malformed packet the instant the realm answers, and the
-- session never authenticates -- so failing to get back into the world is the assertion.
Assert(Reconnect(30000), "the displaced player should be able to log straight back in")

Assert(UnitExists(Me()), "the character should be back in the world after reconnecting")

-- Proves the link is genuinely usable rather than merely established: this needs a round trip
-- that the server answers.
local health = GetHealth(Me())
Assert(health > 0, "the reconnected session should report a live character, got health " .. health)

Log("Reconnected and playable after being displaced")
