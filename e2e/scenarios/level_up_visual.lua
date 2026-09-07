-- Regression: the level-up visual is data-driven end to end, with no hard-coded server
-- knowledge of it. The chain under test is:
--
--   GamePlayerS::RewardExperience raises trigger_event::OnPlayerLevelUp
--     -> GamePlayerS::RaiseTrigger picks up triggers flagged PlayerTrigger (players carry no
--        trigger list of their own, so that flag is the whole mechanism)
--     -> the PlaySpellVisual trigger action
--     -> GameUnitS::NotifyPlaySpellVisual broadcasts to everyone in sight
--     -> the client plays SpellVisualization 40 ("Level Up") on the unit.
--
-- A spell visual has no gameplay side effect to observe, so the assertion is on the packet
-- itself. Asserting on the level alone would pass even with the whole trigger path removed.

local LEVEL_UP_VISUALIZATION = 40
local IMPACT_EVENT = 4          -- proto SpellVisualEvent::IMPACT

local me = Me()
local startLevel = GetLevel(me)
Assert(startLevel > 0, "should know our own level before levelling")

-- Any visual from earlier in the session (there should be none) would make the wait below
-- pass without the level-up actually producing one.
ClearLastSpellVisual()
Assert(LastSpellVisualId() == 0, "no spell visual should be pending before the level up")

GM.LevelUp(1)

Assert(WaitUntil(function() return GetLevel(me) > startLevel end, 10000, "level gained"),
	"GM.LevelUp should raise the character's level")

Assert(WaitUntil(function() return LastSpellVisualId() ~= 0 end, 10000, "spell visual arrives"),
	"levelling should produce a PlaySpellVisual packet — check that trigger 51 exists, carries "
	.. "the PlayerTrigger flag and reacts to the On Level Up event")

Assert(LastSpellVisualId() == LEVEL_UP_VISUALIZATION,
	"expected visualization " .. LEVEL_UP_VISUALIZATION .. " but got " .. LastSpellVisualId())

Assert(LastSpellVisualEvent() == IMPACT_EVENT,
	"level-up visual should fire the IMPACT event (the one-shot slot), got " .. LastSpellVisualEvent())

Assert(LastSpellVisualTarget() == me,
	"the visual should play on the levelling character, got guid " .. LastSpellVisualTarget())

Log("Level " .. startLevel .. " -> " .. GetLevel(me) .. " played visualization "
	.. LastSpellVisualId() .. " on " .. LastSpellVisualTarget())

-- Levelling again must fire it again: a trigger that only ran once would mean the event is
-- being consumed somewhere rather than raised per level.
ClearLastSpellVisual()
local secondStart = GetLevel(me)
GM.LevelUp(1)

Assert(WaitUntil(function() return GetLevel(me) > secondStart end, 10000, "second level gained"),
	"the character should level a second time")
Assert(WaitUntil(function() return LastSpellVisualId() == LEVEL_UP_VISUALIZATION end, 10000,
	"second spell visual arrives"),
	"every level up should play the visual, not just the first")
