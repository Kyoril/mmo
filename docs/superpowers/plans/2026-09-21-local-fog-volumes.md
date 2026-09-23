# Local Fog Volumes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Box and ellipsoid fog volumes, placed in the world editor and stored per map in `<dir>.hfog`, are injected into the froxel volumetric fog with per-block culling, lit like the zone fog, with colour, soft edges, height falloff, a time-of-day window and wind noise.

**Architecture:** `game_common/fog_volume.h` (data + clamps) and `world_fog_volumes.*` (chunk file) are shared by client and editor. `deferred_shading/fog_volume_math.h` (tested, mirrored in HLSL) and `fog_volume_selection.h` turn authored volumes into per-frame `FogVolumeInstance`s. `VolumetricFogPass` uploads up to 64 of them to a structured buffer at CS `t10`; `CS_FogInject` culls them per 8x8x8 block like lights and adds their tinted density. A new world-editor "Fog Volumes" mode edits them, modelled on the area-trigger mode.

**Tech Stack:** C++17, D3D11 / HLSL, ImGui (mmo_edit), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-21-local-fog-volumes-design.md`

## Global Constraints

- Branch `feature/local-fog-volumes`. Never push. Never merge.
- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `camelCase` locals and anonymous-namespace free functions, `#pragma once`, Doxygen on public members, `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new source file (HLSL included).
- No exceptions; use `ASSERT` / `VERIFY` / `ELOG` / `WLOG`.
- Field ranges (clamped on load and in the editor): size each axis >= 0.5; density [0, 1]; color each [0, 2]; edgeFade [0, 1]; heightFalloff [0, 1]; activeFrom/activeTo [0, 24) (wrapped); fadeHours [0, 6]; noiseAmount [0, 1]; noiseDetail one of 1, 2, 4 (anything else -> 1). Shape `Box = 0`, `Ellipsoid = 1`.
- New-volume defaults: size 20 x 6 x 20, density 0.02, color (1, 1, 1), edgeFade 0.3, heightFalloff 0.1, activeFrom = activeTo = 0 (always on), fadeHours 1, noiseAmount 0.4, noiseDetail 1, yaw 0.
- File `Worlds/<dir>/<dir>.hfog`: chunks `MVER` (uint32 version 1) and `FVOL` (uint32 count, then per volume: uint32 id, uint16-length name, uint8 shape, Vector3 position, Vector3 size, float yaw, float density, Vector3 color, float edgeFade, float heightFalloff, float activeFrom, float activeTo, float fadeHours, float noiseAmount, uint8 noiseDetail). Unknown chunks skipped; missing file = empty list.
- Time-of-day factor: from == to -> 1; otherwise active window [from, to) wrapping past midnight, linear fade over `fadeHours` after `from` and before `to` (fade clamped to half the window length).
- Shape distance in the volume's local frame (translated to the centre, rotated by -yaw about +Y): box = max(|p.x|/h.x, |p.y|/h.y, |p.z|/h.z); ellipsoid = length(p / h); outside when >= 1. Edge fade = 1 if edgeFade == 0 else smoothstep(0, edgeFade, 1 - d). Height = exp(-heightFalloff * (p.y + h.y)).
- Noise uvw: `(worldPos.x * detail / NoiseSize - WindOffset.x * detail, worldPos.y * detail / NoiseSize, worldPos.z * detail / NoiseSize - WindOffset.y * detail)`, factor `NoiseDensityFactor(noise, noiseAmount)`.
- Froxel output: `rgb = radiance * (sigmaZone + sum(sigmaVolume * color))`, `a = sigmaZone + sum(sigmaVolume)`; lighting computed once per froxel.
- Capacities: 64 volumes per frame (nearest first), 16 per 8x8x8 block. GPU record 80 bytes; fog constant buffer 256 bytes. Volume buffer at CS `t10`.
- Build: `cmake --build build --config Debug -t <target>`; tests are executables in `bin/Debug/`. On LNK1168 (exe locked by the user's running client/editor/servers) report it; never kill processes you did not start.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. Do not touch `data/client` or `data/editor`.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/shared/game_common/fog_volume.h` (new) | `FogVolume`, `FogVolumeShape`, `SanitizeFogVolume`, `MakeDefaultFogVolume`. |
| `src/shared/game_common/world_fog_volumes.h/.cpp` (new) | `.hfog` read/write. |
| `src/shared/deferred_shading/fog_volume_math.h` (new) | Time-of-day factor, shape distance, fade, height, `FogVolumeInstance`, capacities. |
| `src/shared/deferred_shading/fog_volume_selection.h` (new) | `SelectFogVolumes` (active, in range, visible, nearest 64). |
| `src/shared/deferred_shading/volumetric_fog_pass.*`, `deferred_renderer.h/.cpp`, shaders `VolumetricFogCommon.hlsli`, `FogVolumeCommon.hlsli` (new), `CS_FogInject.hlsl` | GPU buffer, culling, density. |
| `src/mmo_client/game_states/world_state.*` | Load `.hfog`, feed the renderer. |
| `src/mmo_edit/editors/world_editor/world_editor_instance.*`, `edit_modes/fog_volume_edit_mode.*` (new), `src/mmo_edit/selected_map_entity.*` | Own, save, display and edit volumes. |

---

### Task 1: Fog volume data and the `.hfog` file

**Files:**
- Create: `src/shared/game_common/fog_volume.h`, `src/shared/game_common/world_fog_volumes.h`, `src/shared/game_common/world_fog_volumes.cpp`
- Test: `src/tests/game_common_tests/world_fog_volumes_tests.cpp` (model on `world_foliage_tests.cpp`)

**Interfaces:**
- Produces:
  - `enum class FogVolumeShape : uint8 { Box = 0, Ellipsoid = 1 };`
  - `struct FogVolume { uint32 id = 0; String name; FogVolumeShape shape = FogVolumeShape::Box; Vector3 position; Vector3 size{20, 6, 20}; float yaw = 0; float density = 0.02f; Vector3 color{1, 1, 1}; float edgeFade = 0.3f; float heightFalloff = 0.1f; float activeFrom = 0; float activeTo = 0; float fadeHours = 1; float noiseAmount = 0.4f; uint8 noiseDetail = 1; };`
  - `void SanitizeFogVolume(FogVolume& volume)` — applies every clamp in the Global Constraints (hours wrapped with `fmod` into [0, 24)).
  - `class WorldFogVolumeSerializer { static void Write(io::Writer&, const std::vector<FogVolume>&); };` and `class WorldFogVolumeDeserializer : public ChunkReader { explicit WorldFogVolumeDeserializer(std::vector<FogVolume>& out); }` — mirror the `WorldFoliageSerializer` / foliage reader pair in `world_foliage.h/.cpp` (same `ChunkWriter`/`ChunkReader` usage, `ignoreUnhandledChunks = true`). The reader sanitizes every volume.

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/fog_volume.h"
#include "game_common/world_fog_volumes.h"

#include "memory_source.h"
#include "vector_sink.h"
#include "reader.h"
#include "writer.h"

#include <vector>

using namespace mmo;

namespace
{
	std::vector<char> writeVolumes(const std::vector<FogVolume>& volumes)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		WorldFogVolumeSerializer::Write(writer, volumes);
		return buffer;
	}

	bool readVolumes(const std::vector<char>& buffer, std::vector<FogVolume>& out)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		WorldFogVolumeDeserializer deserializer{ out };
		return deserializer.Read(reader);
	}
}

