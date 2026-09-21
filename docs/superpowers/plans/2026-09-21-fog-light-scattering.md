# Point and Spot Light Scattering in Volumetric Fog — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Point and spot lights scatter into the froxel volumetric fog, with a per-light fog multiplier, working spot lights (cone convention, world-model format 2.1, authoring) and a per-zone light scattering strength.

**Architecture:** A dependency-free `light_math.h` (C++, unit-tested) and `LightCommon.hlsli` (HLSL mirror) hold attenuation, spot falloff and culling math. The deferred renderer's existing 64-byte `ShaderLight` structured buffer carries precomputed spot cosines and the fog multiplier; the fog inject compute shader binds the same buffer, culls lights per 8x8x8 thread group into group-shared memory and adds each surviving light's phase-weighted contribution to the froxel's scattered light.

**Tech Stack:** C++17, D3D11 / HLSL (SM5 compute), protobuf (proto2), ImGui (mmo_edit), Catch2.

**Spec:** `docs/superpowers/specs/2026-09-21-fog-light-scattering-design.md`

## Global Constraints

- Branch `feature/fog-light-scattering`. Never push. Never merge (merging is /ship's job).
- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `camelCase` locals and anonymous-namespace free functions, `#pragma once`, Doxygen on public members, `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new source file (HLSL included).
- No exceptions (`SIMPLE_NO_EXCEPTIONS`); use `ASSERT` / `VERIFY` / `ELOG`.
- Cone angles are **full cone angles in degrees**. Defaults: outer 45, inner 30. Clamp [1, 179]; inner <= outer.
- Fog scattering multiplier: default 1, clamp [0, 8]. Profile `light_scattering`: default 1, clamp [0, 8].
- `ShaderLight` stays exactly 64 bytes; the fog constant buffer becomes exactly 240 bytes. C++ and HLSL layouts change together.
- Spot falloff: `smoothstep(cos(outer / 2), cos(inner / 2), dot(spotDirection, lightToPoint))`. Point attenuation: `(1 - saturate(d / range))^2`, 0 at and beyond the range.
- Group-shared light list capacity per 8x8x8 froxel block: 64.
- Fog phase cosine for a light: `dot(ray, -lightToFroxel)` (ray = camera-to-froxel direction).
- Proto field numbers: `PointLightConfig.fog_scattering = 9`, `EnvironmentProfile.light_scattering = 25`, identical in `proto_data` and `client_data` mirrors.
- World model format: `Version_2_1 = 0x0201`; `MOLT` entries 60 bytes in 2.1, 48 bytes in 2.0 (read with defaults 30 / 45 / 1).
- `gxAtmosphereDebug` range 0-4; 4 = lights per fog block.
- Build: `cmake --build build --config Debug -t <target>`; tests are executables in `bin/Debug/`. If a link fails with LNK1168, a server or client process holds the exe - report it, do not kill processes you did not start.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Do not modify `data/client` or `data/editor` (content is out of scope for these tasks).

---

## File Structure

| File | Responsibility |
|---|---|
| `src/shared/scene_graph/light_math.h` (new) | Constants, cone cosines, attenuation, spot falloff, sphere and cone culling (C++, dependency-free apart from `math`). Lives in scene_graph because `Light` uses its constants; the spec said `deferred_shading/`, but scene_graph must not include deferred_shading headers. |
| `src/shared/deferred_shading/shaders/LightCommon.hlsli` (new) | HLSL `Light` struct (mirror of `ShaderLight`) and the same math as `light_math.h`. |
| `src/shared/scene_graph/light.h/.cpp` | Cone angles in degrees with clamps, `fogScattering`. |
| `src/shared/scene_graph/scene.h`, `scene.cpp`, `octree_scene.cpp` | `VisibleLightInfo` carries inner/outer angle and fog scattering. |
| `src/shared/deferred_shading/deferred_renderer.h/.cpp` | `ShaderLight` layout, cosines, passes lights to the fog pass, `SetFogLightScattering`. |
| `src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl` | Uses `LightCommon.hlsli`. |
| `src/shared/scene_graph/world_model.h`, `world_model_serializer.h/.cpp`, `world_model_instance.cpp` | Format 2.1 light fields. |
| `src/mmo_edit/editors/world_model_editor/...` | Cone and fog scattering authoring, gizmo. |
| `src/shared/proto_data/spell_visualizations.proto`, `src/shared/client_data/spell_visualizations.proto`, spell/projectile light creation sites, spell visualization editor | `fog_scattering`. |
| `src/shared/graphics_d3d11/structured_buffer_d3d11.cpp` | Compute-stage binding. |
| `src/shared/deferred_shading/volumetric_fog_settings.h`, `volumetric_fog_pass.h/.cpp`, `shaders/VolumetricFogCommon.hlsli`, `shaders/CS_FogInject.hlsl`, `shaders/PS_FogComposite.hlsl` | Light scattering in the froxel inject, debug view 4. |
| Environment profile files (both protos, `environment_profile.h`, `environment_profile_proto.h`, `environment_state.h/.cpp`, editor window, client and editor hosts) | `light_scattering`. |

---

### Task 1: Light math header

**Files:**
- Create: `src/shared/scene_graph/light_math.h`
- Test: `src/tests/scene_graph_tests/test_light_math.cpp` (the suite globs its directory; no CMake change)

**Interfaces:**
- Produces (namespace `mmo::light_math`):
  - `constexpr float MinConeAngle = 1.0f; MaxConeAngle = 179.0f; DefaultInnerConeAngle = 30.0f; DefaultOuterConeAngle = 45.0f; MaxFogScattering = 8.0f;`
  - `constexpr uint32 MaxLightsPerFogBlock = 64;`
  - `float ConeCosine(float fullAngleDegrees)` — `cos(angle / 2)`.
  - `struct SpotCosinePair { float outer; float inner; };` and `SpotCosinePair SpotCosines(float innerDegrees, float outerDegrees)` — `inner` is kept strictly above `outer` (by at least 1e-4) so `smoothstep` never divides by zero.
  - `float PointAttenuation(float distance, float range)`.
  - `float SmoothStep(float edge0, float edge1, float x)` (HLSL semantics).
  - `float SpotFactor(float cosToPoint, float cosOuter, float cosInner)`.
  - `bool SphereIntersectsSphere(const Vector3& centerA, float radiusA, const Vector3& centerB, float radiusB)`.
  - `bool SpotConeIntersectsSphere(const Vector3& apex, const Vector3& direction, float range, float cosHalfAngle, const Vector3& center, float radius)` — conservative: may return true for a sphere just outside the cone, never false for a sphere that contains a point of the cone within range. `direction` must be normalized.

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/light_math.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Cone cosines use half of the full cone angle", "[light_math]")
{
	CHECK(light_math::ConeCosine(90.0f) == Approx(std::cos(3.14159265f / 4.0f)));
	CHECK(light_math::ConeCosine(0.0f) == Approx(1.0f));
	CHECK(light_math::ConeCosine(179.0f) > 0.0f);

	const light_math::SpotCosinePair pair = light_math::SpotCosines(30.0f, 45.0f);
	CHECK(pair.outer == Approx(light_math::ConeCosine(45.0f)));
	CHECK(pair.inner == Approx(light_math::ConeCosine(30.0f)));

	// Equal angles must not produce a zero-width smoothstep.
	const light_math::SpotCosinePair equal = light_math::SpotCosines(40.0f, 40.0f);
	CHECK(equal.inner > equal.outer);
}

TEST_CASE("Point attenuation falls to zero at the range", "[light_math]")
{
	CHECK(light_math::PointAttenuation(0.0f, 10.0f) == Approx(1.0f));
	CHECK(light_math::PointAttenuation(5.0f, 10.0f) == Approx(0.25f));
	CHECK(light_math::PointAttenuation(10.0f, 10.0f) == Approx(0.0f));
	CHECK(light_math::PointAttenuation(12.0f, 10.0f) == Approx(0.0f));
	CHECK(light_math::PointAttenuation(1.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("Spot factor blends between the outer and inner cone", "[light_math]")
{
	const light_math::SpotCosinePair pair = light_math::SpotCosines(30.0f, 60.0f);
	CHECK(light_math::SpotFactor(1.0f, pair.outer, pair.inner) == Approx(1.0f));
	CHECK(light_math::SpotFactor(pair.inner, pair.outer, pair.inner) == Approx(1.0f));
	CHECK(light_math::SpotFactor(pair.outer, pair.outer, pair.inner) == Approx(0.0f));
	CHECK(light_math::SpotFactor(0.0f, pair.outer, pair.inner) == Approx(0.0f));
	CHECK(light_math::SpotFactor((pair.outer + pair.inner) * 0.5f, pair.outer, pair.inner) == Approx(0.5f));
}

TEST_CASE("Sphere intersection includes touching spheres", "[light_math]")
{
	CHECK(light_math::SphereIntersectsSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, Vector3(1.5f, 0.0f, 0.0f), 1.0f));
	CHECK(light_math::SphereIntersectsSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, Vector3(2.0f, 0.0f, 0.0f), 1.0f));
	CHECK_FALSE(light_math::SphereIntersectsSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, Vector3(2.5f, 0.0f, 0.0f), 1.0f));
}

TEST_CASE("Spot cone culling rejects spheres outside the cone", "[light_math]")
{
	const Vector3 apex(0.0f, 0.0f, 0.0f);
	const Vector3 forward(0.0f, 0.0f, 1.0f);
	const float cosHalf = light_math::ConeCosine(40.0f);

	CHECK(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.0f, 0.0f, 10.0f), 1.0f));
	CHECK_FALSE(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.0f, 0.0f, -5.0f), 1.0f));
	CHECK_FALSE(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.0f, 0.0f, 25.0f), 1.0f));
	CHECK_FALSE(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(10.0f, 0.0f, 5.0f), 1.0f));
	// A sphere around the apex always intersects.
	CHECK(light_math::SpotConeIntersectsSphere(apex, forward, 20.0f, cosHalf, Vector3(0.5f, 0.0f, -0.5f), 1.0f));
}

TEST_CASE("Spot cone culling never drops a sphere containing a lit point", "[light_math]")
{
	// Deterministic LCG so the test is reproducible.
	uint32 state = 12345u;
	const auto next = [&state]()
	{
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
	};

	const Vector3 apex(3.0f, 1.0f, -2.0f);
	const Vector3 direction = Vector3(0.3f, -1.0f, 0.2f).NormalizedCopy();
	const float range = 15.0f;
	const float cosHalf = light_math::ConeCosine(70.0f);

	for (int i = 0; i < 2000; ++i)
	{
		const Vector3 point = apex + Vector3(next() * 40.0f - 20.0f, next() * 40.0f - 20.0f, next() * 40.0f - 20.0f);
		const Vector3 toPoint = point - apex;
		const float distance = toPoint.GetLength();
		if (distance <= 1e-3f || distance > range)
		{
			continue;
		}

		if (toPoint.Dot(direction) / distance < cosHalf)
		{
			continue;
		}

		// The point is lit; any sphere containing it must survive culling.
		const float radius = 0.1f + next() * 4.0f;
		const Vector3 center = point + Vector3(next() - 0.5f, next() - 0.5f, next() - 0.5f).NormalizedCopy() * (radius * next());
		CHECK(light_math::SpotConeIntersectsSphere(apex, direction, range, cosHalf, center, radius));
	}
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `scene_graph/light_math.h` not found.

- [ ] **Step 3: Write the header**

Check the exact `Vector3` API in `src/shared/math/vector3.h` first (`Dot`, `GetLength`, `GetSquaredLength`, `NormalizedCopy`); adapt the calls in the header and test to the names that exist.

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Point and spot light math shared by the CPU and the shaders.
	/// @remark LightCommon.hlsli mirrors every function here - change both together. Dependency-free
	///         apart from math so any test suite can compile it.
	namespace light_math
	{
		/// @brief Smallest allowed full spot cone angle, in degrees.
		constexpr float MinConeAngle = 1.0f;

		/// @brief Largest allowed full spot cone angle, in degrees.
		constexpr float MaxConeAngle = 179.0f;

		/// @brief Default full inner cone angle (full strength inside), in degrees.
		constexpr float DefaultInnerConeAngle = 30.0f;

		/// @brief Default full outer cone angle (no light outside), in degrees.
		constexpr float DefaultOuterConeAngle = 45.0f;

		/// @brief Largest per-light fog scattering multiplier.
		constexpr float MaxFogScattering = 8.0f;

		/// @brief Capacity of the group-shared light list of one 8x8x8 froxel block (CS_FogInject.hlsl).
		constexpr uint32 MaxLightsPerFogBlock = 64;

		/// @brief Cosine of half of a full cone angle.
		/// @param fullAngleDegrees Full cone angle in degrees.
		[[nodiscard]] inline float ConeCosine(const float fullAngleDegrees)
		{
			return std::cos(fullAngleDegrees * 0.5f * 3.14159265358979f / 180.0f);
		}

		/// @brief Precomputed spot cone cosines as uploaded in ShaderLight.
		struct SpotCosinePair
		{
			/// @brief Cosine of half the outer cone angle.
			float outer;

			/// @brief Cosine of half the inner cone angle, always above outer.
			float inner;
		};

		/// @brief Converts full inner and outer cone angles (degrees) to shader cosines.
		/// @remark Keeps inner strictly above outer so the shader's smoothstep never has equal edges.
		[[nodiscard]] inline SpotCosinePair SpotCosines(const float innerDegrees, const float outerDegrees)
		{
			SpotCosinePair pair;
			pair.outer = ConeCosine(outerDegrees);
			pair.inner = std::max(ConeCosine(innerDegrees), pair.outer + 1e-4f);
			return pair;
		}

		/// @brief Distance attenuation of point and spot lights: (1 - d / range)^2, 0 at and beyond the range.
		[[nodiscard]] inline float PointAttenuation(const float distance, const float range)
		{
			if (range <= 0.0f || distance >= range)
			{
				return 0.0f;
			}

			const float t = 1.0f - std::clamp(distance / range, 0.0f, 1.0f);
			return t * t;
		}

		/// @brief HLSL smoothstep.
		[[nodiscard]] inline float SmoothStep(const float edge0, const float edge1, const float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		/// @brief Spot cone falloff.
		/// @param cosToPoint dot(spot direction, normalized light-to-point direction).
		[[nodiscard]] inline float SpotFactor(const float cosToPoint, const float cosOuter, const float cosInner)
		{
			return SmoothStep(cosOuter, cosInner, cosToPoint);
		}

		/// @brief Whether two spheres overlap or touch.
		[[nodiscard]] inline bool SphereIntersectsSphere(const Vector3& centerA, const float radiusA, const Vector3& centerB, const float radiusB)
		{
			const float reach = radiusA + radiusB;
			return (centerB - centerA).GetSquaredLength() <= reach * reach;
		}

		/// @brief Conservative spot cone (apex, axis, range, half-angle) vs sphere test.
		/// @remark Never returns false for a sphere containing a point of the cone within range; may
		///         return true for spheres just outside it. Half-angles must be below 90 degrees.
		/// @param direction Normalized cone axis.
		/// @param cosHalfAngle Cosine of half the outer cone angle.
		[[nodiscard]] inline bool SpotConeIntersectsSphere(const Vector3& apex, const Vector3& direction, const float range,
			const float cosHalfAngle, const Vector3& center, const float radius)
		{
			const Vector3 toCenter = center - apex;
			const float lengthSquared = toCenter.GetSquaredLength();
			const float along = toCenter.Dot(direction);
			const float sinHalfAngle = std::sqrt(std::max(0.0f, 1.0f - cosHalfAngle * cosHalfAngle));
			const float closest = cosHalfAngle * std::sqrt(std::max(lengthSquared - along * along, 0.0f)) - along * sinHalfAngle;

			const bool outsideAngle = closest > radius;
			const bool beyondRange = along > range + radius;
			const bool behindApex = along < -radius;
			return !(outsideAngle || beyondRange || behindApex);
		}
	}
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --config Debug -t scene_graph_tests` then `bin/Debug/scene_graph_tests.exe "[light_math]"`
Expected: all `[light_math]` tests pass. Then run the whole suite (`bin/Debug/scene_graph_tests.exe`) — all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/scene_graph/light_math.h src/tests/scene_graph_tests/test_light_math.cpp
git commit -m "feat(render): light math for spot cones, attenuation and culling"
```

---

### Task 2: Working spot lights on surfaces

**Files:**
- Modify: `src/shared/scene_graph/light.h` (cone accessors ~76-90, members ~163-168)
- Modify: `src/shared/scene_graph/light.cpp`
- Modify: `src/shared/scene_graph/scene.h` (`VisibleLightInfo` ~595-607)
- Modify: `src/shared/scene_graph/scene.cpp` (`Scene::GatherVisibleLights` ~490-547)
- Modify: `src/shared/scene_graph/octree_scene.cpp` (~336-347)
- Modify: `src/shared/deferred_shading/deferred_renderer.h` (`ShaderLight` ~457-468)
- Modify: `src/shared/deferred_shading/deferred_renderer.cpp` (`FindLights` ~708-760)
- Create: `src/shared/deferred_shading/shaders/LightCommon.hlsli`
- Modify: `src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl` (struct ~62-75, point ~164-176, spot ~400-421)
- Test: `src/tests/scene_graph_tests/test_light_cone.cpp`

**Interfaces:**
- Consumes: `light_math::MinConeAngle`, `MaxConeAngle`, `DefaultInnerConeAngle`, `DefaultOuterConeAngle`, `MaxFogScattering`, `SpotCosines` (Task 1).
- Produces:
  - `Light::GetInnerConeAngle() / SetInnerConeAngle(float degrees)`, `GetOuterConeAngle() / SetOuterConeAngle(float degrees)`, `GetFogScattering() / SetFogScattering(float)`.
  - `VisibleLightInfo::innerConeAngle`, `outerConeAngle`, `fogScattering` (replacing `spotAngle`).
  - `ShaderLight` fields `spotCosOuter`, `spotCosInner`, `fogScattering` (replacing `spotAngle` and `padding`).
  - `LightCommon.hlsli`: `struct Light`, `LIGHT_TYPE_POINT/DIRECTIONAL/SPOT`, `float LightAttenuation(float distance, float range)`, `float SpotFactor(Light light, float3 lightToPoint)`.

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/light.h"
#include "scene_graph/light_math.h"

using namespace mmo;

TEST_CASE("Spot cone angles default to a usable cone", "[light]")
{
	const Light light(LightType::Spot);
	CHECK(light.GetOuterConeAngle() == Approx(light_math::DefaultOuterConeAngle));
	CHECK(light.GetInnerConeAngle() == Approx(light_math::DefaultInnerConeAngle));
	CHECK(light.GetFogScattering() == Approx(1.0f));
}

TEST_CASE("Spot cone angles clamp and keep inner inside outer", "[light]")
{
	Light light(LightType::Spot);

	light.SetOuterConeAngle(500.0f);
	CHECK(light.GetOuterConeAngle() == Approx(light_math::MaxConeAngle));

	light.SetOuterConeAngle(-3.0f);
	CHECK(light.GetOuterConeAngle() == Approx(light_math::MinConeAngle));
	CHECK(light.GetInnerConeAngle() <= light.GetOuterConeAngle());

	light.SetOuterConeAngle(60.0f);
	light.SetInnerConeAngle(90.0f);
	CHECK(light.GetInnerConeAngle() == Approx(60.0f));

	light.SetInnerConeAngle(20.0f);
	light.SetOuterConeAngle(10.0f);
	CHECK(light.GetOuterConeAngle() == Approx(10.0f));
	CHECK(light.GetInnerConeAngle() == Approx(10.0f));
}

TEST_CASE("Fog scattering clamps to its range", "[light]")
{
	Light light(LightType::Point);
	light.SetFogScattering(-1.0f);
	CHECK(light.GetFogScattering() == Approx(0.0f));
	light.SetFogScattering(20.0f);
	CHECK(light.GetFogScattering() == Approx(light_math::MaxFogScattering));
	light.SetFogScattering(2.5f);
	CHECK(light.GetFogScattering() == Approx(2.5f));
}
```

If `Light(LightType)` is not publicly constructible, create the light through the smallest existing path the other scene_graph tests use (search `src/tests/scene_graph_tests` for `CreateLight`) and adapt the test.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `GetFogScattering` is not a member of `Light`.

- [ ] **Step 3: Update `Light`**

`light.h` — replace the four cone accessors with:

```cpp
		/// @brief Gets the full inner cone angle of the spot light, in degrees. Inside it the light is at full strength.
		[[nodiscard]] float GetInnerConeAngle() const { return m_innerConeAngle; }

		/// @brief Sets the full inner cone angle of the spot light, in degrees.
		/// @param degrees Clamped to [light_math::MinConeAngle, outer cone angle].
		void SetInnerConeAngle(float degrees);

		/// @brief Gets the full outer cone angle of the spot light, in degrees. Outside it the light has no effect.
		[[nodiscard]] float GetOuterConeAngle() const { return m_outerConeAngle; }

		/// @brief Sets the full outer cone angle of the spot light, in degrees.
		/// @param degrees Clamped to [light_math::MinConeAngle, light_math::MaxConeAngle]. Lowers the inner angle if it would exceed it.
		void SetOuterConeAngle(float degrees);

		/// @brief Gets the multiplier on this light's scattering into volumetric fog.
		[[nodiscard]] float GetFogScattering() const { return m_fogScattering; }

		/// @brief Sets the multiplier on this light's scattering into volumetric fog (0 = none).
		/// @param value Clamped to [0, light_math::MaxFogScattering].
		void SetFogScattering(float value);
```

Members (add `#include "light_math.h"` to `light.h`):

```cpp
		float m_innerConeAngle { light_math::DefaultInnerConeAngle };
		float m_outerConeAngle { light_math::DefaultOuterConeAngle };
		float m_fogScattering { 1.0f };
```

`light.cpp` (add `#include <algorithm>` if missing):

```cpp
	void Light::SetInnerConeAngle(const float degrees)
	{
		m_innerConeAngle = std::clamp(degrees, light_math::MinConeAngle, m_outerConeAngle);
	}

	void Light::SetOuterConeAngle(const float degrees)
	{
		m_outerConeAngle = std::clamp(degrees, light_math::MinConeAngle, light_math::MaxConeAngle);
		m_innerConeAngle = std::clamp(m_innerConeAngle, light_math::MinConeAngle, m_outerConeAngle);
	}

	void Light::SetFogScattering(const float value)
	{
		m_fogScattering = std::clamp(value, 0.0f, light_math::MaxFogScattering);
	}
```

- [ ] **Step 4: Carry the new values through light gathering**

`scene.h` `VisibleLightInfo` — replace `float spotAngle = 0.0f;` with:

```cpp
			float innerConeAngle = 0.0f;   // Full inner cone angle in degrees (spot lights)
			float outerConeAngle = 0.0f;   // Full outer cone angle in degrees (spot lights)
			float fogScattering = 1.0f;    // Multiplier on scattering into volumetric fog
```

In both `Scene::GatherVisibleLights` (`scene.cpp`) and `OctreeScene::GatherVisibleLights` (`octree_scene.cpp`), replace every `info.spotAngle = 0.0f;` with `info.innerConeAngle = 0.0f; info.outerConeAngle = 0.0f;`, replace `info.spotAngle = light->GetOuterConeAngle();` with:

```cpp
					info.innerConeAngle = light->GetInnerConeAngle();
					info.outerConeAngle = light->GetOuterConeAngle();
```

and set `info.fogScattering = light->GetFogScattering();` where the other per-light fields (color, intensity, range) are filled, for every light type. Grep `src/` for `spotAngle` afterwards: the only remaining hits may be in the files this task changes next.

- [ ] **Step 5: Change `ShaderLight` and `FindLights`**

`deferred_renderer.h`:

```cpp
        struct alignas(16) ShaderLight
        {
            Vector3 position;
            float range;
            Vector3 color;
            float intensity;
            Vector3 direction;
            float spotCosOuter;   // cos(outer cone / 2); spot lights only
            uint32 type;          // 0 = Point, 1 = Directional, 2 = Spot
            int32 shadowMap;
            float spotCosInner;   // cos(inner cone / 2), always above spotCosOuter; spot lights only
            float fogScattering;  // Multiplier on scattering into volumetric fog
        };

        static_assert(sizeof(ShaderLight) == 64, "ShaderLight must match struct Light in LightCommon.hlsli");
```

`deferred_renderer.cpp` `FindLights` (add `#include "scene_graph/light_math.h"`): replace `shaderLight.spotAngle = ...;` and `shaderLight.padding = Vector2::Zero;` with:

```cpp
            shaderLight.spotCosOuter = 0.0f;
            shaderLight.spotCosInner = 0.0f;
            if (visibleLight.type == LightType::Spot)
            {
                const light_math::SpotCosinePair cosines = light_math::SpotCosines(visibleLight.innerConeAngle, visibleLight.outerConeAngle);
                shaderLight.spotCosOuter = cosines.outer;
                shaderLight.spotCosInner = cosines.inner;
            }
            shaderLight.fogScattering = visibleLight.fogScattering;
```

- [ ] **Step 6: Create `LightCommon.hlsli` and use it in the lighting shader**

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Point and spot light data and math shared by the deferred lighting pass and the volumetric fog.
// The math mirrors scene_graph/light_math.h (unit-tested) - change both together.

#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

static const uint LIGHT_TYPE_POINT = 0;
static const uint LIGHT_TYPE_DIRECTIONAL = 1;
static const uint LIGHT_TYPE_SPOT = 2;

// MUST stay field-for-field in sync with ShaderLight in deferred_renderer.h (64 bytes).
struct Light
{
    float3 Position;
    float Range;
    float3 Color;
    float Intensity;
    float3 Direction;
    float SpotCosOuter;     // cos(outer cone / 2), spot lights only
    uint Type;              // LIGHT_TYPE_*
    uint ShadowMap;
    float SpotCosInner;     // cos(inner cone / 2), always above SpotCosOuter
    float FogScattering;    // multiplier on scattering into volumetric fog
};

// (1 - d / range)^2, 0 at and beyond the range. Mirrors light_math::PointAttenuation.
float LightAttenuation(float distance, float range)
{
    if (distance >= range)
    {
        return 0.0f;
    }

    float t = 1.0f - saturate(distance / range);
    return t * t;
}

// Spot cone falloff; lightToPoint is the normalized direction from the light to the lit point.
// Mirrors light_math::SpotFactor.
float SpotFactor(Light light, float3 lightToPoint)
{
    return smoothstep(light.SpotCosOuter, light.SpotCosInner, dot(normalize(light.Direction), lightToPoint));
}

#endif
```

`PS_DeferredLighting.hlsl`:
- Delete the local `struct Light { ... };` and add `#include "LightCommon.hlsli"` after `#include "AtmosphereCommon.hlsli"`.
- In `CalculatePointLight`, replace `float attenuation = pow(1.0 - saturate(distance / light.Range), 2.0);` with `float attenuation = LightAttenuation(distance, light.Range);`.
- In `CalculateSpotLight`, replace the block from `// Spot cone` through `float attenuation = pow(...) * spotAttenuation;` with:

```hlsl
    // Spot cone. lightDir points from the surface to the light.
    float spotAttenuation = SpotFactor(light, -lightDir);
    if (spotAttenuation <= 0.0)
        return float3(0, 0, 0);

    // Distance attenuation
    float attenuation = LightAttenuation(distance, light.Range) * spotAttenuation;
```

- Replace any remaining `light.SpotAngle` / `SpotAngle` use in the file (grep) the same way.

- [ ] **Step 7: Build and run the tests**

Run: `cmake --build build --config Debug -t scene_graph_tests mmo_client mmo_edit`
Expected: builds (the shader is compiled by the build; an HLSL error fails it). Then `bin/Debug/scene_graph_tests.exe` — all pass.

- [ ] **Step 8: Commit**

```bash
git add src/shared/scene_graph/light.h src/shared/scene_graph/light.cpp src/shared/scene_graph/scene.h src/shared/scene_graph/scene.cpp src/shared/scene_graph/octree_scene.cpp src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp src/shared/deferred_shading/shaders/LightCommon.hlsli src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl src/tests/scene_graph_tests/test_light_cone.cpp
git commit -m "fix(render): spot lights use degree cone angles with an inner falloff"
```

(Add `.gitignore`d generated headers are not committed; check `git status` for a generated `LightCommon`-related file and leave it untracked if it is build output.)

---

### Task 3: World model light format 2.1

**Files:**
- Modify: `src/shared/scene_graph/world_model.h` (`WorldModelLight` ~93-126)
- Modify: `src/shared/scene_graph/world_model_serializer.h` (version enum ~18-28)
- Modify: `src/shared/scene_graph/world_model_serializer.cpp` (`Serialize` ~34-38, lights chunk ~195-223, `ReadLightsChunk` ~893-940)
- Modify: `src/shared/scene_graph/world_model_instance.cpp` (light creation ~1742-1766)
- Test: `src/tests/scene_graph_tests/test_world_model_light_format.cpp`

**Interfaces:**
- Consumes: `Light::SetInnerConeAngle`, `SetOuterConeAngle`, `SetFogScattering` (Task 2); `light_math::Default*ConeAngle` (Task 1).
- Produces: `WorldModelLight::innerConeAngle`, `outerConeAngle`, `fogScattering`; `world_model_version::Version_2_1 = 0x0201`.

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "memory_source.h"
#include "vector_sink.h"
#include "reader.h"
#include "writer.h"

#include "scene_graph/world_model.h"
#include "scene_graph/world_model_serializer.h"

#include <vector>

using namespace mmo;

namespace
{
	WorldModelLight makeSpot()
	{
		WorldModelLight light;
		light.type = WorldModelLight::LightType::Spot;
		light.useAttenuation = true;
		light.color = 0xFFFF8040;
		light.position = Vector3(1.0f, 2.0f, 3.0f);
		light.intensity = 2.5f;
		light.rotation = Quaternion::Identity;
		light.attenuationStart = 0.0f;
		light.attenuationEnd = 12.0f;
		light.innerConeAngle = 20.0f;
		light.outerConeAngle = 70.0f;
		light.fogScattering = 3.0f;
		return light;
	}

	std::vector<char> serialize(const WorldModel& model, const WorldModelVersion version)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		WorldModelSerializer serializer;
		serializer.Serialize(model, writer, version);
		return buffer;
	}

	bool deserialize(const std::vector<char>& buffer, WorldModel& model)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		WorldModelDeserializer deserializer{ model };
		return deserializer.Read(reader);
	}
}

