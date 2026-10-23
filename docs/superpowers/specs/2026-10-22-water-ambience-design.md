# Water ambience — design

Date: 2026-10-22 · Branch: `feature/water-ambience`

## Intent

Near terrain water the player should hear it, coming from where the water is, with nothing placed by
hand. Rivers get rerouted and their flow is repainted with terrain_tool; the sound has to follow
those edits with no audio work. The approach stays the same at every point along the river.

## Goals

- **River.** On a riverbank the river is heard from its side. When the player turns, it moves to the
  other ear. It gets louder closer to the water and fades out over about 40 m.
- **Flow character.** Fast stretches, by the painted flow speed, sound lively and rushing. Slow, wide
  stretches such as the pool around the Haven island sound calm. The two blend smoothly. If flow
  data is missing on a page, the calm sound plays.
- **Ocean.** The sound comes from the shoreline (water meeting land): waves washing up the beach.
  Open water far from any shore is quiet.
- **Bridge.** Water below and on both sides is heard from below and around, not from one
  direction, and slightly fuller than on the bank.
- **Swimming.** Surround stays wide, not directional, plus a close layer of water lapping and
  splashing at the player. Diving under muffles everything through the existing low-pass
  (`WaterVolumeSystem::GetAudioLowPassHz`).
- **Listener orientation fix.** Left and right follow where the player looks. This affects every
  positioned sound in the game and is part of this work.

## Non-goals

- WMO liquid. It is not implemented in the engine; all current rivers are terrain water.
- Placed or authored water emitters of any kind.
- Server involvement. The feature is client-only and cosmetic: no packets, no protocol bump.
- Editor viewport playback.
- Lava and slime sounds (the data model allows them later).
- Indoor muffling. Inside a building next to the river, the water sounds as it does on the bank
  (user decision). No dependency on WMO room volumes.

## Anti-goals

- Audible jumps: the sound snapping between ears, pumping in volume, or jumping from one loop to
  another as the player crosses a 4 m water cell or a page border.
- A wall of noise. Water is a background bed and must sit under the zone ambience, not over it.
  The current zone loops (`HavenTownDay`, `BriarwatchFieldsDay`) contain no water (user), so no
  re-cut is needed; only levels.
- Per-frame cost the player can feel.

## Constraints and facts

- Terrain water: a 4.1667 m grid (8×8 quads per 33.3 m tile). Presence comes from the quad mask,
  never from height (heights are zero-initialised). Queries: `Terrain::HasWaterAtWorldPos`,
  `GetWaterHeightAtWorldPos`, `GetWaterTypeAtWorldPos`. The client wraps them in `IWaterQuery`
  (`src/shared/deferred_shading/water_volume_system.h:49`, implemented as
  `WorldState::TerrainWaterQuery`).
- Water type is per tile: `terrain::WaterType { None, Water, Ocean, Lava, Slime }`. Rivers are
  `Water`. There is no river flag.
- Flow (`WFLW`, now on develop): each outer vertex of a page has a speed byte (0 = still,
  255 = full), `Page::m_waterFlowSpeeds`, `HasWaterFlow()`. **There is no world-position getter;
  one has to be added** (bilinear over the outer-vertex grid, same page/quad resolution as
  `Terrain::ResolveWaterQuad`).
- Loaded pages: 3×3 around the camera page (533 m pages). A 40 m scan always stays inside loaded
  data. Water queried outside a loaded page reads as "no water".
- Audio: FMOD behind `IAudio` (`src/shared/audio/audio.h`). `Update(listenerPosition, time)` takes
  no orientation; FMODAudio hardcodes forward (1,0,0) and up (0,1,0) (`fmod_audio.cpp`, the TODOs
  in `Update`). `IAudio` has no 3D spread control. `null_audio` mirrors every `IAudio` change, and
  the tests' `FakeAudio` must follow too.
- Sounds go through the `SoundEntry` catalog (`sounds.proto`) and `SoundEntryPlayer`. Water
  profiles (`water_profiles.proto`, id = `WaterType` value) already carry per-type data such as
  `audio_lowpass_hz`.