TEST_CASE("Fog volumes round-trip through the .hfog format", "[fog_volumes]")
{
	FogVolume box;
	box.id = 3;
	box.name = "Pond mist";
	box.shape = FogVolumeShape::Box;
	box.position = Vector3(10.0f, 2.0f, -4.0f);
	box.size = Vector3(30.0f, 5.0f, 12.0f);
	box.yaw = 35.0f;
	box.density = 0.05f;
	box.color = Vector3(0.8f, 1.2f, 0.7f);
	box.edgeFade = 0.5f;
	box.heightFalloff = 0.2f;
	box.activeFrom = 4.0f;
	box.activeTo = 10.0f;
	box.fadeHours = 1.5f;
	box.noiseAmount = 0.6f;
	box.noiseDetail = 2;

	FogVolume ellipsoid;
	ellipsoid.id = 9;
	ellipsoid.shape = FogVolumeShape::Ellipsoid;
	ellipsoid.position = Vector3(-1.0f, 0.0f, 1.0f);

	std::vector<FogVolume> loaded;
	REQUIRE(readVolumes(writeVolumes({ box, ellipsoid }), loaded));
	REQUIRE(loaded.size() == 2);

	const FogVolume& a = loaded[0];
	CHECK(a.id == 3);
	CHECK(a.name == "Pond mist");
	CHECK(a.shape == FogVolumeShape::Box);
	CHECK(a.position.x == Approx(10.0f));
	CHECK(a.size.z == Approx(12.0f));
	CHECK(a.yaw == Approx(35.0f));
	CHECK(a.density == Approx(0.05f));
	CHECK(a.color.y == Approx(1.2f));
	CHECK(a.edgeFade == Approx(0.5f));
	CHECK(a.heightFalloff == Approx(0.2f));
	CHECK(a.activeFrom == Approx(4.0f));
	CHECK(a.activeTo == Approx(10.0f));
	CHECK(a.fadeHours == Approx(1.5f));
	CHECK(a.noiseAmount == Approx(0.6f));
	CHECK(a.noiseDetail == 2);

	CHECK(loaded[1].id == 9);
	CHECK(loaded[1].shape == FogVolumeShape::Ellipsoid);
}

TEST_CASE("Fog volume values are clamped", "[fog_volumes]")
{
	FogVolume volume;
	volume.size = Vector3(0.1f, 3.0f, -2.0f);
	volume.density = 5.0f;
	volume.color = Vector3(-1.0f, 3.0f, 1.0f);
	volume.edgeFade = 2.0f;
	volume.heightFalloff = -1.0f;
	volume.activeFrom = 25.0f;
	volume.activeTo = -2.0f;
	volume.fadeHours = 9.0f;
	volume.noiseAmount = 2.0f;
	volume.noiseDetail = 3;

	SanitizeFogVolume(volume);
	CHECK(volume.size.x == Approx(0.5f));
	CHECK(volume.size.z == Approx(0.5f));
	CHECK(volume.density == Approx(1.0f));
	CHECK(volume.color.x == Approx(0.0f));
	CHECK(volume.color.y == Approx(2.0f));
	CHECK(volume.edgeFade == Approx(1.0f));
	CHECK(volume.heightFalloff == Approx(0.0f));
	CHECK(volume.activeFrom == Approx(1.0f));
	CHECK(volume.activeTo == Approx(22.0f));
	CHECK(volume.fadeHours == Approx(6.0f));
	CHECK(volume.noiseAmount == Approx(1.0f));
	CHECK(volume.noiseDetail == 1);
}

TEST_CASE("An empty fog volume file reads back empty", "[fog_volumes]")
{
	std::vector<FogVolume> loaded;
	REQUIRE(readVolumes(writeVolumes({}), loaded));
	CHECK(loaded.empty());
}
```

Adapt includes and reader entry point (`Read`) to what `world_foliage_tests.cpp` uses.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t game_common_tests`
Expected: compile error, `game_common/fog_volume.h` not found.