TEST_CASE("World model lights round-trip cone angles and fog scattering", "[world_model]")
{
	WorldModel source;
	source.GetLights().push_back(makeSpot());

	WorldModel loaded;
	REQUIRE(deserialize(serialize(source, world_model_version::Latest), loaded));
	REQUIRE(loaded.GetLights().size() == 1);

	const WorldModelLight& light = loaded.GetLights().front();
	CHECK(light.type == WorldModelLight::LightType::Spot);
	CHECK(light.attenuationEnd == Approx(12.0f));
	CHECK(light.innerConeAngle == Approx(20.0f));
	CHECK(light.outerConeAngle == Approx(70.0f));
	CHECK(light.fogScattering == Approx(3.0f));
}

TEST_CASE("Version 2.0 world model lights load with default cone and fog values", "[world_model]")
{
	WorldModel source;
	source.GetLights().push_back(makeSpot());

	WorldModel loaded;
	REQUIRE(deserialize(serialize(source, world_model_version::Version_2_0), loaded));
	REQUIRE(loaded.GetLights().size() == 1);

	const WorldModelLight& light = loaded.GetLights().front();
	CHECK(light.intensity == Approx(2.5f));
	CHECK(light.attenuationEnd == Approx(12.0f));
	CHECK(light.innerConeAngle == Approx(30.0f));
	CHECK(light.outerConeAngle == Approx(45.0f));
	CHECK(light.fogScattering == Approx(1.0f));
}
```

Adapt includes and names to what exists (`WorldModelSerializer` may need a default constructor or a different entry point; `Quaternion::Identity` may be named differently). If `Serialize` of an otherwise empty `WorldModel` fails or `Read` rejects it, add the minimum content the format requires and note it in the report.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `innerConeAngle` is not a member of `WorldModelLight`.

- [ ] **Step 3: Implement format 2.1**

`world_model.h` — append to `WorldModelLight` (add `#include "light_math.h"`):

