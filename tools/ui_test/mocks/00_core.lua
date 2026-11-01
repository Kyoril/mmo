-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Core helpers for the ui_test mocks. The app loads every *.lua in this folder in name order before
-- the TOC, so later files can rely on what earlier ones define:
--   00_core.lua       helpers (this file)
--   01_api_stubs.lua  GENERATED stub for every client API function (returns nil, reported as unmocked)
--   10_*.lua ...      hand-written mocks with fixture data, replacing stubs
--
-- The UiTest table itself is provided by the app: UiTest.FireEvent(name, ...), UiTest.Screenshot(file),
-- UiTest.GetScreenSize(), UiTest.toc, UiTest.locale.

--- Installs a stub for every listed name that does not exist yet. Calling a stub reports the name
--- under "unmockedCalls" in status.json. Counters (GetNum*) return 0 and predicates (Is*, Has*, Can*)
--- return false, so list and visibility code keeps running; everything else returns nil.
function UiTest.StubApi(names)
	for _, name in ipairs(names) do
		if rawget(_G, name) == nil then
			local result = nil
			if name:find("^GetNum") then
				result = 0
			elseif name:find("^Is") or name:find("^Has") or name:find("^Can") then
				result = false
			end

			_G[name] = function()
				UiTest.ReportUnmocked(name)
				return result
			end
		end
	end
end

--- Creates a mock of a C++ object the client exposes as a global (loginConnector, gameData, ...).
--- Fields answer like unit fields do; any other method is reported as "<name>:<Method>" and returns nil.
function UiTest.MockObject(name, fields)
	local object = fields or {}
	return setmetatable(object, {
		__index = function(_, method)
			return function()
				UiTest.ReportUnmocked(name .. ":" .. method)
				return nil
			end
		end
	})
end

--- Fixture data shared by the mocks. Scenarios change it before firing the events that make the UI
--- read it again, e.g. UiTest.data.friends = {} followed by UiTest.FireEvent("FRIEND_LIST_UPDATE").
UiTest.data = UiTest.data or {}

--- Returns a function that always returns the given values (handy for quick mocks).
function UiTest.Returns(...)
	local values = { ... }
	local count = select("#", ...)
	return function()
		return unpack(values, 1, count)
	end
end

--- Shallow copy of a table.
function UiTest.Copy(source)
	local result = {}
	for key, value in pairs(source) do
		result[key] = value
	end
	return result
end

unpack = unpack or table.unpack