- [ ] **Step 3: Implement**

`fog_volume.h` holds the enum, struct (Doxygen per field with its range) and `SanitizeFogVolume` (inline):

```cpp
	inline void SanitizeFogVolume(FogVolume& volume)
	{
		const auto wrapHours = [](const float hours)
		{
			float wrapped = std::fmod(hours, 24.0f);
			if (wrapped < 0.0f)
			{
				wrapped += 24.0f;
			}
			return wrapped >= 24.0f ? 0.0f : wrapped;
		};

		if (volume.shape != FogVolumeShape::Box && volume.shape != FogVolumeShape::Ellipsoid)
		{
			volume.shape = FogVolumeShape::Box;
		}

		volume.size = Vector3(std::max(volume.size.x, 0.5f), std::max(volume.size.y, 0.5f), std::max(volume.size.z, 0.5f));
		volume.density = std::clamp(volume.density, 0.0f, 1.0f);
		volume.color = Vector3(std::clamp(volume.color.x, 0.0f, 2.0f), std::clamp(volume.color.y, 0.0f, 2.0f), std::clamp(volume.color.z, 0.0f, 2.0f));
		volume.edgeFade = std::clamp(volume.edgeFade, 0.0f, 1.0f);
		volume.heightFalloff = std::clamp(volume.heightFalloff, 0.0f, 1.0f);
		volume.activeFrom = wrapHours(volume.activeFrom);
		volume.activeTo = wrapHours(volume.activeTo);
		volume.fadeHours = std::clamp(volume.fadeHours, 0.0f, 6.0f);
		volume.noiseAmount = std::clamp(volume.noiseAmount, 0.0f, 1.0f);
		if (volume.noiseDetail != 1 && volume.noiseDetail != 2 && volume.noiseDetail != 4)
		{
			volume.noiseDetail = 1;
		}
	}
```

Non-finite floats: treat any non-finite value as the struct default before clamping (use `std::isfinite`; a default-constructed `FogVolume` supplies the defaults).

