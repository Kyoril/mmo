# Ocean Water Phase 2 — Underwater Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Swimming and diving read correctly — depth fog and colour tint, screen distortion, projected caustics, sun shafts, a proper surface-crossing transition, and muffled audio.

**Architecture:** A `PostProcessPass` inside `DeferredRenderer` running after the forward pass, writing to a second texture that `GetFinalRenderTarget()` returns, so the client frame and every editor viewport inherit it unchanged. When the camera is dry the pass is skipped and the raw texture is returned, so the cost is exactly zero. A pure `WaterVolumeSystem` in `game_client` owns the submersion state machine and feeds the pass.

**Tech Stack:** C++17, D3D11 (HLSL), Catch2 via `mmo_add_test`, FMOD for the audio low-pass.

**Prerequisite:** Phase 1 complete — in particular `Terrain::GetWaterTypeAtWorldPos` / `HasWaterAtWorldPos` (Phase 1 Task 3) and the `water_profiles` proto (Phase 1 Task 7).

## Global Constraints

Identical to Phase 1. Repeated here because a task's implementer sees only their own task:

- Copyright header on every new source file: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- Allman braces, every `{` and `}` on its own line. Braces always, even single-line bodies.
- Tabs for indentation.
- Member variables `m_camelCase`. Methods `PascalCase`. Locals and anonymous-namespace free functions `camelCase`. Files `snake_case`.
- `#pragma once` in every header. Doxygen `///` on all public members.
- Root namespace `mmo`. No exceptions — `ASSERT` / `VERIFY` / `UNREACHABLE`, `DLOG` / `WLOG` / `ELOG` from `base/macros.h`.
- Type aliases from `base/typedefs.h`.
- **No wire-format changes.** Underwater state is derived client-side from terrain the client already has. Do not bump `ProtocolVersion`.
- `deferred_shading_tests` and `game_client_tests` link only `base`, `math` and explicitly-listed sources. Do **not** add `graphics`, `scene_graph` or `terrain` to their link libraries.
- `data/client` and `data/editor` are submodules.

## File Structure

**Created:**
- `src/shared/deferred_shading/underwater_settings.h` — plain struct plus the transition maths. No graphics dependency, mirroring `ssao_settings.h` / `contact_shadow_settings.h`.
- `src/shared/deferred_shading/post_process_pass.h` / `.cpp`
- `src/shared/deferred_shading/shaders/PS_Underwater.hlsl`
- `src/shared/game_client/water_volume_system.h` / `.cpp`
- `src/tests/deferred_shading_tests/test_underwater_settings.cpp`
- `src/tests/game_client_tests/test_water_volume_system.cpp`

**Modified:**
- `src/shared/deferred_shading/deferred_renderer.h` / `.cpp` — own the pass, route `GetFinalRenderTarget()`
- `src/shared/audio/audio.h`, `src/shared/fmod_audio/*`, `src/shared/null_audio/*` — `SetLowPassCutoff`
- `src/tests/game_client_tests/fake_audio.h` — record the cutoff
- `src/tests/game_client_tests/CMakeLists.txt` — compile `water_volume_system.cpp` directly
- `src/mmo_client/game_states/world_state.cpp` — drive the system each frame
- `src/mmo_client/console/*` — the `gxUnderwaterGodRays` cvar

---

### Task 1: Underwater settings and transition maths

**Files:**
- Create: `src/shared/deferred_shading/underwater_settings.h`
- Test: `src/tests/deferred_shading_tests/test_underwater_settings.cpp`

**Interfaces:**
- Produces, in `namespace mmo`:
  - `struct UnderwaterState { bool active; float submersionDepth; float surfaceHeight; float transitionPhase; float fogDensity; float fogColor[3]; float absorptionColor[3]; float causticsStrength; float distortionStrength; bool godRaysEnabled; };`
  - `struct UnderwaterSettings` with `float transitionSeconds = 0.4f;` and `float AdvanceTransition(float current, bool targetSubmerged, float deltaSeconds) const;`
  - Task 2's `WaterVolumeSystem` and Task 3's `PostProcessPass` both consume these.

- [ ] **Step 1: Write the failing test**

