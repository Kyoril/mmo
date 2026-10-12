-- e2e-class: 1
-- e2e-timeout: 180
-- e2e-own-character: tl
--
-- Learning Tailoring through its quest chain, crafting with it, the skill gate and relog.
-- (Own character: the quests and the learned profession persist on the character, so it must
-- not share progression with other scenarios.)

local QUEST_GOOD_CLOTH, QUEST_NEEDLE, QUEST_TEST_PIECE = 66, 67, 68
local SPELL_TAILORING = 2000
local SPELL_BOLT, SPELL_BAG, SPELL_ROBE = 2001, 2003, 2006
local ITEM_LINEN, ITEM_THREAD, ITEM_BOLT, ITEM_BAG, ITEM_POUCH = 68, 155, 156, 157, 162
local SKILL_TAILORING = 1

GM.ClearInventory()
GM.LevelUp(5)

if not HasSpell(SPELL_TAILORING) then
	GM.AcceptQuest(QUEST_GOOD_CLOTH)
	GM.AddItem(ITEM_LINEN, 6)
	Assert(WaitUntil(function() return GetItemCount(ITEM_LINEN) >= 6 end, 5000, "linen granted"), "linen not granted")
	GM.TurnInQuest(QUEST_GOOD_CLOTH)

	GM.AcceptQuest(QUEST_NEEDLE)
	GM.AddItem(ITEM_POUCH, 1)
	Assert(WaitUntil(function() return GetItemCount(ITEM_POUCH) >= 1 end, 5000, "pouch granted"), "pouch not granted")
	GM.TurnInQuest(QUEST_NEEDLE)

	GM.AcceptQuest(QUEST_TEST_PIECE)
	GM.TurnInQuest(QUEST_TEST_PIECE)

	Assert(WaitUntil(function() return HasSpell(SPELL_TAILORING) end, 5000, "tailoring learned"), "Tailoring was not rewarded")
	Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) == 1 end, 5000, "skill 1"),
		"skill should start at 1, got " .. GetTradeSkill(SKILL_TAILORING))
else
	Log("Tailoring already known (re-run), skipping the quest chain")
end

Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) >= 1 end, 5000, "skill known"), "skill not known")
Assert(GetTradeSkillMax(SKILL_TAILORING) == 75, "apprentice cap is 75, got " .. GetTradeSkillMax(SKILL_TAILORING))
Assert(HasSpell(SPELL_BOLT) and HasSpell(SPELL_BAG), "starter recipes learned")

GM.ClearInventory()
GM.ResetCooldowns()
Assert(WaitUntil(function() return GetItemCount(ITEM_LINEN) == 0 end, 5000, "inventory cleared"), "inventory not cleared")

-- Craft a bolt: orange recipe, guaranteed skill-up.
local startSkill = GetTradeSkill(SKILL_TAILORING)
GM.AddItem(ITEM_LINEN, 2)
Assert(WaitUntil(function() return GetItemCount(ITEM_LINEN) == 2 end, 5000, "linen arrives"), "linen not granted")
Assert(CastSpell(SPELL_BOLT), "bolt cast sent")
Assert(WaitUntil(function() return LastCastResult() == "ok" end, 8000, "bolt crafted"), "bolt craft failed: " .. LastCastResult())
Assert(WaitUntil(function() return GetItemCount(ITEM_BOLT) == 1 end, 3000, "bolt in bags"), "bolt not created")
Assert(GetItemCount(ITEM_LINEN) == 0, "linen consumed")
Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) == startSkill + 1 end, 3000, "skill up"),
	"skill did not rise from " .. startSkill .. ", now " .. GetTradeSkill(SKILL_TAILORING))
local skillAfterBolt = GetTradeSkill(SKILL_TAILORING)

-- Craft a bag from a second bolt plus thread.
GM.ResetCooldowns()
GM.AddItem(ITEM_BOLT, 2)
GM.AddItem(ITEM_THREAD, 3)
Assert(WaitUntil(function() return GetItemCount(ITEM_BOLT) == 3 and GetItemCount(ITEM_THREAD) == 3 end, 5000, "bag reagents"), "bag reagents missing")
Assert(CastSpell(SPELL_BAG), "bag cast sent")
Assert(WaitUntil(function() return GetItemCount(ITEM_BAG) == 1 end, 8000, "bag crafted"), "bag not created: " .. LastCastResult())
Assert(GetItemCount(ITEM_BOLT) == 0 and GetItemCount(ITEM_THREAD) == 0, "bag reagents consumed")

-- A trainer recipe above the current skill is refused without consuming anything.
GM.ResetCooldowns()
GM.LearnSpell(SPELL_ROBE)
Assert(WaitUntil(function() return HasSpell(SPELL_ROBE) end, 5000, "robe learned"), "robe not learned")
GM.AddItem(ITEM_BOLT, 4)
GM.AddItem(ITEM_THREAD, 2)
Assert(WaitUntil(function() return GetItemCount(ITEM_BOLT) == 4 and GetItemCount(ITEM_THREAD) == 2 end, 5000, "robe reagents"), "robe reagents missing")
CastSpell(SPELL_ROBE)
Assert(WaitUntil(function() return LastCastResult():find("^failed") ~= nil end, 3000, "robe refused"), "robe should be refused, got " .. LastCastResult())
Assert(LastCastResult() == "failed:21", "expected FailedSkillTooLow, got " .. LastCastResult())
Assert(GetItemCount(ITEM_BOLT) == 4 and GetItemCount(ITEM_THREAD) == 2, "reagents kept")

-- The skill survives a relog (passive unlock spell re-applied on login).
local skillBefore = GetTradeSkill(SKILL_TAILORING)
Assert(skillBefore >= skillAfterBolt, "skill must not drop")
-- Reconnect expects a dropped session: displace this one with a second login first.
ExpectDisconnect()
Assert(LoginElsewhere(15000), "second login")
Assert(WaitUntil(function() return IsDisconnected() end, 15000, "session dropped"), "session should be displaced")
Assert(Reconnect(30000), "reconnect")
Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) == skillBefore end, 8000, "skill after relog"),
	"skill changed on relog: " .. skillBefore .. " -> " .. GetTradeSkill(SKILL_TAILORING))
Assert(GetTradeSkillMax(SKILL_TAILORING) == 75, "cap after relog")
Log("Tailoring verified")
