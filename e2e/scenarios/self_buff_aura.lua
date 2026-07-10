-- Regression: self-cast buff pipeline — instant cast, aura application
-- (AuraUpdate packets) and mana cost deduction.
--
-- Uses: Frost Armor (spell 6, instant self-buff, costs mana, 30min duration).

local FROST_ARMOR = 6

GM.LearnSpell(FROST_ARMOR)
Assert(WaitUntil(function() return HasSpell(FROST_ARMOR) end, 10000, "Frost Armor learned"),
	"player should know Frost Armor")

local me = Me()
Assert(not HasAura(me, FROST_ARMOR), "player should not have the Frost Armor aura yet")

local manaBefore = GetPower(me)
Assert(manaBefore > 0, "mage should have mana (got " .. tostring(manaBefore) .. ")")

Assert(CastSpell(FROST_ARMOR), "self-cast request should be accepted")

Assert(WaitUntil(function() return LastCastResult() == "ok" end, 10000, "cast succeeds"),
	"Frost Armor cast should succeed, got: " .. LastCastResult())

Assert(WaitUntil(function() return HasAura(me, FROST_ARMOR) end, 10000, "aura applied"),
	"Frost Armor aura should be visible on the player")

Assert(WaitUntil(function() return GetPower(me) < manaBefore end, 10000, "mana deducted"),
	"cast should consume mana (before " .. manaBefore .. ", now " .. tostring(GetPower(me)) .. ")")

Log("Frost Armor applied; mana " .. manaBefore .. " -> " .. GetPower(me))
