-- The friend list as a realm module behind the FRIENDS subsystem, end to end:
--
--   client FriendListRequest / FriendInvite -> realm FriendModule
--     -> answered with FriendListUpdate / FriendCommandResult while the subsystem is on
--     -> answered with FriendCommandResult Disabled (and no list) while it is off
--
-- FRIENDS is realm-owned, so GM.SetSubsystem flips it in the realm itself (no world node round
-- trip) and the realm pushes the change to every client in the world. The subsystem is switched
-- back on at the end so later scenarios on the same stack see it.

local RESULT_PLAYER_NOT_FOUND = 4
local RESULT_DISABLED = 8
local NOBODY = "Zzzfriendless"

Assert(WaitUntil(function() return IsSubsystemAvailable("FRIENDS") end, 10000, "FRIENDS subsystem announced as available"),
	"the realm should announce FRIENDS as available after entering the world")

-- 1. While on, the friend list is served and requests are processed.
local lists = FriendListUpdateCount()
RequestFriendList()
Assert(WaitUntil(function() return FriendListUpdateCount() > lists end, 10000, "friend list"),
	"the realm should send the friend list on request")

ClearLastFriendCommandResult()
FriendInvite(NOBODY)
Assert(WaitUntil(function() return LastFriendCommandResult() ~= -1 end, 10000, "friend invite result"),
	"the realm should answer a friend invite")
Assert(LastFriendCommandResult() == RESULT_PLAYER_NOT_FOUND,
	"inviting an unknown character should report it as not found, got " .. LastFriendCommandResult())

-- 2. Switched off at runtime: the client is told, and every request is refused.
local statusPackets = SubsystemStatusCount()
GM.SetSubsystem("FRIENDS", false)
Assert(WaitUntil(function() return SubsystemStatusCount() > statusPackets and not IsSubsystemAvailable("FRIENDS") end, 10000,
	"FRIENDS unavailable"), "switching the subsystem off should reach the client")

ClearLastFriendCommandResult()
FriendInvite(NOBODY)
Assert(WaitUntil(function() return LastFriendCommandResult() ~= -1 end, 10000, "disabled invite result"),
	"the realm should answer a friend invite while the subsystem is off")
Assert(LastFriendCommandResult() == RESULT_DISABLED,
	"friend invites should be refused while disabled, got " .. LastFriendCommandResult())

lists = FriendListUpdateCount()
ClearLastFriendCommandResult()
RequestFriendList()
Assert(WaitUntil(function() return LastFriendCommandResult() ~= -1 end, 10000, "disabled list result"),
	"the realm should answer a list request while the subsystem is off")
Assert(LastFriendCommandResult() == RESULT_DISABLED,
	"list requests should be refused while disabled, got " .. LastFriendCommandResult())
Assert(FriendListUpdateCount() == lists, "no friend list should be sent while disabled")

-- 3. Switched back on (also restores the stack for later scenarios).
statusPackets = SubsystemStatusCount()
GM.SetSubsystem("FRIENDS", true)
Assert(WaitUntil(function() return SubsystemStatusCount() > statusPackets and IsSubsystemAvailable("FRIENDS") end, 10000,
	"FRIENDS available again"), "switching the subsystem on should reach the client")

lists = FriendListUpdateCount()
RequestFriendList()
Assert(WaitUntil(function() return FriendListUpdateCount() > lists end, 10000, "friend list again"),
	"the realm should serve the friend list again once the subsystem is back on")

Log("Friends subsystem verified")
