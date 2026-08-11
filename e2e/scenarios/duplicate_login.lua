-- An account may hold only one live session. Logging in again must displace the session that
-- is already in the world, and the displaced client must be told why rather than just seeing
-- its connection drop.

-- Losing the connection is the point of this scenario, so the harness must not treat it as a
-- failure. This has to be armed before the second login, or the kick would abort the run.
ExpectDisconnect()

Log("Signing in a second time on the same account")
Assert(LoginElsewhere(15000), "the second login should be accepted by the login server")

Assert(WaitUntil(function() return IsDisconnected() end, 15000, "first session is dropped"),
	"the session that was already in the world should be kicked when the account logs in again")

Assert(LastKickReason() == "logged_in_elsewhere",
	"the kick should be reported as logged_in_elsewhere, got: " .. LastKickReason())

Log("The original session was displaced and told why")