Create `src/tests/deferred_shading_tests/test_underwater_settings.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "deferred_shading/underwater_settings.h"

using namespace mmo;

TEST_CASE("UnderwaterState_Defaults_To_Dry", "[underwater]")
{
	const UnderwaterState state;
	CHECK_FALSE(state.active);
	CHECK(state.submersionDepth == Approx(0.0f));
	CHECK(state.transitionPhase == Approx(0.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Rises_Toward_One", "[underwater]")
{
	const UnderwaterSettings settings;
	// 0.4s default: a 0.2s step covers half the range.
	const float phase = settings.AdvanceTransition(0.0f, true, 0.2f);
	CHECK(phase == Approx(0.5f));
}

TEST_CASE("UnderwaterSettings_Transition_Clamps_At_One", "[underwater]")
{
	const UnderwaterSettings settings;
	CHECK(settings.AdvanceTransition(0.9f, true, 1.0f) == Approx(1.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Falls_Toward_Zero", "[underwater]")
{
	const UnderwaterSettings settings;
	CHECK(settings.AdvanceTransition(1.0f, false, 0.2f) == Approx(0.5f));
	CHECK(settings.AdvanceTransition(0.1f, false, 1.0f) == Approx(0.0f));
}

TEST_CASE("UnderwaterSettings_Transition_Survives_Zero_Delta", "[underwater]")
{
	// A paused frame must not advance the transition, and must not divide by zero.
	const UnderwaterSettings settings;
	CHECK(settings.AdvanceTransition(0.3f, true, 0.0f) == Approx(0.3f));
}

TEST_CASE("UnderwaterSettings_Transition_Handles_Zero_Duration", "[underwater]")
{
	// A zero transition time must snap rather than produce inf/NaN.
	UnderwaterSettings settings;
	settings.transitionSeconds = 0.0f;
	CHECK(settings.AdvanceTransition(0.0f, true, 0.016f) == Approx(1.0f));
	CHECK(settings.AdvanceTransition(1.0f, false, 0.016f) == Approx(0.0f));
}
```

- [ ] **Step 2: Run and confirm it fails**

```bash
cmake --build build --config Debug -t deferred_shading_tests
```

Expected: `cannot open source file "deferred_shading/underwater_settings.h"`.

- [ ] **Step 3: Write the header**

Create `src/shared/deferred_shading/underwater_settings.h` with `UnderwaterState` and `UnderwaterSettings` as specified in the Interfaces block. `AdvanceTransition` moves `current` toward 1 when `targetSubmerged` and toward 0 otherwise, at `1 / transitionSeconds` per second, clamped to `[0,1]`, snapping when `transitionSeconds <= 0`. Keep it dependency-free apart from `base/typedefs.h`, with a local clamp helper exactly as `ssao_settings.h` does.

- [ ] **Step 4: Run and confirm it passes**

```bash
cmake --build build --config Debug -t deferred_shading_tests && cd build && ctest -C Debug -R deferred_shading_tests --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/shared/deferred_shading/underwater_settings.h src/tests/deferred_shading_tests/test_underwater_settings.cpp
git commit -m "feat(deferred_shading): underwater state and transition maths

Dependency-free so deferred_shading_tests, which links only base and math,
covers it on the headless build.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: WaterVolumeSystem

**Files:**
- Create: `src/shared/game_client/water_volume_system.h` / `.cpp`
- Test: `src/tests/game_client_tests/test_water_volume_system.cpp`
- Modify: `src/tests/game_client_tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `UnderwaterState`, `UnderwaterSettings` (Task 1).
- Produces:
  - `class IWaterQuery { public: virtual ~IWaterQuery() = default; virtual bool HasWaterAt(float x, float z) const = 0; virtual float GetWaterHeightAt(float x, float z) const = 0; virtual uint32 GetWaterTypeAt(float x, float z) const = 0; };`
  - `struct WaterProfileValues { float fogDensity; float fogColor[3]; float absorptionColor[3]; float causticsStrength; float distortionStrength; float audioLowPassHz; bool valid; };`
  - `class WaterVolumeSystem` with `void Update(const Vector3& cameraPosition, const Vector3& playerPosition, float deltaSeconds)`, `const UnderwaterState& GetState() const`, `bool IsPlayerSubmerged() const`, `float GetAudioLowPassHz() const`, `void SetProfileResolver(std::function<WaterProfileValues(uint32)>)`.
  - Task 5 (audio) and Task 8 (client wiring) consume these.

The system deliberately takes an `IWaterQuery` rather than a `Terrain&` so the tests need neither the terrain library nor a `Scene`.

- [ ] **Step 1: Write the failing test**

Create `src/tests/game_client_tests/test_water_volume_system.cpp` with a `FakeWaterQuery` implementing `IWaterQuery` from a simple rule (water present when `x > 0`, surface at `y = 0`, type Ocean = 2). Cover:

- Camera above the surface over water → `active` false, `transitionPhase` 0.
- Camera below the surface over water → `active` true, `submersionDepth` positive and equal to `surfaceHeight - cameraY`.
- **Camera below y=0 where there is NO water** → `active` false. This is the bug the whole presence-vs-height distinction exists to prevent; without it every character standing in a valley below sea level reads as swimming.
- Camera submerged, player dry (third-person camera dipped under) → `active` true, `IsPlayerSubmerged()` false.
- Player submerged, camera dry → `active` false, `IsPlayerSubmerged()` true.
- Repeated `Update` calls ramp `transitionPhase` from 0 to 1 over `transitionSeconds` and back.
- Teleporting straight into deep water starts the ramp at 0 rather than snapping to 1.
- Water type changing under the camera re-resolves the profile.
- A missing profile leaves `fogDensity` at 0 and `audioLowPassHz` at 0 rather than reading uninitialised memory.

- [ ] **Step 2: Run and confirm it fails**

```bash
cmake --build build --config Debug -t game_client_tests
```

- [ ] **Step 3: Implement the system**

Write `water_volume_system.h` / `.cpp`. Keep `Update` free of any graphics, Lua, signal or resource-manager access — it takes two positions and a delta and returns state. Use `HasWaterAt` to gate *everything*: never infer presence from the height.

- [ ] **Step 4: Wire it into the test suite**

`game_client_tests` deliberately compiles individual sources rather than linking `game_client`. Add to `src/tests/game_client_tests/CMakeLists.txt`, in the **unguarded** `target_sources` block beside `remote_movement_queue.cpp`:

```cmake
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/water_volume_system.cpp
```

It must stay unguarded, which means `water_volume_system.cpp` must not include anything from `graphics`, `scene_graph`, `terrain` or `client_data`.

- [ ] **Step 5: Run and confirm it passes**

```bash
cmake --build build --config Debug -t game_client_tests && cd build && ctest -C Debug -R game_client_tests --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/shared/game_client/water_volume_system.h src/shared/game_client/water_volume_system.cpp src/tests/game_client_tests/test_water_volume_system.cpp src/tests/game_client_tests/CMakeLists.txt
git commit -m "feat(game_client): WaterVolumeSystem submersion state machine

Gates on water presence, never on surface height: page water heights are
zero-initialised, so keying off height alone makes every character below
y=0 read as swimming. Takes an IWaterQuery rather than a Terrain so the
tests need no terrain library and no Scene.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: PostProcessPass skeleton and the zero-cost dry path

**Files:**
- Create: `src/shared/deferred_shading/post_process_pass.h` / `.cpp`
- Modify: `src/shared/deferred_shading/deferred_renderer.h` / `.cpp`

**Interfaces:**
- Consumes: `UnderwaterState` (Task 1).
- Produces:
  - `class PostProcessPass` with `void Render(const UnderwaterState&, RenderTexturePtr source, VertexBuffer& quad, ShaderBase& fullscreenVs)`, `TexturePtr GetResult() const`, `bool WouldRun(const UnderwaterState&) const`, `void Resize(uint16 w, uint16 h)`.
  - `DeferredRenderer::SetUnderwaterState(const UnderwaterState&)`.
  - `GetFinalRenderTarget()` returns the post output when the pass ran, otherwise `m_renderTexture` unchanged.
  - Task 4 fills in the shader; Task 8 calls `SetUnderwaterState`.

- [ ] **Step 1: Write the skip test**

In `src/tests/deferred_shading_tests/test_underwater_settings.cpp`, add cases for the pure decision function — extract it as a free function `bool ShouldRunUnderwaterPass(const UnderwaterState&)` in `underwater_settings.h` so it is testable without a device:

- dry state with phase 0 → false
- active state → true
- dry state mid-transition (phase > 0) → true, because the meniscus must still animate out after the camera surfaces

- [ ] **Step 2: Implement the pass**

Model it on `src/shared/deferred_shading/ssao_pass.{h,cpp}`: own a `RenderTexturePtr`, create the shader once, draw one full-screen triangle with the shared quad buffer and the deferred lighting vertex shader. Bind the source colour and the G-buffer normal RT (linear depth in alpha).

- [ ] **Step 3: Route it in DeferredRenderer**

Construct the pass alongside `m_ssaoPass` / `m_contactShadowPass`. At the end of `Render`, after the forward pass and the SRV unbinds:

```cpp
        if (m_postProcessPass && m_postProcessPass->WouldRun(m_underwaterState))
        {
            m_postProcessPass->Render(m_underwaterState, m_renderTexture, *m_quadBuffer, *m_deferredLightVs);
        }
```

and:

```cpp
    TexturePtr DeferredRenderer::GetFinalRenderTarget() const
    {
        // When the camera is dry the pass never ran and owns no current result, so every
        // consumer - the client world renderer frame, the editor viewport, the model editors,
        // the spell visualization preview - gets exactly the texture it got before this feature
        // existed, at exactly the same cost.
        if (m_postProcessPass && m_postProcessPass->WouldRun(m_underwaterState))
        {
            return m_postProcessPass->GetResult();
        }

        return m_renderTexture;
    }