`world_fog_volumes.h/.cpp`: chunk magics `MakeChunkMagic('MVER')` style as in `world_foliage.cpp` (pick `'FVER'`/`'FVOL'`-style four-character codes that do not collide with foliage's; document them), version 1, the field order from the Global Constraints, names written as `uint16` length + bytes (use the same string helper the foliage mesh-name chunk uses). The reader skips unknown chunks, rejects a version above 1 with `ELOG` and returns false, and calls `SanitizeFogVolume` on each volume. Add `world_fog_volumes.cpp` to game_common the way the other sources are picked up (check `src/shared/game_common/CMakeLists.txt`; it may glob).

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --config Debug -t game_common_tests` then `bin/Debug/game_common_tests.exe`.
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/game_common/fog_volume.h src/shared/game_common/world_fog_volumes.h src/shared/game_common/world_fog_volumes.cpp src/tests/game_common_tests/world_fog_volumes_tests.cpp
git commit -m "feat(world): fog volume data and the .hfog file format"
```

---

### Task 2: Fog volume math and per-frame selection

**Files:**
- Create: `src/shared/deferred_shading/fog_volume_math.h`, `src/shared/deferred_shading/fog_volume_selection.h`
- Test: `src/tests/deferred_shading_tests/test_fog_volume_math.cpp`

**Interfaces:**
- Consumes: `FogVolume`, `FogVolumeShape` (Task 1; `fog_volume.h` is header-only, base + math).
- Produces (namespace `mmo::fog_volume`):
  - `constexpr uint32 MaxVolumesPerFrame = 64; constexpr uint32 MaxVolumesPerBlock = 16;`
  - `float TimeOfDayFactor(float hour, float from, float to, float fadeHours)`
  - `Vector3 ToLocal(const Vector3& worldPos, const Vector3& center, float yawSin, float yawCos)` — rotation by -yaw about +Y.
  - `float ShapeDistance(uint32 shape, const Vector3& local, const Vector3& halfSize)`
  - `float EdgeFade(float distance, float edgeFade)`
  - `float HeightFactor(const Vector3& local, const Vector3& halfSize, float heightFalloff)`
  - `struct FogVolumeInstance { Vector3 center; float density; Vector3 halfSize; float heightFalloff; Vector3 color; float edgeFade; float yawSin; float yawCos; float noiseAmount; float noiseDetail; uint32 shape; };`
  - In `fog_volume_selection.h`: `std::vector<FogVolumeInstance> SelectFogVolumes(const std::vector<FogVolume>& volumes, float hour, const Vector3& cameraPosition, float range, const std::function<bool(const Vector3& center, float radius)>& isVisible)` — keeps volumes with factor > 0, whose bounding sphere (radius = half the size's length) is within `range` of the camera (distance to centre minus radius <= range) and visible, sorted by distance to the camera, capped at 64; density multiplied by the factor; yaw converted to sin/cos (degrees to radians).

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/fog_volume_math.h"
#include "deferred_shading/fog_volume_selection.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Fog volume time-of-day factor", "[fog_volume]")
{
	CHECK(fog_volume::TimeOfDayFactor(13.0f, 0.0f, 0.0f, 1.0f) == Approx(1.0f));   // always on
	CHECK(fog_volume::TimeOfDayFactor(7.0f, 4.0f, 10.0f, 1.0f) == Approx(1.0f));
	CHECK(fog_volume::TimeOfDayFactor(12.0f, 4.0f, 10.0f, 1.0f) == Approx(0.0f));
	CHECK(fog_volume::TimeOfDayFactor(4.5f, 4.0f, 10.0f, 1.0f) == Approx(0.5f));   // fading in
	CHECK(fog_volume::TimeOfDayFactor(9.75f, 4.0f, 10.0f, 1.0f) == Approx(0.25f)); // fading out
	CHECK(fog_volume::TimeOfDayFactor(1.0f, 22.0f, 6.0f, 1.0f) == Approx(1.0f));   // across midnight
	CHECK(fog_volume::TimeOfDayFactor(23.0f, 22.0f, 6.0f, 1.0f) == Approx(1.0f));
	CHECK(fog_volume::TimeOfDayFactor(12.0f, 22.0f, 6.0f, 1.0f) == Approx(0.0f));
	CHECK(fog_volume::TimeOfDayFactor(5.0f, 4.0f, 6.0f, 3.0f) == Approx(1.0f));    // fade clamped to half the window
	CHECK(fog_volume::TimeOfDayFactor(7.0f, 4.0f, 10.0f, 0.0f) == Approx(1.0f));   // no fade
}

TEST_CASE("Fog volume shape distance honours yaw", "[fog_volume]")
{
	const Vector3 halfSize(10.0f, 2.0f, 4.0f);
	const float yawSin = std::sin(3.14159265f * 0.5f);   // 90 degrees
	const float yawCos = std::cos(3.14159265f * 0.5f);

	// A point 8 m along world +Z lies along the box's local long axis after a 90 degree yaw.
	const Vector3 local = fog_volume::ToLocal(Vector3(0.0f, 0.0f, 8.0f), Vector3(0.0f, 0.0f, 0.0f), yawSin, yawCos);
	CHECK(std::abs(local.x) == Approx(8.0f).margin(1e-4));
	CHECK(local.z == Approx(0.0f).margin(1e-4));
	CHECK(fog_volume::ShapeDistance(0, local, halfSize) == Approx(0.8f).margin(1e-4));

	CHECK(fog_volume::ShapeDistance(0, Vector3(5.0f, 1.0f, 2.0f), halfSize) == Approx(0.5f));
	CHECK(fog_volume::ShapeDistance(1, Vector3(5.0f, 1.0f, 2.0f), halfSize) == Approx(std::sqrt(0.75f)));
	CHECK(fog_volume::ShapeDistance(1, Vector3(10.0f, 0.0f, 0.0f), halfSize) == Approx(1.0f));
}

TEST_CASE("Fog volume edge fade and height factor", "[fog_volume]")
{
	CHECK(fog_volume::EdgeFade(0.0f, 0.3f) == Approx(1.0f));
	CHECK(fog_volume::EdgeFade(0.7f, 0.3f) == Approx(1.0f));
	CHECK(fog_volume::EdgeFade(1.0f, 0.3f) == Approx(0.0f));
	CHECK(fog_volume::EdgeFade(0.85f, 0.3f) == Approx(0.5f));
	CHECK(fog_volume::EdgeFade(0.99f, 0.0f) == Approx(1.0f));
	CHECK(fog_volume::EdgeFade(1.0f, 0.0f) == Approx(0.0f));

	const Vector3 halfSize(5.0f, 3.0f, 5.0f);
	CHECK(fog_volume::HeightFactor(Vector3(0.0f, -3.0f, 0.0f), halfSize, 0.1f) == Approx(1.0f));
	CHECK(fog_volume::HeightFactor(Vector3(0.0f, 0.0f, 0.0f), halfSize, 0.1f) == Approx(std::exp(-0.3f)));
}

TEST_CASE("Fog volume selection keeps active, near, visible volumes", "[fog_volume]")
{
	std::vector<FogVolume> volumes(4);
	volumes[0].position = Vector3(10.0f, 0.0f, 0.0f);
	volumes[1].position = Vector3(5.0f, 0.0f, 0.0f);
	volumes[2].position = Vector3(1000.0f, 0.0f, 0.0f);                 // beyond range
	volumes[3].position = Vector3(3.0f, 0.0f, 0.0f);
	volumes[3].activeFrom = 4.0f;                                        // inactive at noon
	volumes[3].activeTo = 10.0f;

	const auto all = [](const Vector3&, float) { return true; };
	const std::vector<FogVolumeInstance> selected = SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 200.0f, all);
	REQUIRE(selected.size() == 2);
	CHECK(selected[0].center.x == Approx(5.0f));   // nearest first
	CHECK(selected[1].center.x == Approx(10.0f));
	CHECK(selected[0].halfSize.x == Approx(10.0f));
	CHECK(selected[0].density == Approx(0.02f));

	const auto none = [](const Vector3&, float) { return false; };
	CHECK(SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 200.0f, none).empty());
}

