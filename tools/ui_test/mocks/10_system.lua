-- Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
--
-- Client-wide API: time, console variables, key bindings, subsystems, zone text.

local startTime = os.clock()

--- Milliseconds, like the client's GetAsyncTimeMs.
function GetTime()
	return math.floor((os.clock() - startTime) * 1000) + 1000000
end

UiTest.data.gameTime = { hour = 14, minute = 30 }

function GetGameTime()
	return UiTest.data.gameTime.hour, UiTest.data.gameTime.minute
end

-- Console variables: strings, like the client. Scenarios may set more in UiTest.data.cvars.
UiTest.data.cvars = {
	locale = UiTest.locale,
}

function GetCVar(name)
	return UiTest.data.cvars[name] or ""
end

function SetCVar(name, value)
	UiTest.data.cvars[name] = tostring(value)
end

-- Connection objects the glue screens use. Never connected: the login screen shows its idle state.
loginConnector = UiTest.MockObject("loginConnector", {
	IsConnected = UiTest.Returns(false),
	CancelConnect = function() end,
	GetRealms = function() return function() return nil end end,
})

realmConnector = UiTest.MockObject("realmConnector", {
	IsConnected = UiTest.Returns(false),
	CancelConnect = function() end,
	GetRealmName = UiTest.Returns("Alestia"),
})

-- Key bindings: remembered, so binding UI can list them.
UiTest.data.bindings = {}

function RegisterBinding(name, description, category, handler, defaultKey)
	UiTest.data.bindings[name] = { description = description, category = category, handler = handler, key = defaultKey }
end

function GetKeysForBinding(name)
	local binding = UiTest.data.bindings[name]
	local keys = {}
	if binding and binding.key and binding.key ~= "" then
		keys[1] = binding.key
	end
	return keys
end

-- Every realm subsystem is online unless a scenario lists it here as false.
UiTest.data.subsystems = {}

function IsSubsystemAvailable(name)
	return UiTest.data.subsystems[name] ~= false
end

UiTest.data.zone = { zone = "Oakenshire", subZone = "Market Square" }

function GetZoneText()
	return UiTest.data.zone.zone
end

function GetSubZoneText()
	return UiTest.data.zone.subZone
end

-- Inventory slot ids, same encoding as the client: (bag << 8) | slot.
local BAG_0 = 255
local PACK_START, PACK_END = 23, 39
local BAG_SLOTS_START, BAG_SLOTS_END = 19, 23

function GetBackpackSlot(index)
	if index < 0 or index >= PACK_END - PACK_START then
		return -1
	end
	return BAG_0 * 256 + PACK_START + index
end

function IsBackpackSlot(slot)
	local bag, index = math.floor(slot / 256), slot % 256
	return bag == BAG_0 and index >= PACK_START and index <= PACK_END
end

function GetBagSlot(bagIndex, slot)
	if bagIndex < 1 or bagIndex >= BAG_SLOTS_END - BAG_SLOTS_START then
		return -1
	end
	return (BAG_SLOTS_START + bagIndex - 1) * 256 + slot
end

-- Minimap
UiTest.data.minimapZoom = 0

function GetMinimapZoomLevel()
	return UiTest.data.minimapZoom
end

function GetMinimapMinZoomLevel()
	return 0
end

function GetMinimapMaxZoomLevel()
	return 4
end

function SetMinimapZoomLevel(level)
	UiTest.data.minimapZoom = level
end

function GetMinimapObjectsAt()
	return {}
end

-- World map: no baked map in the harness, so the window shows its "no map" text.
function WorldMapHasMap()
	return false
end

function WorldMapGetMapName()
	return "Development"
end

function WorldMapGetHoverInfo()
	return nil
end

function WorldMapGetPlayerInfo()
	return nil
end

function WorldMapCenterOnPlayer() end
function WorldMapShowWholeMap() end
function WorldMapZoomIn() end
function WorldMapZoomOut() end
