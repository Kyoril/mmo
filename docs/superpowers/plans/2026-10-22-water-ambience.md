# Water Ambience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Terrain water (rivers, lakes, ocean shores) plays positioned looping ambience that is
derived automatically from the water data. The listener orientation follows the camera.

**Architecture:** A pure scan in `game_client` samples the 4.17 m water grid around the player
through a small source interface, and aggregates level, direction, focus and flow speed per water
profile. A playback class smooths those values and drives one positioned, streamed 3D loop per
sound (calm/fast crossfade, width via FMOD 3D spread) plus a 2D swim layer. `WorldState`
implements the source over `terrain::Terrain` and feeds it each frame.

**Tech Stack:** C++17, FMOD (behind `IAudio`), protobuf (`water_profiles.proto`), Catch2.

**Spec:** `docs/superpowers/specs/2026-10-22-water-ambience-design.md`

## Global Constraints

- Code style: Allman braces, tabs, `m_camelCase` members, PascalCase methods, `#pragma once`,
  Doxygen on public members, and the header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions. Use `ASSERT` / `WLOG` / `ELOG`.
- Client-only. No packets and no protocol bump.
- Every `IAudio` / `IChannelInstance` change must be mirrored in `FMODAudio`, `NullAudio`
  (`src/shared/null_audio/null_audio.h`) and the tests' `FakeAudio`
  (`src/tests/game_client_tests/fake_audio.h`).
- Both `water_profiles.proto` mirrors (`src/shared/proto_data/`, `src/shared/client_data/`) get
  identical field numbers.
- `game_client_tests` must still build headless. Sources that need `client_data` go in the
  `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR` block of its CMakeLists, and pure sources go in the
  always-built list.
- Sounds: ElevenLabs only. Never use synthesized audio. Remove the 200 Hz hum only with the
  whole-clip FFT bin method. The user picks takes by ear.
- Work in the worktree `H:/mmo-water-amb` on branch `feature/water-ambience`. Data commits go on
  the submodules' `master`, and the parent bumps the gitlinks.

## Review Focus

1. **Scan grid sliding with the player.** If the sample points move with the player, every step
   changes which cells are sampled and the level shimmers. Points must be snapped to the world
   water grid. Owner: Task 4, test `scan points are world-snapped`.
2. **Crossing a page border or an unloaded page.** A sample outside loaded data must read as "no
   water", never as water at height 0. Owner: Task 6. The source implementation uses
   `HasWaterAtWorldPos` first; the test `unloaded page reads as dry` uses a source returning
   no data.
3. **The level dropping to zero for one scan, for example at a teleport or loading.** The voice
   must not stop and restart; it is held for 2 s. Owner: Task 5, test
   `voice survives a short silence`.
4. **Two profiles near each other (river mouth at the coast).** Both play, each with its own
   direction, with no cross-talk. Owner: Task 4, test `river and shore aggregate separately`.
5. **Swimming in water whose profile has no swim sound.** No voice is started and nothing is
   logged repeatedly. Owner: Task 5, test `missing swim sound is silent`.

---

### Task 1: Listener orientation follows the camera

**Files:**
- Modify: `src/shared/audio/audio.h` (IAudio)
- Modify: `src/shared/fmod_audio/fmod_audio.h`, `src/shared/fmod_audio/fmod_audio.cpp` (`init` at about :230, `Update` at about :308)
- Modify: `src/shared/null_audio/null_audio.h`, `src/tests/game_client_tests/fake_audio.h`
- Create: `src/shared/game_client/audio_listener.h` (header-only pure helper)
- Modify: `src/mmo_client/game_states/world_state.cpp` (audio update at about :1135), `src/mmo_client/game_states/login_state.cpp:121`
- Test: `src/tests/game_client_tests/test_audio_listener.cpp`