- Mesh ambient emitters (`AmbientSoundEmitters`, develop) already use 12 of FMOD's 64 channels at
  most. Water may use about 5 more.

## Decisions

### Listener orientation (decided: follow the camera)

`IAudio::Update` gains forward and up vectors. The listener **position** stays at the player, as
today. Its **orientation** is the camera's facing, flattened to yaw, so screen-left is ear-left.
In this third-person game, the player character can face the camera while the camera sees the
river on the left; the screen is what the player orients by. FMOD gets the real forward and up and
the real velocity (it is computed today but zero is passed; keep Doppler off with doppler scale 0
if `set3DSettings` does not already).

### Data: water profiles carry the sounds

New optional fields on `WaterProfile` (both proto mirrors, same field numbers):

| Field | Meaning |
|---|---|
| `ambient_calm_sound` | SoundEntry for still and slow water (lakes, wide river pools) |
| `ambient_fast_sound` | SoundEntry for fast flow. 0 = only the calm sound is used |
| `ambient_shore_only` | true for Ocean: only water cells next to land count as sources |
| `ambient_swim_sound` | SoundEntry for the close lapping layer while swimming in this water |
| `ambient_range` | scan and audible radius in metres (default 40) |
| `ambient_volume` | profile volume multiplier (default 1) |

Profile 1 "Water" gets river calm, river fast and the swim layer. Profile 2 "Ocean" gets the
shore waves as its calm sound, `ambient_shore_only = true`, and the swim layer. A profile with no
sound ids is silent. The SoundEntries are looped, streamed, Ambience category, 2D. The system
positions the sound itself, so the entries do not use `is_3d`.

### Runtime: `WaterAmbience` (client, `src/shared/game_client/`)

Owned by `WorldState`, updated each frame after the audio listener update, using the player
position and the camera yaw. Split into a pure, tested core and a thin playback shell.

**1. Scan (pure; every 0.25 s, staggered):** a fixed grid of sample points on the 4.17 m water
grid within `ambient_range` of the player, about 290 points at 40 m. For each point with water:

- profile = water type. The point is skipped if that profile has no sounds.
- With `ambient_shore_only`, a point counts only if a neighbour within one cell is "land": either
  it has no water, or its terrain ground height is at or above the water surface there. The second
  case finds the visible beach even where ocean water is painted under the sand (my call), so the
  scan's data source also needs ground height (`Terrain::GetHeightAt` or equivalent), not only
  `IWaterQuery`.
- weight `w = (1 − d/R)²`, with d the 3D distance from the listener's ear height to the water
  surface point. Height counts, so the water under a bridge is "below" and somewhat further away.
- accumulates, per profile: Σw, Σw·direction (unit vector listener→point), Σw·speed (river only).

Result per profile: **level** = f(Σw), clamped and normalised so that a 10–15 m wide river at 5 m
saturates (exact curve is a tuning constant); **direction** = Σw·dir / Σw; **focus** = |direction|
(1 = all water to one side, 0 = surrounded); **speed** = Σw·speed / Σw.

**2. Smooth:** level, direction and speed are low-passed every frame (about 0.5 s time constant;
direction is interpolated on the sphere), so 0.25 s scan steps and cell edges are inaudible.

**3. Play:** per active profile, one *bed* = the calm loop and, when a fast sound exists, the fast
loop, crossfaded by smoothed speed (equal-power). Each bed loop is a looped channel placed by the
system:

- Position: listener + smoothed direction × a fixed virtual distance (e.g. 5 m, inside the loop's
  min distance so FMOD distance rolloff stays neutral). Volume carries the distance, through level.
- Width: driven by focus. With focus near 1 the sound is a point to one side. As focus falls
  (bridge, swimming, standing at a bend), the sound widens to all around. This needs **3D spread**
  (`FMOD::Channel::set3DSpread`, new `IChannelInstance::Set3DSpread(degrees)`, about 0° to 360°)
  or, if that is not usable, a mix between a 3D voice and a 2D voice of the same loop. The
  builder picks after a listening check.