```cpp
        /// @brief Full inner cone angle in degrees (spot lights). Full strength inside.
        float innerConeAngle = light_math::DefaultInnerConeAngle;

        /// @brief Full outer cone angle in degrees (spot lights). No light outside.
        float outerConeAngle = light_math::DefaultOuterConeAngle;

        /// @brief Multiplier on this light's scattering into volumetric fog (0 = none).
        float fogScattering = 1.0f;
```

`world_model_serializer.h` — add after `Version_2_0`:

```cpp
            /// @brief Version 2.1: lights store inner and outer cone angles and a fog scattering multiplier.
            Version_2_1 = 0x0201,
```

`world_model_serializer.cpp`:
- In `Serialize`, `Latest` maps to `world_model_version::Version_2_1`.
- Lights chunk:

```cpp
            const bool writeLightExtras = version >= world_model_version::Version_2_1;
            const uint32 lightEntrySize = writeLightExtras ? 60 : 48;
            const uint32 lightCount = static_cast<uint32>(worldModel.GetLights().size());
            const uint32 lightsSize = lightCount * lightEntrySize + 4;
```

  and after writing `light.attenuationEnd`:

```cpp
                if (writeLightExtras)
                {
                    writer
                        << io::write<float>(light.innerConeAngle)
                        << io::write<float>(light.outerConeAngle)
                        << io::write<float>(light.fogScattering);
                }
```

  (End the existing `writer << ... << io::write<float>(light.attenuationEnd);` statement before this block.)
