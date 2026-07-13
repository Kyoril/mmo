-- Verifies the emote system end-to-end: one-shot emotes, pose emotes driving the
-- replicated stand state, /pose variant cycling (with unlock via GM), mood emotes,
-- and standing up when movement starts.
--
-- Uses the baseline emote catalog (data/editor/data/emotes.data):
--   1 Wave (one-shot, default), 4 Sit (pose, default), 7 Happy (mood, default),
--   11 Cross-Legged Sit (sit pose variant, must be unlocked).

local WAVE = 1
local SIT = 4
local HAPPY = 7
local SIT_VARIANT = 11

local STAND_STATE_STAND = 0
local STAND_STATE_SIT = 1

local me = Me()

Assert(GetStandState(me) == STAND_STATE_STAND, "player should spawn standing")

-- A default-known one-shot emote is accepted (a rejected packet would leave no trace,
-- but a malformed one would disconnect the session and fail the scenario).
DoEmote(WAVE)
Log("Waved")

-- Sitting replicates through the StandState field.
DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_SIT end, 10000, "sitting"),
	"stand state should become Sit after the /sit emote")

-- Without any unlocked sit variants, /pose has nothing to cycle to.
CyclePose()
Sleep(1000)
Assert(GetSitPoseEmote(me) == 0, "sit pose should stay default while no variant is unlocked")

-- Unlock the sit pose variant and cycle to it.
GM.LearnEmote(SIT_VARIANT)
Sleep(500)
CyclePose()
Assert(WaitUntil(function() return GetSitPoseEmote(me) == SIT_VARIANT end, 10000, "sit variant selected"),
	"/pose should select the unlocked sit pose variant")

-- Cycling again wraps back to the default pose.
CyclePose()
Assert(WaitUntil(function() return GetSitPoseEmote(me) == 0 end, 10000, "sit pose wrapped"),
	"/pose should wrap back to the default sit pose")

-- Performing the same pose emote again stands the character back up (toggle).
DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_STAND end, 10000, "stood up"),
	"performing /sit again should stand the character up")

-- Movement while seated stands the character up server-side.
DoEmote(SIT)
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_SIT end, 10000, "sitting again"),
	"stand state should become Sit again")

Assert(MoveTo(GetPosX(me) + 5, GetPosY(me), GetPosZ(me), 30000), "player should be able to walk away")
Assert(WaitUntil(function() return GetStandState(me) == STAND_STATE_STAND end, 10000, "stood up by moving"),
	"starting to move should stand the character up")

-- Mood emotes replicate through the MoodEmote field and toggle off.
DoEmote(HAPPY)
Assert(WaitUntil(function() return GetMoodEmote(me) == HAPPY end, 10000, "mood set"),
	"mood emote id should replicate after /happy")

DoEmote(HAPPY)
Assert(WaitUntil(function() return GetMoodEmote(me) == 0 end, 10000, "mood cleared"),
	"performing the active mood again should clear it")

Log("Emote system smoke test passed")