**Interfaces:**
- Produces: `virtual void IAudio::SetListenerOrientation(const Vector3& forward, const Vector3& up) = 0;`
- Produces: `Vector3 mmo::audio_listener::FlattenedForward(const Vector3& cameraDirection);`
  (in `audio_listener.h`)

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("FlattenedForward drops pitch and normalises", "[audio_listener]")
{
	const Vector3 f = audio_listener::FlattenedForward(Vector3(0.0f, -0.8f, -0.6f));
	CHECK(f.x == Approx(0.0f)); CHECK(f.y == Approx(0.0f)); CHECK(f.z == Approx(-1.0f));
}
TEST_CASE("FlattenedForward looking straight down falls back to -Z", "[audio_listener]")
{
	const Vector3 f = audio_listener::FlattenedForward(Vector3(0.0f, -1.0f, 0.0f));
	CHECK(f.z == Approx(-1.0f));
}
TEST_CASE("FakeAudio records listener orientation", "[audio_listener]")
{
	FakeAudio audio; audio.SetListenerOrientation(Vector3(1, 0, 0), Vector3(0, 1, 0));
	CHECK(audio.listenerForward.x == Approx(1.0f));
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cmake --build build --config Debug -t game_client_tests && bin/Debug/game_client_tests "[audio_listener]"`
Expected: compile error, because `audio_listener.h` and `SetListenerOrientation` do not exist.

- [ ] **Step 3: Implement**

- `FlattenedForward`: zero Y, normalise. If the horizontal length is below 1e-4, return (0,0,-1).
- `IAudio::SetListenerOrientation`:
  - FMOD stores the two vectors in members, defaulting to forward (0,0,-1) and up (0,1,0), and
    `Update` passes them instead of the TODO constants.
  - Listener velocity stays zero: Doppler is not wanted, so the spec's "real velocity" note is
    dropped.
  - FMOD `init` flags become `FMOD_INIT_NORMAL | FMOD_INIT_3D_RIGHTHANDED`. The engine is
    right-handed: `Camera::GetDerivedDirection()` is orientation × −Z. Without the flag, left and
    right come out swapped.
  - `NullAudio` makes it a no-op.
  - `FakeAudio` adds public `Vector3 listenerForward, listenerUp`.
- `WorldState`: right before `m_audio.Update(...)`, call
  `m_audio.SetListenerOrientation(audio_listener::FlattenedForward(m_playerController->GetCamera().GetDerivedDirection()), Vector3::UnitY)`.
- `LoginState`: same, with the glue scene camera direction (or leave the default if that camera
  exposes none; note it in the commit).

- [ ] **Step 4: Run the tests and confirm they pass**

Same command. Expected: 3 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/audio/audio.h src/shared/fmod_audio src/shared/null_audio src/shared/game_client/audio_listener.h src/tests/game_client_tests src/mmo_client/game_states/world_state.cpp src/mmo_client/game_states/login_state.cpp
git commit -m "feat(audio): listener orientation follows the camera (right-handed FMOD)"
```

---

### Task 2: Positioned streamed loops and 3D spread

**Files:**
- Modify: `src/shared/audio/audio.h` (`SoundType`, IAudio, IChannelInstance)
- Modify: `src/shared/fmod_audio/fmod_audio.{h,cpp}` (`CreateSound` switch at about :430, `PlaySoundInternal` 3D branch at about :558)
- Modify: `null_audio.h`, `fake_audio.h`
- Modify: `src/shared/game_client/sound_entry_player.{h,cpp}`
- Test: `src/tests/game_client_tests/test_sound_entry_player_positioned.cpp` (in the client_data-gated block)

**Interfaces:**
- Produces: `SoundType::StreamLooped3D` (new enum value, appended last)
- Produces: `virtual SoundIndex IAudio::CreateLoopedStream3D(const String& fileName) = 0;`
- Produces: `virtual void IChannelInstance::Set3DSpread(float degrees) = 0;`
- Produces: `ChannelIndex SoundEntryPlayer::PlayEntryPositionedLoop(uint32 soundId, const Vector3& position, float minDistance, float maxDistance);`
  It loads the entry's file through `CreateLoopedStream3D`, plays it with `PlaySound3D` in the
  entry's category, and applies the entry volume and pitch like `ApplyChannelSettings`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("PlayEntryPositionedLoop starts a positioned voice", "[sound_entry_player]")
{
	// entry 7: looped, 2D, stream, Ambience, files {"Sound/Ambience/Water/RiverCalm.wav"}, volume 0.5
	FakeAudio audio; SoundEntryPlayer player(audio, sounds);
	const ChannelIndex ch = player.PlayEntryPositionedLoop(7, Vector3(3, 0, 4), 5.0f, 60.0f);
	REQUIRE(ch != InvalidChannel);
	CHECK(audio.WasPlayed("Sound/Ambience/Water/RiverCalm.wav"));
	CHECK(audio.channelPositions[ch].x == Approx(3.0f));
	CHECK(audio.GetChannelVolume(ch) == Approx(0.5f));
}
TEST_CASE("Set3DSpread is recorded on the channel", "[sound_entry_player]")
{
	FakeAudio audio; audio.GetChannelInstance(0)->Set3DSpread(120.0f);
	CHECK(audio.GetChannelSpread(0) == Approx(120.0f));
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `bin/Debug/game_client_tests "[sound_entry_player]"`. Expected: compile error.

- [ ] **Step 3: Implement**

- FMOD `CreateSound` with `StreamLooped3D`: `createStream(..., FMOD_LOOP_NORMAL | FMOD_3D, ...)`.
- The 3D branch in `PlaySoundInternal` also accepts `StreamLooped3D`, and so do the distance-cull
  exceptions (cull only `Sound3D`, as today).
- `FMODChannelInstance::Set3DSpread` calls `channel->set3DSpread(degrees)`.
- An FMOD stream plays on one channel only. Document on `PlayEntryPositionedLoop` that at most one
  voice per entry may be live; Task 5 guarantees this.
- `FakeChannelInstance` stores the spread, and `FakeAudio::GetChannelSpread(ch)` reads it.

- [ ] **Step 4: Run the tests and confirm they pass**

Same command. Expected: PASS. Also run the full suite with `ctest -C Debug -R game_client`.

- [ ] **Step 5: Commit**

`feat(audio): streamed positioned loops and 3D spread`

---

### Task 3: Flow speed at a world position

**Files:**
- Modify: `src/shared/terrain/water_lookup.h` (pure helper)
- Modify: `src/shared/terrain/page.h` (getter for the speed vector)
- Modify: `src/shared/terrain/terrain.{h,cpp}` (next to `GetWaterHeightAtWorldPos`, :2209)
- Test: `src/tests/terrain_tests/test_water_lookup.cpp` (append)

**Interfaces:**
- Produces: `inline float water_lookup::SampleFlowSpeed(const std::vector<uint8>& speeds, float localVertX, float localVertZ);`
  This is a bilinear sample over the 129×129 outer-vertex grid, returning 0..1. It returns 0 when
  `speeds` is empty.
- Produces: `[[nodiscard]] const std::vector<uint8>& Page::GetWaterFlowSpeeds() const;`
- Produces: `[[nodiscard]] float Terrain::GetWaterFlowSpeedAtWorldPos(float x, float z) const;`
  It uses the same page resolution and vertex-space maths as `GetWaterHeightAtWorldPos`, and
  returns 0 off the map, without a page, or without flow data.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("SampleFlowSpeed interpolates between vertices", "[water_lookup]")
{
	std::vector<uint8> s(129 * 129, 0); s[10 + 20 * 129] = 255; s[11 + 20 * 129] = 0;
	CHECK(water_lookup::SampleFlowSpeed(s, 10.0f, 20.0f) == Approx(1.0f));
	CHECK(water_lookup::SampleFlowSpeed(s, 10.5f, 20.0f) == Approx(0.5f));
}
TEST_CASE("SampleFlowSpeed without flow data is still water", "[water_lookup]")
{
	CHECK(water_lookup::SampleFlowSpeed({}, 5.0f, 5.0f) == 0.0f);
}
TEST_CASE("SampleFlowSpeed clamps at the page edge", "[water_lookup]")
{
	std::vector<uint8> s(129 * 129, 255);
	CHECK(water_lookup::SampleFlowSpeed(s, 128.9f, 128.9f) == Approx(1.0f));
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cmake --build build --config Debug -t terrain_tests && bin/Debug/terrain_tests "[water_lookup]"`. Expected: compile error.

- [ ] **Step 3: Implement the three functions with the signatures above.**

- [ ] **Step 4: Run the tests and confirm they pass**

Same command. Expected: PASS.

- [ ] **Step 5: Commit**

`feat(terrain): world-position water flow speed query`

---

### Task 4: Pure water scan

**Files:**
- Create: `src/shared/game_client/water_ambience_scan.{h,cpp}`
- Modify: `src/tests/game_client_tests/CMakeLists.txt` (always-built list)
- Test: `src/tests/game_client_tests/test_water_ambience_scan.cpp`

**Interfaces:**
- Produces, in `water_ambience_scan.h`, namespace `mmo`:

```cpp
struct WaterSample { bool hasWater = false; uint32 waterType = 0; float surfaceY = 0; bool hasGround = false; float groundY = 0; float flowSpeed = 0; };
class IWaterAmbienceSource { public: virtual ~IWaterAmbienceSource() = default; virtual WaterSample Sample(float x, float z) const = 0; };
struct WaterAmbienceProfile { uint32 waterType = 0; bool shoreOnly = false; float range = 40.0f; };
struct WaterAmbienceField { uint32 waterType = 0; float level = 0; Vector3 direction; /* mean, |direction| = focus */ float speed = 0; [[nodiscard]] float GetFocus() const; };
std::vector<WaterAmbienceField> ScanWaterAmbience(const IWaterAmbienceSource& source, const Vector3& ear, const std::vector<WaterAmbienceProfile>& profiles);
namespace water_ambience { constexpr float GridSpacing = 33.333333f / 8.0f; constexpr float LevelSaturationWeight = 8.0f; }
```

The algorithm, as fixed by the spec:
- Sample points sit at **world-snapped** quad centres:
  `(floor(x / GridSpacing) + 0.5) * GridSpacing`.
  They cover a square of ±range around the ear, and points farther than `range` (horizontally)
  are skipped.
- Each water sample of a listed profile gets weight `w = (1 − d / range)²`. Here `d` is the 3D
  distance from the ear to `(x, surfaceY, z)`, and only `d < range` counts.
- With `shoreOnly`, a sample counts only if one of its 4 neighbours at ±GridSpacing is land:
  either `!hasWater`, or `hasGround && groundY >= this sample's surfaceY`.
- Accumulate Σw, Σw·unit(ear → point) and Σw·flowSpeed. Then:
  - `level = min(1, Σw / LevelSaturationWeight)`
  - `direction = Σw·dir / Σw`
  - `speed = Σw·flowSpeed / Σw`
- Return one field per profile, in profile order, including level-0 fields.

- [ ] **Step 1: Write the failing tests**

Use a `FakeWaterSource` with a lambda `(x, z) -> WaterSample`. Ear at (0, 1.6, 0). Each case with
its assertions:

```cpp
TEST_CASE("no water gives level 0")                    // field.level == 0
TEST_CASE("river band on the left points left")        // water where x in [-17,-5], type 1, surfaceY 0: level == Approx(1), direction.x < -0.8f, GetFocus() > 0.8f
TEST_CASE("level falls with distance")                 // same band shifted to x in [-42,-30]: level < 0.15f
TEST_CASE("surrounded gives low focus")                // water everywhere, ear 0.5 above surface: GetFocus() < 0.4f
TEST_CASE("water below a bridge points down")          // band x in [-6,6], surfaceY -4, ear y 1.6: direction.y < -0.3f, std::abs(direction.x) < 0.2f
TEST_CASE("shore only ignores open water")             // ocean (type 2) for x < 0, land x >= 0, profile shoreOnly: all contributing points have x > -GridSpacing*1.5 -> direction.x > -0.95f and level > 0; with open ocean everywhere (no land) level == 0
TEST_CASE("beach under shallow water counts as shore") // ocean everywhere, hasGround with groundY = 0.5 above surface for x >= 0: level > 0
TEST_CASE("speed is the weighted mean")                // flowSpeed 1 for x<-10, 0 for -10<=x<-5: 0 < speed < 1, and uniform 0.6 gives speed == Approx(0.6f)
TEST_CASE("river and shore aggregate separately")      // type 1 on the left, type 2 shore on the right: field[0].direction.x < 0, field[1].direction.x > 0
TEST_CASE("scan points are world-snapped")             // moving the ear 1 m inside one cell changes level by < 0.05 for a band edge
TEST_CASE("unloaded page reads as dry")                // source returns hasWater=false for x<0 (not loaded) and water for x>=0: direction.x > 0
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `bin/Debug/game_client_tests "[water_ambience_scan]"`. Expected: compile error.

- [ ] **Step 3: Implement `ScanWaterAmbience` as specified above.**

- [ ] **Step 4: Run the tests and confirm they pass.**

If the "river band on the left" level assertion disagrees with `LevelSaturationWeight = 8`, tune
that constant so the 12 m band at 5 m saturates and the 30 m case stays below 0.15. Only the
constant changes, never the test values.

- [ ] **Step 5: Commit**

`feat(client): water ambience scan`

---

### Task 5: Smoothing and playback (`WaterAmbience`)

**Files:**
- Create: `src/shared/game_client/water_ambience.{h,cpp}`
- Modify: `src/tests/game_client_tests/CMakeLists.txt` (client_data-gated list, plus the
  `HEADER_FILE_ONLY` list for the test)
- Test: `src/tests/game_client_tests/test_water_ambience.cpp`

**Interfaces:**
- Consumes: Task 2 `PlayEntryPositionedLoop`, `Set3DSpread`; Task 4 scan types.
- Produces:

```cpp
struct WaterAmbienceSounds { WaterAmbienceProfile profile; uint32 calmSound = 0; uint32 fastSound = 0; uint32 swimSound = 0; float volume = 1.0f; };
class WaterAmbience final : public NonCopyable
{
public:
	WaterAmbience(SoundEntryPlayer& player, IAudio& audio, uint32 randomSeed = std::random_device{}());
	~WaterAmbience();                                   // stops all voices
	void SetProfiles(std::vector<WaterAmbienceSounds> profiles);
	void Update(const IWaterAmbienceSource& source, const Vector3& ear, bool swimming, uint32 swimWaterType, float deltaSeconds);
	void Stop();
	[[nodiscard]] const std::vector<WaterAmbienceField>& GetSmoothedFields() const;
	struct VoiceInfo { uint32 soundEntryId; float volume; float spread; Vector3 position; };
	[[nodiscard]] std::vector<VoiceInfo> GetVoices() const;
};
namespace water_ambience { constexpr float ScanInterval = 0.25f; constexpr float SmoothingSeconds = 0.5f; constexpr float VirtualDistance = 5.0f; constexpr float StopHoldSeconds = 2.0f; constexpr float SwimFadeSeconds = 0.5f; constexpr float MinSpread = 60.0f; constexpr float MaxSpread = 360.0f; }
```

The behaviour:
- **Scan cadence.** Scan every `ScanInterval`. The first `Update` scans immediately.
- **Smoothing.** Use a one-pole filter with time constant `SmoothingSeconds` on level, speed and
  the **direction vector itself**. Smoothing the vector, not a normalised direction, keeps
  |direction| as the smoothed focus. While level is 0, keep the last direction.
- **Bed voices per profile.**
  - Crossfade weights: `wCalm = cos(speed·π/2)`, `wFast = sin(speed·π/2)`. When `fastSound == 0`,
    use `wCalm = 1`.
  - Voice volume = entry volume × profile volume × level × weight. Set it through
    `IChannelInstance::SetVolume` each frame.
  - Position = `ear + normalize(direction) × VirtualDistance`, or `ear` when |direction| < 0.01.
    Update it with `Set3DPosition` each frame. Start voices with min distance `VirtualDistance`
    and max distance `range`.
  - Spread = `MinSpread + (1 − focus) × (MaxSpread − MinSpread)`, through `Set3DSpread`.
- **Voice lifetime.**
  - Start a voice when its volume exceeds 0.001, with a random playback position within the loop
    (`SetPlaybackPosition`).
  - Stop it only after the volume has stayed at or below 0.001 for `StopHoldSeconds`.
  - Never run two live voices of the same entry. If two profiles share a sound entry, the higher
    volume wins and the other is skipped.
- **Swim layer.**
  - While `swimming`, the profile of `swimWaterType` with a non-zero `swimSound` plays it through
    `SoundEntryPlayer::PlayEntry(id)` (2D).
  - It fades over `SwimFadeSeconds` in and out, and stops at 0.
  - If that profile has no swim sound, play nothing and log nothing.

- [ ] **Step 1: Write the failing tests**

Use `FakeAudio`, a `SoundManager` with entries 1 calm, 2 fast, 3 swim, 4 shore, and the fake
source from Task 4 (move it into a shared test header `fake_water_source.h`).

```cpp
TEST_CASE("river on the left starts one calm voice to the left")      // speed 0: exactly one started channel, file of entry 1, channelPositions[ch].x < 0
TEST_CASE("fast flow crossfades to the fast loop")                    // flowSpeed 1 everywhere, after 3 s of Update(dt=0.05): fast voice volume > calm voice volume, and calm^2+fast^2 == Approx(level^2 * entryVol^2) within 5%
TEST_CASE("smoothing has no step")                                    // water appears at t=1 s: smoothed level after one 0.05 s frame < 0.2
TEST_CASE("voice survives a short silence")                           // water removed for 1.0 s then restored: startedChannels.size() unchanged, stoppedChannels empty
TEST_CASE("voice stops after the hold time")                          // water removed for 3 s: the channel is in stoppedChannels
TEST_CASE("surrounded widens the spread")                             // water everywhere: spread of bed voice > 250
TEST_CASE("swim layer fades in while swimming")                       // swimming=true, type 1 with swimSound 3: entry 3 played; volume after 0.25 s is between 0.3 and 0.8 of entry volume
TEST_CASE("missing swim sound is silent")                             // profile swimSound 0, swimming=true: no channel started for the swim layer
TEST_CASE("no profiles means no work")                                // SetProfiles({}): Update starts nothing
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `bin/Debug/game_client_tests "[water_ambience]"`. Expected: compile error.

- [ ] **Step 3: Implement `WaterAmbience` as specified above.**

- [ ] **Step 4: Run the tests and confirm they pass**

Same command, then `ctest -C Debug -R game_client`. Expected: all green.

- [ ] **Step 5: Commit**

`feat(client): water ambience playback`

---

### Task 6: Profile data and `WorldState` wiring

**Files:**
- Modify: `src/shared/proto_data/water_profiles.proto`, `src/shared/client_data/water_profiles.proto`
- Modify: `src/mmo_client/game_states/world_state.{h,cpp}` (member next to `m_ambientSounds`, an
  `IWaterAmbienceSource` impl next to `TerrainWaterQuery` at about :1548, update after
  `m_ambientSounds.Update` at about :1140, cleanup next to `m_ambientSounds.Clear()` at about :556,
  console command next to `s_ambientSounds` at about :2477, and the command name next to the
  existing ones)
- Modify: the editor's water profile window, if one exists (`grep -rn "audio_lowpass_hz" src/mmo_edit`),
  so the six fields are editable. Add them right after `audio_lowpass_hz`.

**Interfaces:**
- Consumes: Task 3 `Terrain::GetWaterFlowSpeedAtWorldPos`; Task 5 `WaterAmbience`.
- Produces these proto fields on `WaterProfile`:
  - `optional uint32 ambient_calm_sound = 11;`
  - `optional uint32 ambient_fast_sound = 12;`
  - `optional bool ambient_shore_only = 13;`
  - `optional uint32 ambient_swim_sound = 14;`
  - `optional float ambient_range = 15 [default = 40];`
  - `optional float ambient_volume = 16 [default = 1];`

- [ ] **Step 1: Add the six proto fields to both mirrors with the numbers above.**

- [ ] **Step 2: Implement `WorldState::TerrainWaterAmbienceSource : IWaterAmbienceSource`**

`Sample(x, z)` works as follows:
- `hasWater = m_terrain->HasWaterAtWorldPos(x, z)`
- `waterType = GetWaterTypeAtWorldPos`
- `surfaceY = GetWaterHeightAtWorldPos`
- `hasGround = m_terrain->TryGetSmoothHeightAt(x, z, groundY)`
- `flowSpeed = GetWaterFlowSpeedAtWorldPos`

With no terrain, everything stays false and 0.

- [ ] **Step 3: Load profiles into the playback class**

On world enter (where `m_waterVolume`'s profile resolver is set up, about :1698), build
`std::vector<WaterAmbienceSounds>` from `m_project.waterProfiles` for every profile with
`ambient_calm_sound != 0`, and call `m_waterAmbience.SetProfiles(...)`.

- [ ] **Step 4: Update the water ambience each frame**

After `m_ambientSounds.Update(...)`, call:

```
m_waterAmbience.Update(source, unitPos + Vector3(0, 1.6f, 0),
	unit->GetMovementInfo().IsSwimming(),
	terrain->GetWaterTypeAtWorldPos(unitPos.x, unitPos.z), deltaSeconds)
```

Also call `m_waterAmbience.Stop()` where the ambient sounds are cleared.

- [ ] **Step 5: Add a debug console command**

Add `waterSounds` (category `ConsoleCommandCategory::Sound`). It logs, per smoothed field: water
type, level, focus, speed and direction. Per voice: entry id, volume, spread and position.

- [ ] **Step 6: Build and run the full suite**

Run: `cmake --build build --config Debug -t mmo_client mmo_edit all_tests && cd build && ctest -C Debug --output-on-failure`
Expected: the build succeeds and all tests pass.

- [ ] **Step 7: Commit**

`feat(client): drive water ambience from terrain water`

---

### Task 7: Sounds and data (user audition gate)

**Files (data submodules, on `master`):**
- Create: `data/client/Sound/Ambience/Water/{RiverCalm,RiverFast,OceanShore,Swim}.wav`
- Modify: `data/client/ClientDB/{sounds,water_profiles}.data` and
  `data/editor/data/{sounds,water_profiles}.data` (byte-identical pairs)

- [ ] **Step 1: Generate 4 takes per loop**

Use the ElevenLabs connector: node `sfx`, model `eleven_text_to_sound_v2`, `duration_seconds` 30,
`loop` true, `prompt_influence` 0.5. Prompts ask for "close, clearly audible" recordings and
exclude birds, wind, voices and music. Generate:
- calm wide river gently flowing and lapping at grassy banks
- lively stream rushing over stones, no waterfall
- ocean waves washing up a sandy beach and drawing back
- close water lapping and sloshing around a swimmer

- [ ] **Step 2: Send the raw takes to the user**

Level-match each take to −31 LUFS and nothing else, then send them with `SendUserFile`. Check
each one for the 200 Hz series with the scratch `buzz.py` method. **Wait for the user's picks.**

- [ ] **Step 3: Remove the hum and write the files**

Remove the hum from the picked takes with the whole-clip FFT bin method (±0.4 Hz at k·200 Hz,
k = 1..45). Write 16-bit 44.1 kHz stereo WAVs.

- [ ] **Step 4: Add the SoundEntries**

Add 4 SoundEntries at the next free ids above the current maximum. Each is Ambience, looped,
stream, `is_3d` false, with 1000 ms fades.

- [ ] **Step 5: Set the profile fields**

- Profile 1 "Water": calm, fast, swim.
- Profile 2 "Ocean": shore as its calm sound, `ambient_shore_only` true, swim.

Write both copies of each table, and verify by parsing that the other entries are unchanged.

- [ ] **Step 6: Commit and bump the gitlinks**

Commit both submodules, then bump the gitlinks on `feature/water-ambience`.

---

### Task 8: In-game listening check and tuning

- [ ] **Step 1: Run the client and listen at four spots**

Start the client (see the `client-visual-verification` memory) and run `waterSounds` at each of
these, comparing the log against expectations:
- the Briarwatch river bank, turning 180° (direction must swap ears)
- a Haven bridge (focus < 0.5)
- swimming (swim layer playing)
- an ocean shore (shore direction toward the beach)

- [ ] **Step 2: Have the user listen at the same spots**

Tune only the constants in `water_ambience` / `water_ambience_scan` and the profile volumes.
Commit each tuning round: `tune(client): water ambience <what>`.

- [ ] **Step 3: Hand over for `/ship`**

When the user is happy, they run `/ship`. Before that, check
`git log develop..feature/water-ambience` contains only this work.