- `ReadLightsChunk`, after the existing reads and before `if (!reader)`:

```cpp
            if (m_version >= world_model_version::Version_2_1)
            {
                reader
                    >> io::read<float>(light.innerConeAngle)
                    >> io::read<float>(light.outerConeAngle)
                    >> io::read<float>(light.fogScattering);
            }
```

`world_model_instance.cpp` — after the range block (before `lightNode.AttachObject(light);`):

```cpp
            if (sceneType == LightType::Spot)
            {
                light.SetOuterConeAngle(wmoLight.outerConeAngle);
                light.SetInnerConeAngle(wmoLight.innerConeAngle);
            }
            light.SetFogScattering(wmoLight.fogScattering);
```

(Use the actual name of the scene `LightType` variable in that function.) Check whether `sceneType == LightType::Spot` lights need their node orientation applied (they already do via `SetOrientation`).

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --config Debug -t scene_graph_tests mmo_client mmo_edit nav_builder` (drop `nav_builder` if the target does not exist; `src/shared/nav_build/map.cpp` also reads world models).
Expected: builds; `bin/Debug/scene_graph_tests.exe` all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/scene_graph/world_model.h src/shared/scene_graph/world_model_serializer.h src/shared/scene_graph/world_model_serializer.cpp src/shared/scene_graph/world_model_instance.cpp src/tests/scene_graph_tests/test_world_model_light_format.cpp
git commit -m "feat(render): world model format 2.1 stores spot cones and fog scattering"
```