- Volume: entry volume × profile `ambient_volume` × level × crossfade weight.
- Playback path (my call): the entries stay streamed and 2D in the catalog, but `IAudio` gets a
  way to start a streamed loop as a positioned 3D voice, because today a looped 2D entry becomes
  an `FMOD_2D` stream and `PlaySoundInternal` only enables 3D for 3D sound types. If the 3D/2D mix
  fallback is used for width, each voice needs its own stream handle (one FMOD stream cannot play
  on two channels).
- Loops start at a random playback position (`SetPlaybackPosition`, added for ambient emitters)
  and stop after fading to zero for 2 s. They do not restart on every small level dip.

**4. Swim layer:** while the player's movement reports swimming (`UnitMovement::IsSwimming`), the
profile's `ambient_swim_sound` plays 2D at full width, fading in and out over about 0.5 s. The
profile is the water type at the player.

**5. Bridge:** handled entirely by height-aware distance and focus. Nothing special-cases bridges.
The "slightly fuller" feel comes from the low focus. If the listening test says a bridge needs
more presence, that is tuning, not a new mechanism.

Category is Ambience, so the existing Ambience volume slider and mute apply. Voice budget: at most
2 bed loops per profile, 2 profiles realistically (river near the coast), plus 1 swim layer.

### Sounds (generated)

ElevenLabs text-to-sound (`eleven_text_to_sound_v2`, `loop: true`, 30 s). These four loops go into
`data/client/Sound/Ambience/Water/`:

- river calm (slow, wide water gently flowing and lapping at the banks)
- river fast (lively rushing stream over stones, no waterfall)
- ocean shore (waves washing up a beach and drawing back)
- swim (close water lapping and sloshing around a swimmer)

Lessons from the Briarwatch fields session, which are mandatory:

- Prompt for a "close, clearly audible" recording; set subtlety with gain, never in the prompt.
- Check every take for the generator's 200 Hz hum series and remove it only with the whole-clip FFT
  bin method (`generated-sfx-pipeline` memory). Never use broad notches or layering.
- No synthesized audio.
- The user picks takes by ear from raw takes that were only level-matched.

## Testing

- Pure scan and aggregation (`ScanWaterAmbience` or similar) take a fake `IWaterQuery`-like source
  with type, height and speed. Catch2 cases cover:
  - river on the left gives a direction pointing left and focus close to 1
  - surrounded gives focus close to 0
  - water directly below (bridge) gives a downward direction and low focus
  - shore-only ignores open water
  - level falls with distance
  - speed is averaged correctly
  - no water gives level 0
- The smoothing step is tested for no jump within one frame when the raw level steps.
- The playback shell is tested against `FakeAudio`: voices start and stop, crossfade weights add
  up, the stop happens after the hold time.
- The listener-orientation change is tested in FMOD-free form: `FakeAudio`/`null_audio` receive
  the vectors, and `WorldState` passes camera yaw.
- Manual check in the real client (per the `client-visual-verification` memory): a debug cvar or
  console line prints per-profile level, focus, speed and direction, and the user listens at the
  Briarwatch river, the Haven bridges, swimming, and an ocean shore.

## Calls made by the author, not the user

- The listener orientation follows the camera yaw while the position stays at the player.
- The sounds live on `WaterProfile`. Rivers are profile 1 "Water"; lakes share it at speed 0.
- The shore test uses "no water, or ground at or above the water surface".
- The streamed loops get a 3D playback path in `IAudio`.
- Range is about 40 m.

## Left to the builder

- The exact level curve and saturation width, the virtual distance, smoothing constants, the
  crossfade curve and the stop hold time. These are tuning constants in one place.
- 3D spread versus a 3D/2D voice mix for width.
- The SoundEntry ids for the four loops (next free ids) and the generation prompts, within the
  rules above.
- Where exactly in `WorldState::OnIdle` the update sits, provided it runs after the listener
  update and uses the same player position.
