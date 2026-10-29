-- Regression: data-driven attributes. Attribute points earned by levelling are spent on an
-- attribute by id, which raises that attribute's replicated value and lowers the unspent points.
-- Attribute ids are game data (data/editor/data/attributes.data); 0 is Stamina in the shipped data.

local me = Me()
local stamina = 0

-- Wait for the level itself: the attribute fields of the level up arrive with it, and reading
-- stamina before that would race the level up's own stamina gain.
local levelBefore = GetLevel(me)
GM.LevelUp(3)
Assert(WaitUntil(function() return GetLevel(me) == levelBefore + 3 end, 10000, "level rises"),
	"level should rise by 3 from " .. levelBefore .. ", got " .. tostring(GetLevel(me)))
Sleep(500)
Assert(GetAvailableAttributePoints() > 0,
	"levelling up should grant attribute points, have " .. tostring(GetAvailableAttributePoints()))

local pointsBefore = GetAvailableAttributePoints()
local staminaBefore = GetAttribute(me, stamina)
Assert(staminaBefore > 0, "stamina should be replicated, got " .. tostring(staminaBefore))

AddAttributePoint(stamina)
Assert(WaitUntil(function() return GetAttribute(me, stamina) == staminaBefore + 1 end, 10000, "stamina rises"),
	"spending a point should raise stamina from " .. staminaBefore .. ", got " .. tostring(GetAttribute(me, stamina)))
Assert(GetAvailableAttributePoints() < pointsBefore,
	"spending a point should lower the unspent points from " .. pointsBefore .. ", got " .. tostring(GetAvailableAttributePoints()))

-- Attribute slot 9 exists on the wire but no attribute is defined there: the server must ignore it
local pointsAfterSpend = GetAvailableAttributePoints()
AddAttributePoint(9)
Sleep(1000)
Assert(GetAvailableAttributePoints() == pointsAfterSpend,
	"points spent on an undefined attribute must be rejected, unspent went " .. pointsAfterSpend .. " -> " .. tostring(GetAvailableAttributePoints()))

-- Spending is saved per attribute id (character_class_attributes) together with the attribute
-- layout it was made under, and must come back unchanged after logging in again.
local staminaSpent = GetAttribute(me, stamina)
local pointsSpent = GetAvailableAttributePoints()
-- Reconnect expects a dropped session: displace this one with a second login first.
ExpectDisconnect()
Assert(LoginElsewhere(15000), "second login should be accepted")
Assert(WaitUntil(function() return IsDisconnected() end, 15000, "session displaced"), "first session should be kicked")
Assert(Reconnect(30000), "should log back into the world")
me = Me()
Assert(WaitUntil(function() return GetAttribute(me, stamina) == staminaSpent end, 10000, "stamina persisted"),
	"spent stamina should survive a relog: " .. staminaSpent .. ", got " .. tostring(GetAttribute(me, stamina)))
Assert(GetAvailableAttributePoints() == pointsSpent,
	"unspent points should survive a relog: " .. pointsSpent .. ", got " .. tostring(GetAvailableAttributePoints()))

Log("Attribute points verified: stamina " .. staminaBefore .. " -> " .. GetAttribute(me, stamina)
	.. ", unspent " .. pointsBefore .. " -> " .. GetAvailableAttributePoints())