---

### Task 4: World model editor light authoring

**Files:**
- Modify: `src/mmo_edit/editors/world_model_editor/panels/world_model_properties_panel.cpp` (`DrawLightProperties` ~281-428)
- Modify: `src/mmo_edit/editors/world_model_editor/world_model_editor_instance.cpp` (preview lights ~3365-3382, spot gizmo ~3490-3505, `CreateLight` ~2920)

**Interfaces:**
- Consumes: `WorldModelLight::innerConeAngle`, `outerConeAngle`, `fogScattering` (Task 3); `Light::SetInnerConeAngle`, `SetOuterConeAngle`, `SetFogScattering` (Task 2); `light_math::MinConeAngle`, `MaxConeAngle`, `MaxFogScattering`.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Add the widgets**

In `DrawLightProperties`, directly after the `// Range` block (before the delete button), add:

```cpp
		// Spot cone
		if (light.type == WorldModelLight::LightType::Spot)
		{
			if (ImGui::DragFloat("Outer Cone##light", &light.outerConeAngle, 0.5f, light_math::MinConeAngle, light_math::MaxConeAngle, "%.1f deg"))
			{
				light.outerConeAngle = std::clamp(light.outerConeAngle, light_math::MinConeAngle, light_math::MaxConeAngle);
				light.innerConeAngle = std::min(light.innerConeAngle, light.outerConeAngle);
				if (callbacks.onUpdateLightVisualizations)
				{
					callbacks.onUpdateLightVisualizations();
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Full cone angle outside which the spot light has no effect.");
			}

			if (ImGui::DragFloat("Inner Cone##light", &light.innerConeAngle, 0.5f, light_math::MinConeAngle, light.outerConeAngle, "%.1f deg"))
			{
				light.innerConeAngle = std::clamp(light.innerConeAngle, light_math::MinConeAngle, light.outerConeAngle);
				if (callbacks.onUpdateLightVisualizations)
				{
					callbacks.onUpdateLightVisualizations();
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Full cone angle inside which the spot light is at full strength. It fades out towards the outer cone.");
			}
		}

		// Fog scattering
		if (ImGui::DragFloat("Fog Scattering##light", &light.fogScattering, 0.05f, 0.0f, light_math::MaxFogScattering, "%.2f"))
		{
			light.fogScattering = std::clamp(light.fogScattering, 0.0f, light_math::MaxFogScattering);
			if (callbacks.onUpdateLightVisualizations)
			{
				callbacks.onUpdateLightVisualizations();
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("How strongly this light glows in volumetric fog. 0 = no glow (e.g. lights inside buildings).");
		}
```

Include `scene_graph/light_math.h` and `<algorithm>` if they are not already reachable.

- [ ] **Step 2: Apply the values to the editor preview lights**

Where the editor creates or updates preview `Light`s from `WorldModelLight` (~3365-3382), after color, intensity and range are applied, add:

```cpp
				if (wmoLight.type == WorldModelLight::LightType::Spot)
				{
					light->SetOuterConeAngle(wmoLight.outerConeAngle);
					light->SetInnerConeAngle(wmoLight.innerConeAngle);
				}
				light->SetFogScattering(wmoLight.fogScattering);
```

(adapt variable names to the code there).

- [ ] **Step 3: Make the spot gizmo match the runtime light**

At runtime a spot light shines along its node orientation times `Vector3::UnitZ` (`light.cpp`, `Light::Update`: `m_derivedDirection = parentOrientation * m_direction`, `m_direction` defaults to `UnitZ`; `world_model_instance.cpp` sets the node orientation from `WorldModelLight::rotation`). The gizmo (~3490-3505) draws along -Z with a hardcoded 45 degree cone.

- Change the gizmo's cone axis to `light.rotation * Vector3::UnitZ` (the same direction the runtime uses) and its half-angle to `light.outerConeAngle * 0.5f` degrees, drawn to `light.attenuationEnd` (or the length it used before if that reads better; keep the existing drawing primitives).
- If `CreateLight` (~2920) gives new spot lights a rotation meant to point down along -Z, adjust it so a new spot light points straight down with the +Z convention (for example a -90 degree pitch rather than +90). State in the report which way new spots point after the change.

- [ ] **Step 4: Build**

Run: `cmake --build build --config Debug -t mmo_edit`
Expected: builds without warnings from the changed files. (No automated test covers ImGui panels; the controller verifies in the editor.)

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editors/world_model_editor/panels/world_model_properties_panel.cpp src/mmo_edit/editors/world_model_editor/world_model_editor_instance.cpp
git commit -m "feat(editor): author spot cones and fog scattering on world model lights"
```

---

### Task 5: Fog scattering for spell and projectile lights

**Files:**
- Modify: `src/shared/proto_data/spell_visualizations.proto` (`PointLightConfig` ~37-47)
- Modify: `src/shared/client_data/spell_visualizations.proto` (same message)
- Modify: `src/shared/game_client/spell_visualization_service.cpp` (~947)
- Modify: `src/shared/game_client/projectile_manager.cpp` (~130-136)
- Modify: `src/shared/game_common/projectile_target.h` (projectile parameters, `lightRange` ~116)
- Modify: `src/mmo_edit/editor_windows/spell_visualization_preview.cpp` (~871, ~1817)
- Modify: `src/mmo_edit/editor_windows/editor_projectile_manager.cpp` (~113-117)
- Modify: `src/mmo_edit/editor_windows/spell_visualization_editor_window.cpp` (kit light ~1036-1070, projectile light ~1495-1516)
- Test: `src/tests/client_data_tests/test_point_light_config.cpp`

**Interfaces:**
- Consumes: `Light::SetFogScattering` (Task 2); `light_math::MaxFogScattering`.
- Produces: `PointLightConfig.fog_scattering` (field 9, default 1); projectile parameters `float lightFogScattering = 1.0f;`.

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/client_data/proto_client/spell_visualizations.pb.h"

using namespace mmo;

TEST_CASE("Point light configs scatter into fog by default", "[spell_visualization]")
{
	const proto_client::PointLightConfig config;
	CHECK(config.fog_scattering() == Approx(1.0f));

	const auto* field = proto_client::PointLightConfig::descriptor()->FindFieldByName("fog_scattering");
	REQUIRE(field != nullptr);
	CHECK(field->number() == 9);
}
```

