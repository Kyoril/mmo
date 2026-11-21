# Cutscenes

A cutscene is a camera flight through the world: the camera leaves the player's character, travels
along an authored path, and returns. Narration, music and sound effects can run alongside. Cutscenes
are plain data (`cutscenes` table), known to the client and to the world server.

They are camera-only for now. Nothing in the world is animated or spawned for a cutscene; what the
camera shows is the live world, with the creatures and players that happen to be there.

## What happens when one plays

| | Client | World server |
|---|---|---|
| Start | Hides the ui, locks the player's input, moves the camera to the path | Flags the character `InCutscene` |
| While it runs | Flies the camera, plays sounds, shows subtitles | Moves the player's point of view along the same path |
| End | Gives camera, input and ui back | Clears the flag, takes the point of view back to the character |

A cutscene ends when its timeline is over, when the player skips it, or when the server cancels it.

**The character during a cutscene.** It stays where it is in the world. Other players see a marker
next to its nameplate and a line in its tooltip. It takes no damage, and creatures neither attack it
nor keep attacking it. In return it cannot act: casts, attacks, item use and pet orders are dropped,
and a character that ends up more than a few metres from where it stood ends the cutscene. A
cutscene does not start for a character that is dead or in combat, and not within 30 seconds of the
previous one (the GM command ignores that pause).

**Skipping.** Any key or click shows the hint; Escape while the hint is up skips. The author decides
per cutscene whether it may be skipped.

## The server's point of view

The server only sends a client what is near its character. A camera 900 m away would fly through an
empty world, so the player gets a second point of view for the length of the cutscene: a
[`RemoteView`](../src/shared/game_server/world/remote_view.h) that the world node moves along the
camera path, one visibility tile at a time (every 200 ms, from the moment the client reports that it
plays).

The player stays subscribed at its character the whole time. This has three consequences worth
knowing:

- Nothing around the character is despawned while the camera is away, so there is no burst of
  spawn packets when it comes back.
- The character itself can never be despawned for its own client. The client could not survive
  that, and the view never asks for it.
- Where the two views overlap, the character's own subscription wins. The view passes on only what
  the player would not be told anyway (`TileSubscriber::AcceptsBroadcastFrom`), so no packet reaches
  the client twice.

The server follows the path on its own clock. If the client never reports that it plays (two
minutes), or does not end the cutscene in time (ten seconds past its length), the server ends it.

## Authoring

Cutscenes are edited in mmo_edit, **Cutscene Editor**.

A cutscene belongs to one map and is made of **shots**. A shot is one camera with **keyframes**: a
time, a position, a direction (yaw, pitch) and optionally a field of view. Shots play one after
another, and going from one shot to the next is a hard cut. That is how a cutscene switches cameras.

**Keyframes are world editor viewpoints.** Fly the world editor camera to where the camera should
be, press *Copy Viewpoint*, and use *Paste* on a keyframe (or *Add keyframe from clipboard
viewpoint*). *Copy* on a keyframe goes the other way. *Retime by speed* spaces the keyframes of a
shot so that the camera flies at a steady speed.

| Setting | Meaning |
|---|---|
| Blend in / blend out | Milliseconds the camera glides from the player's own camera onto the path, and back to it at the end. With 0 the cutscene cuts. |
| Fade in / fade out (per shot) | Milliseconds the picture fades from and to black at the ends of the shot. |
| Path (per shot) | A smooth curve through the keyframes, or straight lines between them. |
| Music | A sound entry that replaces the zone music while the cutscene plays. |
| Sounds | Sound entries started at a time on the timeline: narration, effects. |
| Subtitles | Lines shown at the bottom of the screen for a while, translatable like every data text. |

Remember *Export to client* afterwards: the client reads its own copy of the table.

**Cuts to places far away.** The client streams the world in around the camera. A blend or a flight
gives it time to do that. A cut to a camera several hundred metres away does not, so the client
shows the loading screen for a moment and loads the world there first. This applies to the start of
a cutscene without a blend in, to a cut between shots, and to the end of a cutscene without a blend
out that leaves off far from the character. Fades hide the transition.

## Starting a cutscene

| How | Where |
|---|---|
| A new character's first world entry | Race Editor, *Intro Cutscene*. Played once per character; the cutscene has to belong to the race's start map. |
| A trigger | Action **PlayCutscene** (target: a player, data: the cutscene). For example an area trigger at a dungeon entrance. |
| By hand | Console command `cutscene <id>` (GM level 2). Running it again restarts the cutscene, which helps while tuning a path. |
| In code | `Player::StartCutscene(id)` on the world node. |

The intro is recognised by the character's played time being zero. It is saved whenever the
character leaves a world, so a relog or a teleport to another map never plays the intro again, also
not when the first session ended in a crash.

## For programmers

- Data: [`cutscenes.proto`](../src/shared/proto_data/cutscenes.proto), mirrored in
  `src/shared/client_data/`. The timeline is evaluated by the templates in
  [`game/cutscene.h`](../src/shared/game/cutscene.h), on both sides.
- Wire: `CutsceneStart` and `CutsceneEnd` from the server; `CutsceneBegin`, `CutsceneEnd` and the GM
  opcode `CheatPlayCutscene` from the client (game protocol 42).
- World server: [`player_cutscene.cpp`](../src/world_server/player_cutscene.cpp).
- Client: [`CutscenePlayer`](../src/mmo_client/systems/cutscene_player.h) runs the timeline;
  [`world_state_cutscene.cpp`](../src/mmo_client/game_states/world_state_cutscene.cpp) hands it the
  camera (`PlayerController::SetCameraOverride`), the input (`SetInputLocked`) and the ui
  (`FrameManager::SetUiHidden`).
- Ui: `CutsceneFrame.xml` draws the fade, the subtitle and the skip hint. Hiding the ui does not
  hide or close any frame; frames are simply not drawn and get no input, except those marked
  `VisibleWhileUiHidden`. Lua gets the events `CUTSCENE_STARTED(id)` and `CUTSCENE_ENDED(id, reason)`
  and the functions `IsCutscenePlaying`, `GetCutsceneFade`, `GetCutsceneSubtitle`,
  `IsCutsceneSkipHintVisible` and `unit:IsInCutscene()`.
- Bots and the E2E client have no screen. They answer a cutscene by skipping it at once
  (`BotRealmConnector::SetCutsceneAutoSkip`), which keeps a freshly created bot character from
  standing protected and idle through its intro.
- Tests: `[cutscene]` in `game_server_tests` (timeline, remote view), `[cutscenes]` in
  `client_data_tests`, and the E2E scenario `cutscene_camera_view.lua`.

## Limits

- The server follows the camera position only. It does not check what the camera looks at, so a
  camera that looks at something more than about 70 m away shows it without its creatures.
- Zone name, weather profile and ambience still follow the character, not the camera.
- A cutscene plays on the map the character is on. It cannot show another map.
- There is no voice-over yet for the intro; its narration is subtitles only.