```

Add `Resize` forwarding beside the existing `m_sceneColorCopy->Resize(...)` call.

- [ ] **Step 4: Verify the dry path is untouched**

Build and launch `mmo_edit`. Every viewport must look byte-identical to before. This is the regression that matters most: the pass sits in the path of five different tools.

- [ ] **Step 5: Commit**

---

### Task 4: Underwater fog, tint and distortion

**Files:**
- Create: `src/shared/deferred_shading/shaders/PS_Underwater.hlsl`
- Modify: `post_process_pass.cpp` — the constant buffer

Exponential extinction on the G-buffer linear depth using the profile's absorption colour, density and colour lerped by depth below the surface, plus two low-frequency sine warps of the screen UV whose amplitude falls off with depth. Verify by diving in the test bay: the far wall of the bay should desaturate and blue out with distance, and the image should breathe gently near the surface and settle deeper down.

- [ ] Steps: write the shader, wire the constants, build, dive and screenshot, commit.

---

### Task 5: Audio low-pass

**Files:**
- Modify: `src/shared/audio/audio.h` — `virtual void SetLowPassCutoff(float hz) = 0;`
- Modify: `src/shared/fmod_audio/*` — FMOD low-pass DSP on the master channel group; `hz <= 0` removes the DSP
- Modify: `src/shared/null_audio/*` — no-op
- Modify: `src/tests/game_client_tests/fake_audio.h` — record the last cutoff
- Test: extend `test_water_volume_system.cpp`

The pure virtual makes every backend a compile error if missed. Test that the system ramps the cutoff over the transition rather than switching hard, and that surfacing restores it to 0.

- [ ] Steps: declare, implement all three backends, extend the fake, test, build, commit.

---

### Task 6: Caustics

Project the caustics texture in world space by reconstructing world position from the G-buffer depth, two layers at different scales scrolling against each other, multiplied over the scene, masked to upward-facing surfaces by the G-buffer normal and faded with depth below the surface. Generate `data/client/Textures/Caustics_01.htex` offline the same way as the whitecap noise in Phase 1 Task 9 Step 1.

- [ ] Steps: generate and import the texture, extend the shader, build, dive and screenshot, commit both repos.

---

### Task 7: God rays and the surface crossing

God rays: 16-tap radial blur from the sun's screen position, masked to the upper screen and shallow depths, at half resolution, behind the `gxUnderwaterGodRays` cvar defaulting on. Register the cvar next to the existing `gx*` cvars.

Surface crossing: `transitionPhase` drives a droplet/meniscus band. When the camera plane straddles the surface, split the screen at the waterline in the shader and apply the underwater treatment only below it, rather than snapping the whole frame.

- [ ] Steps: extend the shader, register the cvar, build, swim slowly through the surface and screenshot the half-submerged frame, commit.

---

### Task 8: Client wiring

**Files:**
- Modify: `src/mmo_client/game_states/world_state.cpp`

Own a `WaterVolumeSystem`, implement `IWaterQuery` over the live `Terrain` (forwarding to `HasWaterAtWorldPos`, `GetWaterHeightAtWorldPos`, `GetWaterTypeAtWorldPos` from Phase 1 Task 3), install the profile resolver from `Project::waterProfiles`, call `Update` each frame with the camera and player positions, then push `GetState()` to `DeferredRenderer::SetUnderwaterState` and `GetAudioLowPassHz()` to the audio device.

- [ ] Steps: implement the query adapter, wire the frame update, build, verify a full swim and dive in the client, commit.

---

### Task 9: Full verification

- [ ] Run the gate: `powershell -File tools/gate/verify.ps1` — expect green.
- [ ] Confirm `python tools/protocol_version_check.py` reports no change.
- [ ] Add an E2E scenario that swims a character into the test bay and asserts the swim state, guarding the Phase 1 terrain query changes against regression. Scenarios live in `e2e/scenarios/*.lua`; remember to restore position and destroy spawns so the scenario stays isolated.
- [ ] Real-client check of the full sequence: walk in from the beach, swim, dive, surface, leave. Screenshot each stage.

---

## Phase 2 done when

- `ctest -C Debug` green including the new `[underwater]` and `[water_volume]` cases.
- `tools/gate/verify.ps1` green.
- The dry path is provably unchanged: editor viewports identical to before Task 3.
- A full swim/dive/surface cycle reads correctly in the real client.
- No `ProtocolVersion` bump.
