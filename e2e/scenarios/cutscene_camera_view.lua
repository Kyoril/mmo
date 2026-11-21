-- Cutscenes: the server side of a camera flight.
--
-- The chain under test is:
--
--   GM "cutscene 1" -> world node Player::StartCutscene -> CutsceneStart to the client, the character
--     gets the InCutscene unit flag
--   client CutsceneBegin -> the player's second point of view (RemoteView) starts to follow the camera
--     path of the cutscene, tile by tile
--   client CutsceneEnd(skipped) -> the view is taken away, the flag is cleared, CutsceneEnd comes back
--
-- Cutscene 1 is the human intro: its camera starts above the harbour north of Haven, some 900 m away
-- from Oakenshire where the character stands, and reaches the harbourmaster on the quay within its
-- first shot. So while the cutscene runs, the client must be sent a creature it could never see from
-- where its character is - and must keep everything around the character, which is not despawned
-- while the camera is away.
--
-- A real client plays the cutscene; this one only answers the packets. By default it would skip a
-- cutscene on the spot, as every bot does, so that is switched off first.

local CUTSCENE = 1
local IN_CUTSCENE = 0x40

local FARMER_HALDOR = 15      -- stands a few metres from the character in Oakenshire
local HARBOURMASTER = 143     -- Lysa Fairwind on the Haven quay, under the camera of the first shot

local REASON_SKIPPED = 1
local REASON_CANCELLED = 2

local function InCutscene()
	return (GetUnitFlags(Me()) & IN_CUTSCENE) ~= 0
end

SetCutsceneAutoSkip(false)

-- The human starting point in Oakenshire
GM.Worldport(0, 288.3, 5.3, 552.6, 0)
Sleep(1500)

Assert(FindUnitByEntry(FARMER_HALDOR) ~= nil, "Farmer Haldor should stand next to the starting point")
Assert(FindUnitByEntry(HARBOURMASTER) == nil, "the Haven harbourmaster must not be visible from Oakenshire")
Assert(not InCutscene(), "the character should not start out in a cutscene")

-- 1) The server announces the cutscene and flags the character
local starts = CutsceneStartCount()
GM.PlayCutscene(CUTSCENE)
Assert(WaitUntil(function() return CutsceneStartCount() == starts + 1 end, 5000, "CutsceneStart"),
	"the server should announce the cutscene")
Assert(LastCutsceneId() == CUTSCENE, "announced cutscene " .. LastCutsceneId() .. ", expected " .. CUTSCENE)
Assert(WaitUntil(InCutscene, 5000, "InCutscene flag"), "the character should be flagged as being in a cutscene")

-- Until the client says that it plays, the point of view stays with the character
Sleep(1500)
Assert(FindUnitByEntry(HARBOURMASTER) == nil, "the camera must not travel before the client started playing")

-- 2) The client plays: the camera's surroundings arrive, the character's stay
CutsceneBegin(CUTSCENE)
Assert(WaitUntil(function() return FindUnitByEntry(HARBOURMASTER) ~= nil end, 15000, "harbourmaster in camera view"),
	"the client should be sent the harbourmaster once the camera flies over the Haven quay")
Assert(FindUnitByEntry(FARMER_HALDOR) ~= nil, "the surroundings of the character must stay spawned while the camera is away")
Assert(UnitExists(Me()), "the own character must never be despawned by a cutscene")
Log("camera view delivered the harbourmaster while Farmer Haldor stayed")

-- 3) Skipping ends it: the camera's surroundings go, the flag goes
local ends = CutsceneEndCount()
CutsceneEnd(CUTSCENE, true)
Assert(WaitUntil(function() return CutsceneEndCount() == ends + 1 end, 5000, "CutsceneEnd"),
	"the server should confirm the end of the cutscene")
Assert(LastCutsceneEndReason() == REASON_SKIPPED, "end reason " .. LastCutsceneEndReason() .. ", expected skipped")
Assert(WaitUntil(function() return FindUnitByEntry(HARBOURMASTER) == nil end, 5000, "harbourmaster gone"),
	"what only the camera saw should be despawned when the cutscene ends")
Assert(WaitUntil(function() return not InCutscene() end, 5000, "flag cleared"), "the InCutscene flag should be cleared")
Assert(FindUnitByEntry(FARMER_HALDOR) ~= nil, "the surroundings of the character must still be there")
Assert(UnitExists(Me()), "the own character must still be there")

-- 4) Walking away during a cutscene ends it: its protection is for a character that stands still
starts = CutsceneStartCount()
ends = CutsceneEndCount()
GM.PlayCutscene(CUTSCENE)
Assert(WaitUntil(function() return CutsceneStartCount() == starts + 1 end, 5000, "second CutsceneStart"),
	"the cutscene should start a second time")
CutsceneBegin(CUTSCENE)

-- A few metres are forgiven (a step that was being taken, settling onto the ground); this is more
Sleep(1000)
MoveTo(282.0, 5.0, 556.0, 8000)
Assert(WaitUntil(function() return CutsceneEndCount() == ends + 1 end, 8000, "cancelled by movement"),
	"moving the character should end the cutscene")
Assert(LastCutsceneEndReason() == REASON_CANCELLED, "end reason " .. LastCutsceneEndReason() .. ", expected cancelled")
Assert(WaitUntil(function() return not InCutscene() end, 5000, "flag cleared after cancel"),
	"the InCutscene flag should be cleared after the cutscene was cancelled")

SetCutsceneAutoSkip(true)