Match the include path and namespace to how `src/tests/client_data_tests/test_environment_profiles.cpp` includes client protos.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t client_data_tests`
Expected: compile error, no member `fog_scattering`.

- [ ] **Step 3: Add the field and apply it**

Both protos, inside `PointLightConfig` after `fade_out_time`:

```proto
    // Multiplier on how strongly this light glows in volumetric fog (0 = none).
    optional float fog_scattering = 9 [default = 1.0];
```

Game client:
- `spell_visualization_service.cpp`, after `light.SetRange(lightConfig.range());`: `light.SetFogScattering(lightConfig.fog_scattering());`
- `projectile_manager.cpp`, after `m_light->SetRange(lightConfig.range());`: `m_light->SetFogScattering(lightConfig.fog_scattering());`

Editor:
- `projectile_target.h`: next to `float lightRange = 10.0f;` add `/// @brief Multiplier on the light's scattering into volumetric fog.` and `float lightFogScattering = 1.0f;` (match the surrounding doc style).
- `spell_visualization_preview.cpp` ~871: `params.lightFogScattering = lightConfig.fog_scattering();`
- `spell_visualization_preview.cpp` ~1817 (kit preview light): `light.SetFogScattering(lightConfig.fog_scattering());` after the range.
- `editor_projectile_manager.cpp` after `m_light->SetRange(params.lightRange);`: `m_light->SetFogScattering(params.lightFogScattering);`
- `spell_visualization_editor_window.cpp`, kit light section after the Range drag:

```cpp
					float fogScattering = light->has_fog_scattering() ? light->fog_scattering() : 1.0f;
					if (ImGui::DragFloat("Fog Scattering", &fogScattering, 0.05f, 0.0f, light_math::MaxFogScattering, "%.2f"))
					{
						light->set_fog_scattering(std::clamp(fogScattering, 0.0f, light_math::MaxFogScattering));
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("How strongly this light glows in volumetric fog. 0 = no glow.");
					}
```

  and the same in the projectile light section after its Range drag, with the label `"Fog Scattering##proj"`.

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build build --config Debug -t client_data_tests mmo_client mmo_edit`
Expected: builds; `bin/Debug/client_data_tests.exe` all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/proto_data/spell_visualizations.proto src/shared/client_data/spell_visualizations.proto src/shared/game_client/spell_visualization_service.cpp src/shared/game_client/projectile_manager.cpp src/shared/game_common/projectile_target.h src/mmo_edit/editor_windows/spell_visualization_preview.cpp src/mmo_edit/editor_windows/editor_projectile_manager.cpp src/mmo_edit/editor_windows/spell_visualization_editor_window.cpp src/tests/client_data_tests/test_point_light_config.cpp
git commit -m "feat(render): spell and projectile lights carry a fog scattering multiplier"
```

---

### Task 6: Light scattering in the froxel fog

**Files:**
- Modify: `src/shared/graphics_d3d11/structured_buffer_d3d11.cpp` (`BindToStage` ~59-70)
- Modify: `src/shared/deferred_shading/volumetric_fog_settings.h` (debug clamp ~92-96, new strength)
- Modify: `src/shared/deferred_shading/volumetric_fog_pass.h` (`Render` ~50-62)
- Modify: `src/shared/deferred_shading/volumetric_fog_pass.cpp` (constants ~46-76, `Render` ~218-313)
- Modify: `src/shared/deferred_shading/deferred_renderer.h` (fog accessors ~245-253)
- Modify: `src/shared/deferred_shading/deferred_renderer.cpp` (fog pass call ~447)
- Modify: `src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli` (cbuffer ~13-34)
- Modify: `src/shared/deferred_shading/shaders/LightCommon.hlsli` (add culling)
- Modify: `src/shared/deferred_shading/shaders/CS_FogInject.hlsl`
- Modify: `src/shared/deferred_shading/shaders/PS_FogComposite.hlsl` (debug ~58)
- Modify: `src/mmo_client/game_states/world_state.cpp` (cvar help ~2146)
- Modify: `docs/rendering-atmosphere.md`, `docs/console_commands.md`
- Test: `src/tests/deferred_shading_tests/test_volumetric_fog_settings.cpp`

**Interfaces:**
- Consumes: `ShaderLight` / `struct Light`, `LightAttenuation`, `SpotFactor` (Task 2); `light_math::MaxLightsPerFogBlock`, `MaxFogScattering`, `SpotConeIntersectsSphere` semantics (Task 1).
- Produces:
  - `VolumetricFogSettings::lightScatterStrength` (default 1) and `SetLightScatterStrength(float)` clamped to [0, 8]; `SetDebugMode` clamps to [0, 4].
  - `DeferredRenderer::SetFogLightScattering(float strength)`.
  - `VolumetricFogPass::Render(..., ConstantBuffer& cameraBuffer, StructuredBuffer& lights, uint32 lightCount, SamplerState& shadowSampler, ...)`.

- [ ] **Step 1: Write the failing tests**

In `test_volumetric_fog_settings.cpp`, change the existing debug clamp expectation (`SetDebugMode(7)`) from 3 to 4, and add:

```cpp
TEST_CASE("Fog light scattering strength clamps to its range", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.lightScatterStrength == Approx(1.0f));

	settings.SetLightScatterStrength(-2.0f);
	CHECK(settings.lightScatterStrength == Approx(0.0f));

	settings.SetLightScatterStrength(20.0f);
	CHECK(settings.lightScatterStrength == Approx(8.0f));

	settings.SetLightScatterStrength(2.5f);
	CHECK(settings.lightScatterStrength == Approx(2.5f));
}

TEST_CASE("Fog debug view 4 shows lights per block", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	settings.SetDebugMode(4);
	CHECK(settings.debugMode == 4u);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --config Debug -t deferred_shading_tests`
Expected: compile error, no member `lightScatterStrength`.

- [ ] **Step 3: Settings, renderer accessor, structured buffer binding**

`volumetric_fog_settings.h`:

```cpp
		/// @brief Multiplier on point and spot light scattering (the zone's light_scattering).
		float lightScatterStrength = 1.0f;
```

```cpp
		/// @brief Sets the point and spot light scattering strength, clamped to [0, 8].
		void SetLightScatterStrength(const float value)
		{
			lightScatterStrength = std::clamp(value, 0.0f, 8.0f);
		}
```

and `SetDebugMode` doc/clamp become `[0, 4]` / `std::clamp(mode, 0, 4)`; update the `debugMode` member comment to list 4 = lights per fog block.

`deferred_renderer.h`:

```cpp
        /// @brief Sets how strongly point and spot lights scatter in the volumetric fog, clamped to [0, 8].
        void SetFogLightScattering(float strength) { m_volumetricFogPass->GetSettings().SetLightScatterStrength(strength); }
```

and update the `SetAtmosphereDebugMode` doc to `0 off, 1 scattered light, 2 transmittance, 3 density, 4 lights per fog block`.

`structured_buffer_d3d11.cpp` `BindToStage`, new case:

```cpp
		case ShaderType::ComputeShader:
			m_context.CSSetShaderResources(slot, 1, srvs);
			break;
```

- [ ] **Step 4: Constant buffer and `Render`**

`volumetric_fog_pass.cpp` `VolumetricFogConstants`, append after `volumeEnabled`:

```cpp
			uint32 lightCount;
			float lightScatterStrength;
			float lightPadding[2];
```

and change the assert to `== 240`.

`VolumetricFogCommon.hlsli`, append inside `VolumetricFogBuffer` after `VolumeEnabled`:

```hlsl
    uint LightCount;            // entries in the deferred renderer's light buffer (t9)
    float LightScatterStrength; // zone multiplier on point and spot light scattering
    float2 _LightPadding;
```

and update the `DebugMode` comment to include `4 lights per fog block`.