TEST_CASE("Fog volume selection caps at the per-frame limit", "[fog_volume]")
{
	std::vector<FogVolume> volumes(100);
	for (size_t i = 0; i < volumes.size(); ++i)
	{
		volumes[i].position = Vector3(static_cast<float>(100 - i), 0.0f, 0.0f);
	}

	const auto all = [](const Vector3&, float) { return true; };
	const std::vector<FogVolumeInstance> selected = SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 500.0f, all);
	REQUIRE(selected.size() == fog_volume::MaxVolumesPerFrame);
	CHECK(selected.front().center.x == Approx(1.0f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t deferred_shading_tests`
Expected: compile error, headers not found.

- [ ] **Step 3: Implement the headers**

`fog_volume_math.h` (dependency-free apart from base + math; header comment: mirrored by `FogVolumeCommon.hlsli`):

```cpp
		[[nodiscard]] inline float TimeOfDayFactor(const float hour, const float from, const float to, const float fadeHours)
		{
			if (from == to)
			{
				return 1.0f;
			}

			const float length = std::fmod(to - from + 24.0f, 24.0f);
			const float sinceStart = std::fmod(hour - from + 24.0f, 24.0f);
			if (sinceStart >= length)
			{
				return 0.0f;
			}

			const float fade = std::min(fadeHours, length * 0.5f);
			if (fade <= 0.0f)
			{
				return 1.0f;
			}

			const float untilEnd = length - sinceStart;
			return std::clamp(std::min(sinceStart, untilEnd) / fade, 0.0f, 1.0f);
		}

		[[nodiscard]] inline Vector3 ToLocal(const Vector3& worldPos, const Vector3& center, const float yawSin, const float yawCos)
		{
			const Vector3 d = worldPos - center;
			// Rotation by -yaw about +Y.
			return Vector3(d.x * yawCos - d.z * yawSin, d.y, d.x * yawSin + d.z * yawCos);
		}

		[[nodiscard]] inline float ShapeDistance(const uint32 shape, const Vector3& local, const Vector3& halfSize)
		{
			const Vector3 n(local.x / halfSize.x, local.y / halfSize.y, local.z / halfSize.z);
			if (shape == 1)
			{
				return std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			}
			return std::max(std::abs(n.x), std::max(std::abs(n.y), std::abs(n.z)));
		}

		[[nodiscard]] inline float EdgeFade(const float distance, const float edgeFade)
		{
			if (distance >= 1.0f)
			{
				return 0.0f;
			}
			if (edgeFade <= 0.0f)
			{
				return 1.0f;
			}
			const float t = std::clamp((1.0f - distance) / edgeFade, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		[[nodiscard]] inline float HeightFactor(const Vector3& local, const Vector3& halfSize, const float heightFalloff)
		{
			return std::exp(-heightFalloff * (local.y + halfSize.y));
		}
```

Check the yaw sign convention against the test (a 90 degree yaw maps world +Z onto local ±X); the editor's gizmo in Task 5 must use the same convention (yaw rotates the box counter-clockwise seen from above, matching `Quaternion(Degree(yaw), Vector3::UnitY)`). If the test's expectation and `Quaternion`'s convention disagree, keep the math consistent with `Quaternion(Degree(yaw), Vector3::UnitY) * localPoint + center == worldPoint` and adjust only the test's sign expectations, noting it in the report.

`fog_volume_selection.h`: header-only `SelectFogVolumes` as specified in Interfaces (include `game_common/fog_volume.h`, `<algorithm>`, `<functional>`, `<vector>`). Distance = `(position - camera).GetLength()`; radius = `(size * 0.5f).GetLength()`; skip when `distance - radius > range`; sort survivors by distance; `resize` to the cap.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --config Debug -t deferred_shading_tests` then `bin/Debug/deferred_shading_tests.exe "[fog_volume]"` and the full suite.
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/deferred_shading/fog_volume_math.h src/shared/deferred_shading/fog_volume_selection.h src/tests/deferred_shading_tests/test_fog_volume_math.cpp
git commit -m "feat(render): fog volume math and per-frame selection"
```

---

### Task 3: Fog volumes in the froxel inject

**Files:**
- Create: `src/shared/deferred_shading/shaders/FogVolumeCommon.hlsli`
- Modify: `src/shared/deferred_shading/volumetric_fog_pass.h/.cpp`, `src/shared/deferred_shading/deferred_renderer.h/.cpp`
- Modify: `src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli`, `CS_FogInject.hlsl`
- Modify: `docs/rendering-atmosphere.md`
- Test: `src/tests/deferred_shading_tests/test_fog_volume_math.cpp` (GPU record size)

**Interfaces:**
- Consumes: `FogVolumeInstance`, `fog_volume::MaxVolumesPerFrame`, `MaxVolumesPerBlock` (Task 2).
- Produces: `DeferredRenderer::SetFogVolumes(const std::vector<FogVolumeInstance>& volumes)`; `VolumetricFogPass::SetFogVolumes(...)` (same signature).

- [ ] **Step 1: Write the failing test**

Add to `fog_volume_math.h` in Step 3 a `GpuFogVolume` record; test first:

```cpp
TEST_CASE("The GPU fog volume record is 80 bytes", "[fog_volume]")
{
	STATIC_REQUIRE(sizeof(fog_volume::GpuFogVolume) == 80);

	FogVolumeInstance instance;
	instance.center = Vector3(1.0f, 2.0f, 3.0f);
	instance.density = 0.5f;
	instance.shape = 1;
	const fog_volume::GpuFogVolume gpu = fog_volume::ToGpu(instance);
	CHECK(gpu.center[1] == Approx(2.0f));
	CHECK(gpu.density == Approx(0.5f));
	CHECK(gpu.shape == 1u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t deferred_shading_tests`
Expected: compile error, no `GpuFogVolume`.

- [ ] **Step 3: GPU record**

In `fog_volume_math.h`:

```cpp
		/// @brief GPU layout of one fog volume. MUST match struct FogVolume in FogVolumeCommon.hlsli (80 bytes).
		struct alignas(16) GpuFogVolume
		{
			float center[3];
			float density;
			float halfSize[3];
			float heightFalloff;
			float color[3];
			float edgeFade;
			float yawSin;
			float yawCos;
			float noiseAmount;
			float noiseDetail;
			uint32 shape;
			float padding[3];
		};

		[[nodiscard]] inline GpuFogVolume ToGpu(const FogVolumeInstance& instance)
		{
			GpuFogVolume gpu{};
			gpu.center[0] = instance.center.x;
			gpu.center[1] = instance.center.y;
			gpu.center[2] = instance.center.z;
			gpu.density = instance.density;
			gpu.halfSize[0] = instance.halfSize.x;
			gpu.halfSize[1] = instance.halfSize.y;
			gpu.halfSize[2] = instance.halfSize.z;
			gpu.heightFalloff = instance.heightFalloff;
			gpu.color[0] = instance.color.x;
			gpu.color[1] = instance.color.y;
			gpu.color[2] = instance.color.z;
			gpu.edgeFade = instance.edgeFade;
			gpu.yawSin = instance.yawSin;
			gpu.yawCos = instance.yawCos;
			gpu.noiseAmount = instance.noiseAmount;
			gpu.noiseDetail = instance.noiseDetail;
			gpu.shape = instance.shape;
			return gpu;
		}
```

Add `static_assert(sizeof(GpuFogVolume) == 80, ...)` after the struct.

- [ ] **Step 4: Pass and renderer plumbing**

- `VolumetricFogPass`: create a dynamic structured buffer of `fog_volume::MaxVolumesPerFrame` `GpuFogVolume`s in the constructor (`GraphicsDevice::CreateStructuredBuffer(sizeof(GpuFogVolume), MaxVolumesPerFrame, nullptr)`; `ASSERT`). `SetFogVolumes` converts up to the cap with `ToGpu` into a member vector and uploads with `Update` when non-empty; stores the count. In `Render`, set the new constant `fogVolumeCount` and bind the buffer at CS `t10` next to the light buffer (only when the count is > 0; the shader never reads it otherwise).
- `VolumetricFogConstants` appends `uint32 fogVolumeCount; float fogVolumePadding[3];` — `static_assert == 256`. `VolumetricFogBuffer` in `VolumetricFogCommon.hlsli` appends `uint FogVolumeCount; float3 _FogVolumePadding;`.
- `DeferredRenderer::SetFogVolumes(const std::vector<FogVolumeInstance>& volumes)` forwards to the pass (Doxygen: call each frame; an empty vector clears them).

- [ ] **Step 5: HLSL**

`FogVolumeCommon.hlsli` (mirrors `fog_volume_math.h`):

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Local fog volume data and math. Mirrors deferred_shading/fog_volume_math.h (unit-tested) - change both together.

#ifndef FOG_VOLUME_COMMON_HLSLI
#define FOG_VOLUME_COMMON_HLSLI

// MUST stay field-for-field in sync with fog_volume::GpuFogVolume (80 bytes).
struct FogVolume
{
    float3 Center;
    float Density;          // already scaled by the time of day
    float3 HalfSize;
    float HeightFalloff;
    float3 Color;
    float EdgeFade;
    float YawSin;
    float YawCos;
    float NoiseAmount;
    float NoiseDetail;
    uint Shape;             // 0 box, 1 ellipsoid
    float3 _Padding;
};

float3 FogVolumeToLocal(FogVolume volume, float3 worldPos)
{
    float3 d = worldPos - volume.Center;
    return float3(d.x * volume.YawCos - d.z * volume.YawSin, d.y, d.x * volume.YawSin + d.z * volume.YawCos);
}

float FogVolumeShapeDistance(FogVolume volume, float3 local)
{
    float3 n = local / volume.HalfSize;
    if (volume.Shape == 1)
    {
        return length(n);
    }
    return max(abs(n.x), max(abs(n.y), abs(n.z)));
}

float FogVolumeEdgeFade(float distance, float edgeFade)
{
    if (distance >= 1.0f)
    {
        return 0.0f;
    }
    if (edgeFade <= 0.0f)
    {
        return 1.0f;
    }
    return smoothstep(0.0f, edgeFade, 1.0f - distance);
}

float FogVolumeHeightFactor(FogVolume volume, float3 local)
{
    return exp(-volume.HeightFalloff * (local.y + volume.HalfSize.y));
}

#endif
```

(Use the same local transform as the C++ version after Task 2's convention check — copy it exactly.)

`CS_FogInject.hlsl`:
- `#include "FogVolumeCommon.hlsli"`; `StructuredBuffer<FogVolume> FogVolumes : register(t10);`
- `static const uint MAX_BLOCK_VOLUMES = 16; // mirrors fog_volume::MaxVolumesPerBlock`
- `groupshared uint s_blockVolumeHits; groupshared uint s_blockVolumes[MAX_BLOCK_VOLUMES];`
- Reset `s_blockVolumeHits` together with `s_blockLightHits` (thread 0, before the first barrier). In the same parallel pass after the light loop, cull volumes:

```hlsl
    for (uint volumeIndex = groupIndex; volumeIndex < FogVolumeCount; volumeIndex += FOG_GROUP_THREADS)
    {
        FogVolume volume = FogVolumes[volumeIndex];
        float3 toCenter = volume.Center - blockSphere.xyz;
        float reach = length(volume.HalfSize) + blockSphere.w;
        if (dot(toCenter, toCenter) <= reach * reach)
        {
            uint slot;
            InterlockedAdd(s_blockVolumeHits, 1, slot);
            if (slot < MAX_BLOCK_VOLUMES)
            {
                s_blockVolumes[slot] = volumeIndex;
            }
        }
    }
```

  (before the second barrier, so both lists are complete after it).
- Helper computing the tinted and plain volume density sums:

```hlsl
// Adds this froxel's local fog volume density: sigma (extinction) and sigma * colour (scattering).
void AccumulateFogVolumes(float3 worldPos, uint blockVolumeCount, inout float sigmaTotal, inout float3 sigmaScatter)
{
    for (uint i = 0; i < blockVolumeCount; ++i)
    {
        FogVolume volume = FogVolumes[s_blockVolumes[i]];
        float3 local = FogVolumeToLocal(volume, worldPos);
        float distance = FogVolumeShapeDistance(volume, local);
        if (distance >= 1.0f)
        {
            continue;
        }

        float detail = volume.NoiseDetail;
        float3 noiseUvw = float3(worldPos.x * detail / NoiseSize - WindOffset.x * detail, worldPos.y * detail / NoiseSize, worldPos.z * detail / NoiseSize - WindOffset.y * detail);
        float noise = NoiseVolume.SampleLevel(NoiseSampler, noiseUvw, 0.0f);

        float sigma = volume.Density * FogVolumeEdgeFade(distance, volume.EdgeFade) * FogVolumeHeightFactor(volume, local) * NoiseDensityFactor(noise, volume.NoiseAmount);
        sigmaTotal += sigma;
        sigmaScatter += sigma * volume.Color;
    }
}
```

- In `main` after the zone `sigma` and `radiance` are computed:

```hlsl
    uint blockVolumeCount = min(s_blockVolumeHits, MAX_BLOCK_VOLUMES);
    float sigmaTotal = sigma;
    float3 sigmaScatter = sigma.xxx;
    AccumulateFogVolumes(worldPos, blockVolumeCount, sigmaTotal, sigmaScatter);

    InjectOutput[id] = float4(radiance * sigmaScatter, sigmaTotal);
```

  replacing the previous `InjectOutput[id] = float4(radiance * sigma, sigma);`. The debug-mode-4 early return stays as is. Update the file header comment.

- Docs (`docs/rendering-atmosphere.md`): a "Local fog volumes" subsection: shapes, fields, `.hfog` file, per-block culling (16 per block, 64 per frame), lighting shared with the zone fog, only inside the froxel range, debug view 3 includes them.

- [ ] **Step 6: Build and run the tests**

Run: `cmake --build build --config Debug -t deferred_shading_tests scene_graph_tests mmo_client mmo_edit`
Expected: builds (shaders compile); `bin/Debug/deferred_shading_tests.exe` passes.

- [ ] **Step 7: Commit**

```bash
git add src/shared/deferred_shading/fog_volume_math.h src/shared/deferred_shading/volumetric_fog_pass.h src/shared/deferred_shading/volumetric_fog_pass.cpp src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp src/shared/deferred_shading/shaders/FogVolumeCommon.hlsli src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli src/shared/deferred_shading/shaders/CS_FogInject.hlsl docs/rendering-atmosphere.md src/tests/deferred_shading_tests/test_fog_volume_math.cpp
git commit -m "feat(render): local fog volumes in the froxel inject"
```

---

### Task 4: Load and feed volumes in the client and the editor host

**Files:**
- Modify: `src/mmo_client/game_states/world_state.h/.cpp` (`LoadMap` ~5371, per-frame environment update next to `SetColorGrading` / `SetFogLightScattering`)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h/.cpp` (world load, Save ~1109 next to the foliage save, per-frame update next to `SetColorGrading`)

**Interfaces:**
- Consumes: `WorldFogVolumeDeserializer`, `WorldFogVolumeSerializer`, `FogVolume` (Task 1); `SelectFogVolumes` (Task 2); `DeferredRenderer::SetFogVolumes` (Task 3).
- Produces (editor, used by Task 5): on `WorldEditorInstance` (and exposed through `IWorldEditor` in `edit_modes/world_edit_mode.h`): `std::vector<FogVolume>& GetFogVolumes()`, `void MarkFogVolumesChanged()`, `uint32 GenerateFogVolumeId()` (max id + 1).

- [ ] **Step 1: Client**

- Member `std::vector<FogVolume> m_fogVolumes;`.
- In `LoadMap`, after the `.hwld` is read: clear `m_fogVolumes`; open `assetPath + ".hfog"` with `AssetRegistry::OpenFile`; if it exists, read it with `WorldFogVolumeDeserializer` (log `WLOG` and keep an empty list when reading fails); `DLOG` the count.
- Each frame, next to `renderer->SetColorGrading(...)`:

```cpp
		const float hour = m_gameTime.GetNormalizedTimeOfDay() * 24.0f;
		const Camera& camera = /* the world camera used for rendering */;
		renderer->SetFogVolumes(SelectFogVolumes(m_fogVolumes, hour, camera.GetDerivedPosition(), renderer->GetVolumetricFogRange(),
			[&camera](const Vector3& center, const float radius) { return camera.IsVisible(Sphere(center, radius)); }));
```

  Use the real accessors: the game-time API used elsewhere in `world_state.cpp` for the normalized time, the camera the scene renders with, and the fog range (add `DeferredRenderer::GetVolumetricFogRange()` returning the pass setting's range if no getter exists). Check `Sphere`'s constructor in `src/shared/math/sphere.h`.

- [ ] **Step 2: Editor host**

- Member `std::vector<FogVolume> m_fogVolumes; bool m_fogVolumesDirty = false;`.
- On world open (where the `.hwld` is loaded), read `m_assetPath` with extension `.hfog` (via `AssetRegistry::OpenFile` on the same relative path the other world files use) into `m_fogVolumes` if present.
- Save (next to the foliage save): when `m_fogVolumesDirty`, write `WorldFogVolumeSerializer::Write` to the `.hfog` path (same path construction and file-writing style as the foliage save); when the list is empty, delete the file if it exists instead. Clear the dirty flag after a successful write. Include the flag in whatever "unsaved changes" check the editor has, if any.
- Per frame, next to `SetColorGrading`: same `SelectFogVolumes` call using the editor camera and the editor's time of day (the sky component's normalized time used for the environment, times 24).
- Accessors from the Interfaces block, added to `IWorldEditor` as pure virtuals and implemented in `WorldEditorInstance`.

- [ ] **Step 3: Build and run the tests**

Run: `cmake --build build --config Debug -t game_common_tests deferred_shading_tests mmo_client mmo_edit`
Expected: builds; tests pass. There is no new unit test in this task (host wiring); the report must state how the load path handles a missing file.

- [ ] **Step 4: Commit**

```bash
git add src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_editor_instance.h src/mmo_edit/editors/world_editor/world_editor_instance.cpp src/mmo_edit/editors/world_editor/edit_modes/world_edit_mode.h src/shared/deferred_shading/deferred_renderer.h
git commit -m "feat(world): client and editor load, save and render fog volumes"
```

---

### Task 5: World editor "Fog Volumes" mode

**Files:**
- Create: `src/mmo_edit/editors/world_editor/edit_modes/fog_volume_edit_mode.h/.cpp`
- Modify: `src/mmo_edit/selected_map_entity.h/.cpp` (new `SelectedFogVolume`)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h/.cpp` (register the mode; wireframe objects; picking; selection)

**Interfaces:**
- Consumes: `IWorldEditor::GetFogVolumes()`, `MarkFogVolumesChanged()`, `GenerateFogVolumeId()` (Task 4); `FogVolume`, `SanitizeFogVolume` (Task 1).

Model everything on the area-trigger mode: `edit_modes/area_trigger_edit_mode.*` (mode UI, activate/deactivate adding and removing visuals), `WorldEditorInstance::AddAreaTrigger` (~1717-1846: `ManualRenderObject` line list with `Editor/Wireframe`, scene node, query flags for picking, `SelectedAreaTrigger` construction), `SelectedAreaTrigger` in `selected_map_entity.*` (translate / rotate / scale / duplicate / remove), and how the mode is registered (`availableModes[]`, the `unique_ptr` member, construction in the instance constructor).

- [ ] **Step 1: Selectable**

`SelectedFogVolume` holds the volume's `id` (not a pointer — the vector can reallocate), a reference to the editor's volume vector, its scene node and wireframe object, and duplication/removal callbacks like `SelectedAreaTrigger`:
- `Translate(delta)`: `position += delta`; move the node.
- `Rotate(delta)`: take the yaw part of `delta` (rotation about +Y) and add it to `yaw` in degrees, wrapped to [0, 360); update the node orientation to `Quaternion(Degree(yaw), Vector3::UnitY)`.
- `Scale(delta)`: multiply `size` per axis, keep each axis >= 0.5; rebuild the wireframe.
- `SetPosition` / `SetOrientation` / `SetScale` and getters consistently (scale reported as `size`).
- Every change calls `MarkFogVolumesChanged()`.

- [ ] **Step 2: Wireframe**

A function on `WorldEditorInstance` (next to `AddAreaTrigger`) creating the visual for one volume: box = 12 edges of the local box; ellipsoid = three rings (XY, YZ, XZ planes, 48 segments each) scaled by the half-size; drawn in local space on a scene node positioned at `position` with orientation `Quaternion(Degree(yaw), Vector3::UnitY)`. Colour the lines with the volume's colour. Use the same query flag mechanism the area triggers use so clicking selects the volume (add a `SceneQueryFlags_FogVolumes` bit if a new flag is needed).

- [ ] **Step 3: Edit mode**

`FogVolumeEditMode` ("Fog Volumes"):
- `OnActivate`: create visuals for every volume; `OnDeactivate`: remove them.
- `DrawDetails`:
  - "Add Box" / "Add Ellipsoid": new `FogVolume` with the Global Constraints defaults, `id = GenerateFogVolumeId()`, position = the camera focus point the area-trigger mode uses for placement, appended, visual created, selected, `MarkFogVolumesChanged()`.
  - List of volumes (name or "Fog Volume #id"); clicking selects one.
  - Property panel for the selected volume: Name (InputText), Shape (Combo), Position (DragFloat3), Size (DragFloat3, min 0.5), Yaw (DragFloat 0-360), Density (DragFloat 0-1, speed 0.001, "%.4f"), Color (ColorEdit3 HDR, 0-2), Edge Fade, Height Falloff (0-1), Active From / Active To (SliderFloat 0-24 "%.1f h", with a tooltip "equal values = always on"), Fade Hours (0-6), Noise Amount (0-1), Noise Detail (Combo 1x/2x/4x). Every edit calls `SanitizeFogVolume`, rebuilds the visual when shape/size/yaw/colour change, and calls `MarkFogVolumesChanged()`.
  - "Delete" button for the selected volume.
- Register the mode like the area-trigger mode (member, construction, `availableModes[]` entry; no keyboard shortcut needed).

- [ ] **Step 4: Build**

Run: `cmake --build build --config Debug -t mmo_edit`
Expected: builds without warnings from the new files. No automated test covers editor UI; the controller verifies in the editor.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editors/world_editor/edit_modes/fog_volume_edit_mode.h src/mmo_edit/editors/world_editor/edit_modes/fog_volume_edit_mode.cpp src/mmo_edit/selected_map_entity.h src/mmo_edit/selected_map_entity.cpp src/mmo_edit/editors/world_editor/world_editor_instance.h src/mmo_edit/editors/world_editor/world_editor_instance.cpp
git commit -m "feat(editor): place and edit fog volumes in the world editor"
```

(Add any other file the mode needed, e.g. a scene query flag header.)

---

## Controller steps (after all tasks)

1. Final whole-branch review.
2. Full gate.
3. Visual check with the user (editor placement, save, client), then ship on their go.
