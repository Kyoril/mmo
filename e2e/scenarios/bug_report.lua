-- In-game bug reports and the generic subsystem status, end to end:
--
--   client BugReport (subject, comment, zlib compressed client JSON)
--     -> realm: availability of the node's BUG_REPORT subsystem, per-character rate limit,
--        identity attached, forwarded to the world node
--     -> world: client JSON inflated and sanitized, server snapshot attached, queued for the
--        uploader thread, BugReportResult back to the client
--     -> uploader: the test stack's bug API is file://e2e/runtime/bugs, so the uploaded
--        document lands there as JSON and is read back here.
--
-- Then GM.SetSubsystem switches the subsystem off realm-wide: the realm relays it to the node,
-- the node reports its new status, the realm pushes it to the client, and reports are refused.
-- The subsystem is switched back on at the end so later scenarios on the same stack see it.
--
-- Uses: Chunk of Boar Meat (item 1).

local BOAR_MEAT = 1
local COMMENT = "E2E: the sell price of this item looks wrong."

local function contains(text, fragment)
	return string.find(text, fragment, 1, true) ~= nil
end

Assert(WaitUntil(function() return IsSubsystemAvailable("BUG_REPORT") end, 10000, "BUG_REPORT subsystem announced as available"),
	"the realm should announce BUG_REPORT as available after entering the world")

-- 1. An item report is accepted and uploaded with the server snapshot.
ClearLastBugReportResult()
SubmitBugReport("item", BOAR_MEAT, "0", "Chunk of Boar Meat", COMMENT,
	'{"version":"e2e","os":"e2e-os","evil":"dropped","logTail":"e2e log line"}')
Assert(WaitUntil(function() return LastBugReportResult() ~= "" end, 10000, "bug report result"),
	"the server should answer the bug report")
Assert(LastBugReportResult() == "ACCEPTED", "the report should be accepted, got " .. LastBugReportResult())

local document = ""
Assert(WaitUntil(function()
	document = NewestBugReportFile()
	return contains(document, COMMENT)
end, 10000, "uploaded bug report document"), "the world node should upload the report")

Assert(contains(document, '"schemaVersion":1'), "document carries the schema version")
Assert(contains(document, '"type":"item"'), "document carries the subject type")
Assert(contains(document, '"id":' .. BOAR_MEAT), "document carries the subject id")
Assert(string.find(document, '"characterName":"[^"]+"') ~= nil, "document carries the reporting character")
Assert(string.find(document, '"accountId":"%d+"') ~= nil, "document carries the account the realm vouches for")
Assert(contains(document, '"server":{'), "document carries the server snapshot")
Assert(contains(document, '"combatEvents":'), "server snapshot carries the combat history")
Assert(contains(document, '"subjectState":{'), "server snapshot carries the item state")
Assert(contains(document, '"logTail":"e2e log line"'), "client log tail is kept")
Assert(contains(document, '"os":"e2e-os"'), "known client fields are kept")
Assert(not contains(document, '"evil"'), "unknown client fields are dropped")
Log("Bug report uploaded: " .. #document .. " bytes")

-- 2. A second report right away is rate limited by the realm.
ClearLastBugReportResult()
SubmitBugReport("generic", 0, "0", "", "E2E: second report", '{}')
Assert(WaitUntil(function() return LastBugReportResult() ~= "" end, 10000, "rate limited result"),
	"the realm should answer the second report")
Assert(LastBugReportResult() == "RATE_LIMITED", "the second report should be rate limited, got " .. LastBugReportResult())

-- 3. Switched off at runtime: the client is told, and reports are refused.
local statusPackets = SubsystemStatusCount()
GM.SetSubsystem("BUG_REPORT", false)
Assert(WaitUntil(function() return SubsystemStatusCount() > statusPackets and not IsSubsystemAvailable("BUG_REPORT") end, 10000,
	"BUG_REPORT unavailable"), "switching the subsystem off should reach the client")

ClearLastBugReportResult()
SubmitBugReport("generic", 0, "0", "", "E2E: while disabled", '{}')
Assert(WaitUntil(function() return LastBugReportResult() ~= "" end, 10000, "disabled result"),
	"the realm should answer a report while the subsystem is off")
Assert(LastBugReportResult() == "DISABLED", "reports should be refused while disabled, got " .. LastBugReportResult())

-- 4. Switched back on (also restores the stack for later scenarios).
statusPackets = SubsystemStatusCount()
GM.SetSubsystem("BUG_REPORT", true)
Assert(WaitUntil(function() return SubsystemStatusCount() > statusPackets and IsSubsystemAvailable("BUG_REPORT") end, 10000,
	"BUG_REPORT available again"), "switching the subsystem on should reach the client")

Log("Bug report flow and subsystem status verified")