`volumetric_fog_pass.h` / `.cpp` — `Render` gains `StructuredBuffer& lights, uint32 lightCount` directly after `ConstantBuffer& cameraBuffer` (document both: "The deferred renderer's light buffer (ShaderLight entries)." / "Number of valid entries in lights."). Include `graphics/structured_buffer.h`. In `Render`:

```cpp
		constants.lightCount = lightCount;
		constants.lightScatterStrength = m_settings.lightScatterStrength;
		constants.lightPadding[0] = 0.0f;
		constants.lightPadding[1] = 0.0f;
```

and in the inject step, next to the other compute binds: `lights.BindToStage(ShaderType::ComputeShader, 9);` (the existing `ClearComputeBindings` afterwards clears t9).

`deferred_renderer.cpp` fog pass call (~447): pass `*m_lightStructuredBuffer, static_cast<uint32>(m_shaderLights.size())` after `*scene.GetCameraBuffer()` (use the real member names of the light buffer and the packed light vector).

- [ ] **Step 5: Culling helpers in `LightCommon.hlsli`**

Append before `#endif`:

```hlsl
// Conservative spot cone vs sphere test: never false for a sphere containing a lit point of the cone.
// direction must be normalized; cosHalfAngle is the outer cone cosine. Mirrors
// light_math::SpotConeIntersectsSphere.
bool SpotConeIntersectsSphere(float3 apex, float3 direction, float range, float cosHalfAngle, float3 center, float radius)
{
    float3 toCenter = center - apex;
    float lengthSquared = dot(toCenter, toCenter);
    float along = dot(toCenter, direction);
    float sinHalfAngle = sqrt(saturate(1.0f - cosHalfAngle * cosHalfAngle));
    float closest = cosHalfAngle * sqrt(max(lengthSquared - along * along, 0.0f)) - along * sinHalfAngle;

    bool outsideAngle = closest > radius;
    bool beyondRange = along > range + radius;
    bool behindApex = along < -radius;
    return !(outsideAngle || beyondRange || behindApex);
}

// Whether a point or spot light can reach any point of the sphere (xyz center, w radius) and
// scatters into fog at all.
bool LightReachesFogSphere(Light light, float4 sphere)
{
    if (light.Type == LIGHT_TYPE_DIRECTIONAL || light.FogScattering <= 0.0f)
    {
        return false;
    }

    float3 toCenter = sphere.xyz - light.Position;
    float reach = light.Range + sphere.w;
    if (dot(toCenter, toCenter) > reach * reach)
    {
        return false;
    }

    if (light.Type == LIGHT_TYPE_SPOT)
    {
        return SpotConeIntersectsSphere(light.Position, normalize(light.Direction), light.Range, light.SpotCosOuter, sphere.xyz, sphere.w);
    }

    return true;
}
```

- [ ] **Step 6: Culling and the light term in `CS_FogInject.hlsl`**

Add `#include "LightCommon.hlsli"` after `#include "VolumetricFogCommon.hlsli"`, the buffer and group-shared state after the other resources:

```hlsl
StructuredBuffer<Light> Lights : register(t9);

static const uint FOG_GROUP_SIZE = 8;
static const uint FOG_GROUP_THREADS = FOG_GROUP_SIZE * FOG_GROUP_SIZE * FOG_GROUP_SIZE;
static const uint MAX_BLOCK_LIGHTS = 64; // mirrors light_math::MaxLightsPerFogBlock

groupshared uint s_blockLightHits;                 // lights that reach the block (may exceed the capacity)
groupshared uint s_blockLights[MAX_BLOCK_LIGHTS];  // indices into Lights
```

Helpers (above `main`):

```hlsl
// World-space bounding sphere (xyz center, w radius) of one 8x8x8 froxel block: the sphere around the
// block's eight corners between its first and last depth-slice boundary. Every jittered sample of the
// block lies inside the convex hull of these corners.
float4 FroxelBlockSphere(uint3 groupId)
{
    uint3 first = groupId * FOG_GROUP_SIZE;
    uint3 last = min(first + FOG_GROUP_SIZE, uint3(GridWidth, GridHeight, GridDepth));

    float2 uvMin = float2(first.xy) / float2(GridWidth, GridHeight);
    float2 uvMax = float2(last.xy) / float2(GridWidth, GridHeight);
    float nearDepth = SliceToDepth(float(first.z) / float(GridDepth));
    float farDepth = SliceToDepth(float(last.z) / float(GridDepth));

    float3 corners[8];
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        float2 uv = float2((i & 1) ? uvMax.x : uvMin.x, (i & 2) ? uvMax.y : uvMin.y);
        float depth = (i & 4) ? farDepth : nearDepth;
        corners[i] = FogWorldPosition(FogWorldRay(uv), depth);
    }

    float3 center = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (uint j = 0; j < 8; ++j)
    {
        center += corners[j];
    }
    center *= 0.125f;

    float radiusSquared = 0.0f;
    [unroll]
    for (uint k = 0; k < 8; ++k)
    {
        float3 offset = corners[k] - center;
        radiusSquared = max(radiusSquared, dot(offset, offset));
    }

    return float4(center, sqrt(radiusSquared));
}

// Radiance scattered toward the camera by the block's lights. ray: camera-to-froxel direction.
float3 BlockLightScattering(float3 worldPos, float3 ray, uint blockLightCount)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < blockLightCount; ++i)
    {
        Light light = Lights[s_blockLights[i]];
        float3 toFroxel = worldPos - light.Position;
        float distance = length(toFroxel);
        float attenuation = LightAttenuation(distance, light.Range);
        if (attenuation <= 0.0f)
        {
            continue;
        }

        float3 lightToFroxel = toFroxel / max(distance, 1e-4f);
        if (light.Type == LIGHT_TYPE_SPOT)
        {
            attenuation *= SpotFactor(light, lightToFroxel);
        }

        // Light travelling along lightToFroxel scatters toward the camera (-ray).
        sum += light.Color * (light.Intensity * attenuation * light.FogScattering) * ScatterPhase(dot(ray, -lightToFroxel));
    }

    return sum * LightScatterStrength;
}
```

Check `ScatterPhase` and `FogWorldPosition` / `FogWorldRay` signatures in `AtmosphereCommon.hlsli` / `VolumetricFogCommon.hlsli` and adapt the calls.

Replace `main` with (group sync must be reached by every thread, so the grid bounds check moves after the barriers):

```hlsl
[numthreads(FOG_GROUP_SIZE, FOG_GROUP_SIZE, FOG_GROUP_SIZE)]
void main(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // 1. Cull the frame's lights against this block once, in parallel.
    if (groupIndex == 0)
    {
        s_blockLightHits = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    float4 blockSphere = FroxelBlockSphere(groupId);
    for (uint lightIndex = groupIndex; lightIndex < LightCount; lightIndex += FOG_GROUP_THREADS)
    {
        if (LightReachesFogSphere(Lights[lightIndex], blockSphere))
        {
            uint slot;
            InterlockedAdd(s_blockLightHits, 1, slot);
            if (slot < MAX_BLOCK_LIGHTS)
            {
                s_blockLights[slot] = lightIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    uint blockLightCount = min(s_blockLightHits, MAX_BLOCK_LIGHTS);

    if (DebugMode == 4)
    {
        // Lights per block: black none, blue one .. yellow sixteen or more, red over capacity.
        float3 color = float3(0.0f, 0.0f, 0.0f);
        if (s_blockLightHits > MAX_BLOCK_LIGHTS)
        {
            color = float3(1.0f, 0.0f, 0.0f);
        }
        else if (blockLightCount > 0)
        {
            color = lerp(float3(0.0f, 0.2f, 1.0f), float3(1.0f, 1.0f, 0.0f), saturate(float(blockLightCount - 1) / 15.0f));
        }

        const float debugSigma = 0.02f;
        InjectOutput[id] = float4(color * debugSigma, debugSigma);
        return;
    }

    // 2. This froxel.
    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float viewDepth = SliceToDepth((float(id.z) + Jitter) / float(GridDepth));
    float3 worldPos = FogWorldPosition(ray, viewDepth);

    // The pattern moves with the wind: the noise at p now is the noise that was at p - offset.
    float3 noiseUvw = float3(worldPos.x / NoiseSize - WindOffset.x, worldPos.y / NoiseSize, worldPos.z / NoiseSize - WindOffset.y);
    float noise = NoiseVolume.SampleLevel(NoiseSampler, noiseUvw, 0.0f);

    float sigma = FogDensityAt(worldPos.y) * NoiseDensityFactor(noise, NoiseAmount);
    float visibility = SampleSunVisibility(worldPos, viewDepth);
    float3 radiance = FogSource(dot(ray, SunDirection), visibility);
    radiance += BlockLightScattering(worldPos, ray, blockLightCount);

    InjectOutput[id] = float4(radiance * sigma, sigma);
}
```

