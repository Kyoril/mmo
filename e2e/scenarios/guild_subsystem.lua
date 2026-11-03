-- Guilds as a realm module behind the GUILD subsystem, end to end:
--
--   client GuildInvite / guild chat -> realm GuildModule
--     -> answered with GuildCommandResult while the subsystem is on
--     -> answered with GuildCommandResult Disabled while it is off
--
-- First a guild's life cycle (GM-founded, guild chat, reconnect, disbanded by the leader leaving),
-- then the switch: without a guild an invite is refused with NotInGuild while the subsystem is on,
-- and with Disabled while it is off. GUILD is realm-owned, so GM.SetSubsystem flips it in the realm
-- itself. The guild is disbanded and the subsystem switched back on before the scenario ends, so
-- later scenarios on the same stack see neither.

local RESULT_NOT_IN_GUILD = 1
local RESULT_DISABLED = 7
local NOBODY = "Zzzguildless"

Assert(WaitUntil(function() return IsSubsystemAvailable("GUILD") end, 10000, "GUILD subsystem announced as available"),
	"the realm should announce GUILD as available after entering the world")

local RESULT_PLAYER_NOT_FOUND = 4

-- Invites a character that does not exist and returns the realm's answer (or -1 on timeout):
-- NotInGuild while the character is in no guild, PlayerNotFound once it is in one.
local function ProbeInvite()
	ClearLastGuildCommandResult()
	GuildInvite(NOBODY)
	WaitUntil(function() return LastGuildCommandResult() ~= -1 end, 5000, "guild invite result")
	return LastGuildCommandResult()
end

-- Polls until the probe answers the expected result.
local function ProbeUntil(expected)
	for _ = 1, 10 do
		if ProbeInvite() == expected then
			return true
		end
		Sleep(500)
	end
	return false
end

-- 0. A guild's life cycle through the module: found, chat, survive a reconnect, disband.
-- A run that failed halfway may have left the character leading the guild: disband it first
if ProbeInvite() == RESULT_PLAYER_NOT_FOUND then
	GuildLeave()
end
Assert(ProbeUntil(RESULT_NOT_IN_GUILD), "the character should start in no guild, got " .. LastGuildCommandResult())

-- Guild creation is asynchronous and tells the client nothing directly
GM.CreateGuild("E2E Guild Subsystem")
Assert(ProbeUntil(RESULT_PLAYER_NOT_FOUND), "the character should be in the guild it founded, got " .. LastGuildCommandResult())

local chats = GetGuildChatCount()
SendChat("hello guild", "guild")
Assert(WaitUntil(function() return GetGuildChatCount() > chats end, 10000, "guild chat echo"),
	"guild chat should reach the guild, sender included")
Assert(GetLastGuildChat() == "hello guild", "unexpected guild chat line '" .. GetLastGuildChat() .. "'")

-- Logging out and back in runs the module's left/entered world hooks with the guild in place
-- (Reconnect expects a dropped session: displace this one with a second login first.)
ExpectDisconnect()
Assert(LoginElsewhere(15000), "second login should be accepted")
Assert(WaitUntil(function() return IsDisconnected() end, 15000, "session displaced"), "first session should be kicked")
Assert(Reconnect(30000), "the character should get back into the world")
Assert(WaitUntil(function() return IsSubsystemAvailable("GUILD") end, 10000, "GUILD announced again"),
	"the realm should announce GUILD again after the relog")
Assert(ProbeInvite() == RESULT_PLAYER_NOT_FOUND, "guild membership should survive a reconnect, got " .. LastGuildCommandResult())

-- The leader leaving disbands the guild
GuildLeave()
Assert(ProbeUntil(RESULT_NOT_IN_GUILD), "leaving as the leader should disband the guild, got " .. LastGuildCommandResult())

-- 1. While on, guild requests reach the module.
ClearLastGuildCommandResult()
GuildInvite(NOBODY)
Assert(WaitUntil(function() return LastGuildCommandResult() ~= -1 end, 10000, "guild invite result"),
	"the realm should answer a guild invite")
Assert(LastGuildCommandResult() == RESULT_NOT_IN_GUILD,
	"inviting without being in a guild should report NotInGuild, got " .. LastGuildCommandResult())

-- 2. Switched off at runtime: the client is told, and requests and guild chat are refused.
local statusPackets = SubsystemStatusCount()
GM.SetSubsystem("GUILD", false)
Assert(WaitUntil(function() return SubsystemStatusCount() > statusPackets and not IsSubsystemAvailable("GUILD") end, 10000,
	"GUILD unavailable"), "switching the subsystem off should reach the client")

ClearLastGuildCommandResult()
GuildInvite(NOBODY)
Assert(WaitUntil(function() return LastGuildCommandResult() ~= -1 end, 10000, "disabled invite result"),
	"the realm should answer a guild invite while the subsystem is off")
Assert(LastGuildCommandResult() == RESULT_DISABLED,
	"guild invites should be refused while disabled, got " .. LastGuildCommandResult())

ClearLastGuildCommandResult()
SendChat("hello guild", "guild")
Assert(WaitUntil(function() return LastGuildCommandResult() ~= -1 end, 10000, "disabled guild chat result"),
	"the realm should answer guild chat while the subsystem is off")
Assert(LastGuildCommandResult() == RESULT_DISABLED,
	"guild chat should be refused while disabled, got " .. LastGuildCommandResult())

-- 3. Switched back on (also restores the stack for later scenarios).
statusPackets = SubsystemStatusCount()
GM.SetSubsystem("GUILD", true)
Assert(WaitUntil(function() return SubsystemStatusCount() > statusPackets and IsSubsystemAvailable("GUILD") end, 10000,
	"GUILD available again"), "switching the subsystem on should reach the client")

ClearLastGuildCommandResult()
GuildInvite(NOBODY)
Assert(WaitUntil(function() return LastGuildCommandResult() ~= -1 end, 10000, "guild invite result again"),
	"the realm should answer a guild invite once the subsystem is back on")
Assert(LastGuildCommandResult() == RESULT_NOT_IN_GUILD,
	"guild invites should be processed again once the subsystem is back on, got " .. LastGuildCommandResult())

Log("Guild subsystem verified")
