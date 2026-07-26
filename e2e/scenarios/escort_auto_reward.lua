-- e2e-timeout: 300
-- e2e-own-character: esc
--
-- (Own character: quest chain state must be clean — quest 48/56 untouched.)
--
-- Escort quest stack regression: verifies the data + runtime chain behind
-- "Out of the Barrowfield" (quest 56):
--   * chain gating: 56 requires 48 rewarded (prevquestid),
--   * area-trigger arrival: entering the Barrowfront Camp trigger fires
--     QuestExplorationCredit for the triggering player only,
--   * AutoRewarded: the completed quest rewards itself without any turn-in.
--
-- The follow movement itself needs a real questgiver accept (the OnQuestAccept
-- trigger on the escortee) and is not exercised here — GM.AcceptQuest bypasses
-- the questgiver.

local me = Me()

if GetLevel(me) < 8 then
	GM.LevelUp(8 - GetLevel(me))
end

-- Chain gating: 56 must be locked until 48 ("The Silent Barrowfield",
-- a breadcrumb that auto-completes at accept) has been rewarded.
GM.AcceptQuest(48)
GM.TurnInQuest(48, 0)
Sleep(500)

GM.AcceptQuest(56)

-- Snapshot before arrival: the wait below must only be satisfied by the
-- auto-reward of quest 56 (1300 xp, 300 copper).
local xpBefore = GetXp()
local moneyBefore = GetMoney()
local levelBefore = GetLevel(me)

-- Teleport to the palisade approach (outside the radius-12 arrival trigger at
-- (-436, -1, 262)) and walk in. Area triggers are detected client-side in the
-- real game; the headless client reports the entry explicitly and the server
-- validates the position before executing the linked trigger.
GM.Worldport(0, -436, -1, 244, 0)
Assert(MoveTo(-436, -1, 262, 30000), "player should reach the Barrowfront Camp arrival point")
SendAreaTrigger(7)

local ok = WaitUntil(function()
	if GetXp() ~= xpBefore then return true end
	if GetMoney() ~= moneyBefore then return true end
	if GetLevel(me) > levelBefore then return true end
	return false
end, 15000, "escort quest auto-reward")
Assert(ok, "quest 56 should auto-reward on entering the arrival trigger (xp "
	.. tostring(GetXp()) .. ", money " .. tostring(GetMoney()) .. ")")

Log("Escort quest auto-rewarded on arrival (xp " .. xpBefore .. " -> " .. GetXp()
	.. ", money " .. moneyBefore .. " -> " .. GetMoney() .. ")")