Update the file's header comment to mention point and spot lights.

`PS_FogComposite.hlsl`: change `if (DebugMode == 1)` to `if (DebugMode == 1 || DebugMode == 4)`.

- [ ] **Step 7: Docs and cvar help**

- `world_state.cpp` `gxAtmosphereDebug` help: `"Fog debug view: 0 = off, 1 = scattered light, 2 = transmittance, 3 = fog density, 4 = lights per fog block."`
- `docs/console_commands.md`: same text for `gxAtmosphereDebug`.
- `docs/rendering-atmosphere.md`: in the `gxAtmosphereDebug` table row add `4 lights per fog block`; add a "Point and spot lights" subsection under the fog model describing: all point and spot lights scatter; per-light `fogScattering` (world model lights, spell/projectile `fog_scattering`); per-zone `light_scattering` (Task 7); per-8x8x8-block culling into a 64-entry group-shared list; attenuation and cone shared with the lighting pass through `LightCommon.hlsli` / `scene_graph/light_math.h`; no shadows, so lights inside buildings should use `fogScattering` 0; the temporal blend smears fast-moving lights.

- [ ] **Step 8: Build and run the tests**

Run: `cmake --build build --config Debug -t deferred_shading_tests scene_graph_tests mmo_client mmo_edit`
Expected: builds (shader compile errors fail the build); `bin/Debug/deferred_shading_tests.exe` and `bin/Debug/scene_graph_tests.exe` all pass.

- [ ] **Step 9: Commit**

```bash
git add src/shared/graphics_d3d11/structured_buffer_d3d11.cpp src/shared/deferred_shading/volumetric_fog_settings.h src/shared/deferred_shading/volumetric_fog_pass.h src/shared/deferred_shading/volumetric_fog_pass.cpp src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli src/shared/deferred_shading/shaders/LightCommon.hlsli src/shared/deferred_shading/shaders/CS_FogInject.hlsl src/shared/deferred_shading/shaders/PS_FogComposite.hlsl src/mmo_client/game_states/world_state.cpp docs/rendering-atmosphere.md docs/console_commands.md src/tests/deferred_shading_tests/test_volumetric_fog_settings.cpp
git commit -m "feat(render): point and spot lights scatter into the froxel fog"
```

---

### Task 7: Per-zone light scattering strength

**Files:**
- Modify: `src/shared/proto_data/environment_profiles.proto`, `src/shared/client_data/environment_profiles.proto`
- Modify: `src/shared/scene_graph/environment_profile.h`, `environment_profile_proto.h`
- Modify: `src/shared/scene_graph/environment_state.h`, `environment_state.cpp`
- Modify: `src/mmo_edit/editor_windows/environment_profile_editor_window.cpp`
- Modify: `src/mmo_client/game_states/world_state.cpp` (~6268), `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` (~1392)
- Modify: `docs/rendering-atmosphere.md` (profile field list)
- Test: `src/tests/scene_graph_tests/test_environment_light_scattering.cpp`

**Interfaces:**
- Consumes: `DeferredRenderer::SetFogLightScattering(float)` (Task 6).
- Produces: `EnvironmentProfile::lightScattering`, `EnvironmentState::lightScattering` (default 1).

Every place that handles `fogNoiseAmount` / `fog_noise_amount` handles the new value the same way. Find them with `grep -rn "fogNoiseAmount\|fog_noise_amount" src/` and cover each hit (profile struct, proto loader, any profile-to-proto writer, state struct, `EvaluateEnvironment`, `LerpEnvironment`, editor window).

- [ ] **Step 1: Write the failing test**

Model it on `src/tests/scene_graph_tests/test_environment_wind_fields.cpp` (same includes and helpers):

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

using namespace mmo;

TEST_CASE("Light scattering defaults to one", "[environment]")
{
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
	CHECK(profile.lightScattering == Approx(1.0f));
	CHECK(EvaluateEnvironment(profile, 0.5f).lightScattering == Approx(1.0f));
}

TEST_CASE("Light scattering loads clamped from the profile record", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(7);
	record.set_name("Test");
	CHECK(record.light_scattering() == Approx(1.0f));

	record.set_light_scattering(20.0f);
	CHECK(LoadEnvironmentProfile(record).lightScattering == Approx(8.0f));

	record.set_light_scattering(-1.0f);
	CHECK(LoadEnvironmentProfile(record).lightScattering == Approx(0.0f));

	record.set_light_scattering(2.5f);
	CHECK(LoadEnvironmentProfile(record).lightScattering == Approx(2.5f));
}

TEST_CASE("Light scattering blends linearly between profiles", "[environment]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.lightScattering = 1.0f;
	b.lightScattering = 3.0f;
	CHECK(LerpEnvironment(a, b, 0.5f).lightScattering == Approx(2.0f));
}
```

Use the loader's real name and record type as `test_environment_wind_fields.cpp` does (it may be a template over the proto type).

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, no member `lightScattering`.

- [ ] **Step 3: Implement**

Both protos, after `fog_noise_size = 24`:

```proto
	// Multiplier on how strongly point and spot lights glow in the volumetric fog.
	optional float light_scattering = 25 [default = 1];
```

- `environment_profile.h`: `/// @brief Multiplier on point and spot light scattering in the fog.` `float lightScattering = 1.0f;`
- `environment_profile_proto.h` loader: `result.lightScattering = std::clamp(profile.light_scattering(), 0.0f, 8.0f);` (and the reverse direction if a profile-to-proto writer exists).
- `environment_state.h`: `float lightScattering = 1.0f;` with a doc comment.
- `environment_state.cpp`: `state.lightScattering = profile.lightScattering;` in `EvaluateEnvironment`; `state.lightScattering = lerpFloat(a.lightScattering, b.lightScattering, t);` in `LerpEnvironment`.
- Editor window, in the Fog section after "Fog Base Height":

```cpp
			float lightScattering = entry.light_scattering();
			if (ImGui::DragFloat("Light Scattering", &lightScattering, 0.05f, 0.0f, 8.0f, "%.2f"))
			{
				entry.set_light_scattering(std::clamp(lightScattering, 0.0f, 8.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How strongly lanterns, torches and spell lights glow in this zone's fog. 1 = default.");
			}
```

- Client `world_state.cpp` next to `renderer->SetBloomThreshold(state.bloomThreshold);`: `renderer->SetFogLightScattering(state.lightScattering);`
- Editor `world_editor_instance.cpp` next to its `SetBloomThreshold`: the same line.
- `docs/rendering-atmosphere.md`: add `light scattering` to the profile's wind-and-noise / fog field list.

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build build --config Debug -t scene_graph_tests client_data_tests mmo_client mmo_edit`
Expected: builds; both test executables pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/proto_data/environment_profiles.proto src/shared/client_data/environment_profiles.proto src/shared/scene_graph/environment_profile.h src/shared/scene_graph/environment_profile_proto.h src/shared/scene_graph/environment_state.h src/shared/scene_graph/environment_state.cpp src/mmo_edit/editor_windows/environment_profile_editor_window.cpp src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_editor_instance.cpp docs/rendering-atmosphere.md src/tests/scene_graph_tests/test_environment_light_scattering.cpp
git commit -m "feat(render): zones set how strongly lights glow in their fog"
```

---

## Controller verification (after all tasks)

Not a subagent task. After the final whole-branch review:

1. Full gate: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1` (servers stopped).
2. Client capture (desktop visible, temporary uncommitted `MMO_FORCE_GAMETIME_MS` override, reverted afterwards): Oakenshire at night under Coastal Sea Fog — lantern halos; `gxAtmosphereDebug 4` via Config.cfg.
3. Spot check: give one Oakenshire world model a spot light (format 2.1) for the check, confirm cone on the ground and in fog, then decide with the user whether the content change stays.
4. Append implementation notes to the spec.
