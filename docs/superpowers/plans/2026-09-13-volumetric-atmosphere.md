# Volumetric Atmosphere Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add height fog, shadowed ray-marched light shafts and bloom to the D3D11 deferred renderer, on top of a linear-HDR frame with a final tonemap pass.

**Architecture:** Lighting and forward materials stop tonemapping and write linear HDR. A new `AtmospherePass` marches the CSM shadow maps at reduced resolution, blurs and bilaterally upsamples the result, and composites analytic height fog plus shafts onto the opaque scene. A `BloomPass` builds a downsample/upsample chain and a `TonemapPass` applies bloom, exposure, ACES, gamma and dither. Fog parameters are global cvars multiplied by two new time-of-day colour curves that `SkyComponent` evaluates into `Scene`.

**Tech Stack:** C++17, HLSL SM5 (MSBuild FxCompile via CMake), Catch2, Lua/XML frame UI, ImGui editor.

**Spec:** [docs/superpowers/specs/2026-09-13-volumetric-atmosphere-design.md](../specs/2026-09-13-volumetric-atmosphere-design.md). Read its "Implementation Notes" section first.

## Global Constraints

- Branch `feature/volumetric-atmosphere`; never push; merge only through `/gate` then `/ship`.
- Code style: Allman braces, braces on every `if`, tabs in C++ (the existing HLSL files use 4 spaces; match that in HLSL), `m_camelCase` members, `PascalCase` methods, `snake_case` files, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new source file (HLSL included).
- No exceptions; use `ASSERT` / `VERIFY` / `ELOG` / `WLOG`.
- D3D11 only. Every new pass keeps its bytecode `#ifdef _WIN32` seam exactly like `ssao_pass.cpp`, so non-Windows builds still compile.
- Settings structs used by tests must depend on nothing but `base/typedefs.h` (the headless `deferred_shading_tests` links only `base` and `math`).
- Every generated shader header `src/shared/deferred_shading/shaders/PS_<Name>.h` gets a line in `.gitignore` next to the existing ones (lines 60-65).
- Nothing before the TonemapPass may tonemap or gamma-encode (except standalone forward rendering, see spec note 1).
- New localized strings go into all four `data/client/Locales/Locale_*/Localization.txt`.
- `data/client` and `data/editor` are submodules that already had uncommitted changes before this branch started. When committing material rebuilds, stage only the files the rebuild changed and ask the user before committing inside a submodule.
- After adding source files, re-run `cmake -S . -B build` (the build uses globs).

## File Structure

| File | Responsibility |
|---|---|
| `src/shared/deferred_shading/atmosphere_math.h` (new) | C++ mirror of the HLSL tonemap / inverse tonemap / height-fog / phase functions, for tests |
| `src/shared/scene_graph/atmosphere_settings.h` (new) | `AtmosphereParameters` (tunable), `AtmosphereTimeOfDay` (curve output), `CombineAtmosphere` |
| `src/shared/deferred_shading/atmosphere_pass_settings.h` (new) | quality presets for the march |
| `src/shared/deferred_shading/bloom_settings.h` (new) | quality presets, intensity, threshold |
| `src/shared/deferred_shading/tonemap_settings.h` (new) | exposure, dither |
| `src/shared/deferred_shading/tonemap_pass.{h,cpp}` + `shaders/PS_Tonemap.hlsl` (new) | final tonemap + bloom composite |
| `src/shared/deferred_shading/shaders/AtmosphereCommon.hlsli` (new) | b1/b12 declarations + fog functions shared by lighting and atmosphere shaders |
| `src/shared/deferred_shading/atmosphere_pass.{h,cpp}` + `shaders/PS_AtmosphereMarch.hlsl`, `PS_AtmosphereBlur.hlsl`, `PS_AtmosphereComposite.hlsl` (new) | fog + shafts |
| `src/shared/deferred_shading/bloom_pass.{h,cpp}` + `shaders/PS_BloomDownsample.hlsl`, `PS_BloomUpsample.hlsl` (new) | bloom chain |
| `src/shared/deferred_shading/deferred_renderer.{h,cpp}` | pass wiring, setters, GPU timers |
| `src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl` | linear output, include common header, drop legacy fog |
| `src/shared/deferred_shading/CMakeLists.txt` | stop compiling `.hlsli` as shaders |
| `src/shared/scene_graph/scene.{h,cpp}` | camera cbuffer layout, atmosphere state, forward output flag |
| `src/shared/graphics_d3d11/material_compiler_d3d11.cpp` | forward fog + conditional tonemap + inverse tonemap |
| `src/shared/graphics/sky_component.{h,cpp}` | `FogColor.hccv` / `SunScatter.hccv` curves |
| `src/mmo_client/game_states/world_state.{h,cpp}` | cvars |
| `src/mmo_edit/editors/world_editor/world_editor_instance.cpp`, `world_settings_panel.cpp` | legacy fog call sites, sliders |
| `data/client/Interface/GameUI/OptionsFrame.lua`, locales | options menu |
| `src/tests/deferred_shading_tests/test_atmosphere_*.cpp`, `test_bloom_settings.cpp`, `test_tonemap_settings.cpp` (new) | unit tests |
| `tools/render_compare/capture_client.ps1`, `compare_screenshots.py` (new) | regression checkpoint helpers |
| `docs/rendering-atmosphere.md` (new), `docs/console_commands.md` | documentation |

---

### Task 1: Atmosphere math mirror

**Files:**
- Create: `src/shared/deferred_shading/atmosphere_math.h`
- Test: `src/tests/deferred_shading_tests/test_atmosphere_math.cpp`

**Interfaces:**
- Produces (namespace `mmo::atmosphere`): `float AcesFilm(float)`, `float Tonemap(float linear)`, `float InverseAcesFilm(float)`, `float InverseTonemap(float display)`, `constexpr float MaxInvertibleDisplay = 0.999f`, `constexpr float MaxDensityExponent = 12.0f`, `float FogDensityAt(float y, float density, float falloff, float baseHeight)`, `float FogOpticalDepth(float originY, float dirY, float length, float density, float falloff, float baseHeight)`, `float ScatterPhase(float anisotropy, float cosTheta)`. Tasks 3-5 copy these formulas verbatim into HLSL.

- [ ] **Step 1: Write the failing test**

`src/tests/deferred_shading_tests/test_atmosphere_math.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/atmosphere_math.h"

#include <cmath>

using namespace mmo::atmosphere;

namespace
{
	/// Midpoint-rule optical depth in double precision, used as ground truth.
	double NumericOpticalDepth(double originY, double dirY, double length, double density, double falloff, double baseHeight)
	{
		constexpr int steps = 20000;
		const double dt = length / steps;
		double sum = 0.0;
		for (int i = 0; i < steps; ++i)
		{
			const double y = originY + dirY * (i + 0.5) * dt;
			sum += density * std::exp(-falloff * (y - baseHeight)) * dt;
		}
		return sum;
	}
}

TEST_CASE("Inverse tonemap round-trips every invertible display value", "[atmosphere]")
{
	for (int i = 0; i <= 999; ++i)
	{
		const float display = static_cast<float>(i) / 1000.0f;
		const float linear = InverseTonemap(display);
		REQUIRE(linear >= 0.0f);
		REQUIRE(Tonemap(linear) == Approx(display).margin(5e-4));
	}
}

TEST_CASE("Inverse tonemap maps black to black and is monotonic", "[atmosphere]")
{
	REQUIRE(InverseTonemap(0.0f) == Approx(0.0f).margin(1e-6));

	float previous = -1.0f;
	for (int i = 0; i <= 999; ++i)
	{
		const float linear = InverseTonemap(static_cast<float>(i) / 1000.0f);
		REQUIRE(linear > previous);
		previous = linear;
	}
}

TEST_CASE("Inverse tonemap clamps display values above the invertible range", "[atmosphere]")
{
	REQUIRE(InverseTonemap(1.5f) == Approx(InverseTonemap(MaxInvertibleDisplay)));
	REQUIRE(std::isfinite(InverseTonemap(1.0f)));
}

TEST_CASE("Closed-form height fog optical depth matches numeric integration", "[atmosphere]")
{
	const float originYs[] = { 0.0f, 12.0f, 40.0f };
	const float dirYs[] = { -0.3f, -0.05f, 0.0f, 0.2f, 0.9f };
	const float lengths[] = { 1.0f, 60.0f, 300.0f };
	const float falloffs[] = { 0.0f, 0.05f, 0.15f };
	constexpr float density = 0.02f;
	constexpr float baseHeight = 0.0f;

	for (const float originY : originYs)
	{
		for (const float dirY : dirYs)
		{
			for (const float length : lengths)
			{
				for (const float falloff : falloffs)
				{
					// The closed form is exact only where the density exponent clamp is inactive.
					const float lowestY = std::min(originY, originY + dirY * length);
					if (-falloff * (lowestY - baseHeight) > MaxDensityExponent)
					{
						continue;
					}

					const double expected = NumericOpticalDepth(originY, dirY, length, density, falloff, baseHeight);
					const float actual = FogOpticalDepth(originY, dirY, length, density, falloff, baseHeight);

					INFO("originY=" << originY << " dirY=" << dirY << " length=" << length << " falloff=" << falloff);
					REQUIRE(actual == Approx(expected).epsilon(0.01).margin(1e-6));
				}
			}
		}
	}
}

TEST_CASE("Fog density clamps its exponent far below the base height", "[atmosphere]")
{
	const float deep = FogDensityAt(-10000.0f, 0.02f, 0.05f, 0.0f);
	REQUIRE(std::isfinite(deep));
	REQUIRE(deep == Approx(0.02f * std::exp(MaxDensityExponent)));
}

TEST_CASE("Scatter phase is isotropic at zero anisotropy and forward-peaked otherwise", "[atmosphere]")
{
	REQUIRE(ScatterPhase(0.0f, 1.0f) == Approx(1.0f));
	REQUIRE(ScatterPhase(0.0f, -1.0f) == Approx(1.0f));

	const float towardSun = ScatterPhase(0.7f, 1.0f);
	const float awayFromSun = ScatterPhase(0.7f, -1.0f);
	REQUIRE(towardSun > 10.0f);
	REQUIRE(awayFromSun < 1.0f);
	REQUIRE(awayFromSun > 0.0f);
}
```

- [ ] **Step 2: Run it to verify it fails**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t deferred_shading_tests
```

Expected: compile error `Cannot open include file: 'deferred_shading/atmosphere_math.h'`.

- [ ] **Step 3: Write the implementation**

`src/shared/deferred_shading/atmosphere_math.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <algorithm>
#include <cmath>

namespace mmo::atmosphere
{
	/// @brief Largest display value InverseTonemap inverts. ACES reaches 1.0 at a finite input,
	///        so 1.0 itself has no stable inverse.
	constexpr float MaxInvertibleDisplay = 0.999f;

	/// @brief Upper bound of the height-fog density exponent. Keeps rays that reach far below the
	///        base height from overflowing floating point.
	constexpr float MaxDensityExponent = 12.0f;

	/// @brief Display gamma used by the tonemap pass and the forward materials.
	constexpr float Gamma = 2.2f;

	/// @brief Narkowicz ACES fit, identical to ACESFilm in the HLSL shaders.
	inline float AcesFilm(const float x)
	{
		constexpr float a = 2.51f;
		constexpr float b = 0.03f;
		constexpr float c = 2.43f;
		constexpr float d = 0.59f;
		constexpr float e = 0.14f;
		return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
	}

	/// @brief Linear HDR to display value: ACES then gamma.
	inline float Tonemap(const float linear)
	{
		return std::pow(AcesFilm(linear), 1.0f / Gamma);
	}

	/// @brief Analytic inverse of AcesFilm for y in [0, a/c).
	/// @remark Solves (y·c − a)x² + (y·d − b)x + y·e = 0. The leading coefficient is negative and
	///         the constant term non-negative, so exactly one root is non-negative.
	inline float InverseAcesFilm(const float y)
	{
		constexpr float a = 2.51f;
		constexpr float b = 0.03f;
		constexpr float c = 2.43f;
		constexpr float d = 0.59f;
		constexpr float e = 0.14f;
		const float qa = y * c - a;
		const float qb = y * d - b;
		const float qc = y * e;
		return (-qb - std::sqrt(std::max(qb * qb - 4.0f * qa * qc, 0.0f))) / (2.0f * qa);
	}

	/// @brief Display value to the linear HDR value Tonemap maps back onto it.
	inline float InverseTonemap(const float display)
	{
		const float clamped = std::clamp(display, 0.0f, MaxInvertibleDisplay);
		return InverseAcesFilm(std::pow(clamped, Gamma));
	}

	/// @brief Extinction coefficient of the exponential height fog at world height y.
	inline float FogDensityAt(const float y, const float density, const float falloff, const float baseHeight)
	{
		return density * std::exp(std::min(-falloff * (y - baseHeight), MaxDensityExponent));
	}

	/// @brief Closed-form optical depth of the height fog along a straight ray segment.
	/// @param originY World height of the segment start.
	/// @param dirY Y component of the normalised ray direction.
	/// @param length Segment length in metres.
	/// @remark With σ(t) = σ_start·e^(−k·t/L) and k = falloff·dirY·L, the integral is
	///         L·(σ_start − σ_end)/k. Near k = 0 the trapezoid rule is used instead, which is exact
	///         to O(k²) and avoids the cancellation in the difference.
	inline float FogOpticalDepth(const float originY, const float dirY, const float length,
		const float density, const float falloff, const float baseHeight)
	{
		const float sigmaStart = FogDensityAt(originY, density, falloff, baseHeight);
		const float sigmaEnd = FogDensityAt(originY + dirY * length, density, falloff, baseHeight);
		const float k = falloff * dirY * length;
		if (std::abs(k) < 1e-3f)
		{
			return length * 0.5f * (sigmaStart + sigmaEnd);
		}

		return length * (sigmaStart - sigmaEnd) / k;
	}

	/// @brief Scattering phase normalised so an isotropic medium yields 1: a Henyey-Greenstein lobe
	///        blended 80/20 with the isotropic term.
	/// @param anisotropy Henyey-Greenstein g in [0, 1).
	/// @param cosTheta Cosine between the view ray and the direction toward the sun.
	inline float ScatterPhase(const float anisotropy, const float cosTheta)
	{
		const float g2 = anisotropy * anisotropy;
		const float hg = (1.0f - g2) / std::pow(std::max(1.0f + g2 - 2.0f * anisotropy * cosTheta, 1e-4f), 1.5f);
		return 1.0f + (hg - 1.0f) * 0.8f;
	}
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t deferred_shading_tests
.\bin\Debug\deferred_shading_tests.exe "[atmosphere]"
```

Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/shared/deferred_shading/atmosphere_math.h src/tests/deferred_shading_tests/test_atmosphere_math.cpp
git commit -m "feat(render): atmosphere math reference for tonemap and height fog"
```

---

### Task 2: Settings structs

**Files:**
- Create: `src/shared/scene_graph/atmosphere_settings.h`
- Create: `src/shared/deferred_shading/atmosphere_pass_settings.h`
- Create: `src/shared/deferred_shading/bloom_settings.h`
- Create: `src/shared/deferred_shading/tonemap_settings.h`
- Test: `src/tests/deferred_shading_tests/test_atmosphere_settings.cpp`, `test_atmosphere_pass_settings.cpp`, `test_bloom_settings.cpp`, `test_tonemap_settings.cpp`

**Interfaces:**
- Produces:
  - `struct AtmosphereParameters { float density=0.02f; float heightFalloff=0.05f; float baseHeight=0.0f; float anisotropy=0.7f; float shaftStrength=1.0f; void SetDensity(float); void SetHeightFalloff(float); void SetBaseHeight(float); void SetAnisotropy(float); void SetShaftStrength(float); }`
  - `struct AtmosphereTimeOfDay { float fogTint[3]{0.447f,0.638f,1.0f}; float densityMultiplier=1.0f; float sunScatterColor[3]{1,1,1}; float shaftMultiplier=1.0f; }`
  - `struct AtmosphereConstants { float density; float heightFalloff; float baseHeight; float anisotropy; float fogTint[3]; float sunScatterColor[3]; float shaftStrength; }`
  - `AtmosphereConstants CombineAtmosphere(const AtmosphereParameters&, const AtmosphereTimeOfDay&, bool fogEnabled)`
  - `struct AtmospherePassSettings { int32 qualityLevel=3; uint32 resolutionDivisor=2; uint32 stepCount=48; uint32 blurIterations=2; float marchDistance=200.0f; float skyDistance=2000.0f; uint32 debugMode=0; static constexpr float MaxMarchDistance=300.0f; void ApplyQualityLevel(int); void SetMarchDistance(float); void SetDebugMode(int); bool IsMarchEnabled() const; }`
  - `struct BloomSettings { int32 qualityLevel=2; uint32 startDivisor=2; uint32 levelCount=6; float intensity=0.08f; float threshold=0.8f; float knee=0.5f; void ApplyQualityLevel(int); void SetIntensity(float); void SetThreshold(float); bool IsEnabled() const; }`
  - `struct TonemapSettings { float exposure=1.0f; float ditherStrength=0.5f; void SetExposure(float); }`

- [ ] **Step 1: Write the failing tests**

`src/tests/deferred_shading_tests/test_atmosphere_settings.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/atmosphere_settings.h"

using namespace mmo;

TEST_CASE("AtmosphereParameters defaults match the design spec", "[atmosphere]")
{
	const AtmosphereParameters parameters;
	REQUIRE(parameters.density == Approx(0.02f));
	REQUIRE(parameters.heightFalloff == Approx(0.05f));
	REQUIRE(parameters.baseHeight == Approx(0.0f));
	REQUIRE(parameters.anisotropy == Approx(0.7f));
	REQUIRE(parameters.shaftStrength == Approx(1.0f));
}

TEST_CASE("AtmosphereParameters setters clamp to sane ranges", "[atmosphere]")
{
	AtmosphereParameters parameters;

	parameters.SetDensity(-1.0f);
	REQUIRE(parameters.density == Approx(0.0f));
	parameters.SetDensity(5.0f);
	REQUIRE(parameters.density == Approx(1.0f));

	parameters.SetHeightFalloff(-0.5f);
	REQUIRE(parameters.heightFalloff == Approx(0.0f));
	parameters.SetHeightFalloff(3.0f);
	REQUIRE(parameters.heightFalloff == Approx(1.0f));

	parameters.SetBaseHeight(-50000.0f);
	REQUIRE(parameters.baseHeight == Approx(-10000.0f));

	// g >= 1 makes Henyey-Greenstein singular.
	parameters.SetAnisotropy(1.0f);
	REQUIRE(parameters.anisotropy == Approx(0.95f));
	parameters.SetAnisotropy(-0.3f);
	REQUIRE(parameters.anisotropy == Approx(0.0f));

	parameters.SetShaftStrength(100.0f);
	REQUIRE(parameters.shaftStrength == Approx(16.0f));
}

TEST_CASE("CombineAtmosphere multiplies base values by the time-of-day curve", "[atmosphere]")
{
	AtmosphereParameters parameters;
	parameters.density = 0.02f;
	parameters.shaftStrength = 2.0f;

	AtmosphereTimeOfDay timeOfDay;
	timeOfDay.densityMultiplier = 2.5f;
	timeOfDay.shaftMultiplier = 0.5f;
	timeOfDay.fogTint[0] = 0.9f;
	timeOfDay.fogTint[1] = 0.6f;
	timeOfDay.fogTint[2] = 0.4f;
	timeOfDay.sunScatterColor[0] = 1.0f;
	timeOfDay.sunScatterColor[1] = 0.7f;
	timeOfDay.sunScatterColor[2] = 0.4f;

	const AtmosphereConstants constants = CombineAtmosphere(parameters, timeOfDay, true);
	REQUIRE(constants.density == Approx(0.05f));
	REQUIRE(constants.shaftStrength == Approx(1.0f));
	REQUIRE(constants.heightFalloff == Approx(parameters.heightFalloff));
	REQUIRE(constants.baseHeight == Approx(parameters.baseHeight));
	REQUIRE(constants.anisotropy == Approx(parameters.anisotropy));
	REQUIRE(constants.fogTint[0] == Approx(0.9f));
	REQUIRE(constants.fogTint[2] == Approx(0.4f));
	REQUIRE(constants.sunScatterColor[1] == Approx(0.7f));
}

TEST_CASE("CombineAtmosphere zeroes density when fog is disabled", "[atmosphere]")
{
	const AtmosphereConstants constants = CombineAtmosphere(AtmosphereParameters(), AtmosphereTimeOfDay(), false);
	REQUIRE(constants.density == Approx(0.0f));
}

TEST_CASE("CombineAtmosphere never lets a curve overshoot make values negative", "[atmosphere]")
{
	AtmosphereTimeOfDay timeOfDay;
	timeOfDay.densityMultiplier = -0.2f;
	timeOfDay.shaftMultiplier = -1.0f;
	timeOfDay.fogTint[1] = -0.1f;
	timeOfDay.sunScatterColor[2] = -0.3f;

	const AtmosphereConstants constants = CombineAtmosphere(AtmosphereParameters(), timeOfDay, true);
	REQUIRE(constants.density == Approx(0.0f));
	REQUIRE(constants.shaftStrength == Approx(0.0f));
	REQUIRE(constants.fogTint[1] == Approx(0.0f));
	REQUIRE(constants.sunScatterColor[2] == Approx(0.0f));
}
```

`src/tests/deferred_shading_tests/test_atmosphere_pass_settings.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/atmosphere_pass_settings.h"

using namespace mmo;

TEST_CASE("AtmospherePassSettings defaults to High quality", "[atmosphere]")
{
	const AtmospherePassSettings settings;
	REQUIRE(settings.qualityLevel == 3);
	REQUIRE(settings.resolutionDivisor == 2);
	REQUIRE(settings.stepCount == 48);
	REQUIRE(settings.blurIterations == 2);
	REQUIRE(settings.marchDistance == Approx(200.0f));
	REQUIRE(settings.skyDistance == Approx(2000.0f));
	REQUIRE(settings.debugMode == 0);
	REQUIRE(settings.IsMarchEnabled());
}

TEST_CASE("AtmospherePassSettings quality levels map to resolution, steps and blur", "[atmosphere]")
{
	AtmospherePassSettings settings;

	settings.ApplyQualityLevel(0);
	REQUIRE(settings.qualityLevel == 0);
	REQUIRE(settings.stepCount == 0);
	REQUIRE(settings.blurIterations == 0);
	REQUIRE(settings.resolutionDivisor == 1);
	REQUIRE_FALSE(settings.IsMarchEnabled());

	settings.ApplyQualityLevel(1);
	REQUIRE(settings.resolutionDivisor == 4);
	REQUIRE(settings.stepCount == 12);
	REQUIRE(settings.blurIterations == 1);

	settings.ApplyQualityLevel(2);
	REQUIRE(settings.resolutionDivisor == 2);
	REQUIRE(settings.stepCount == 24);
	REQUIRE(settings.blurIterations == 1);

	settings.ApplyQualityLevel(3);
	REQUIRE(settings.resolutionDivisor == 2);
	REQUIRE(settings.stepCount == 48);
	REQUIRE(settings.blurIterations == 2);

	settings.ApplyQualityLevel(4);
	REQUIRE(settings.resolutionDivisor == 1);
	REQUIRE(settings.stepCount == 64);
	REQUIRE(settings.blurIterations == 2);
}

TEST_CASE("AtmospherePassSettings clamps out-of-range input", "[atmosphere]")
{
	AtmospherePassSettings settings;

	settings.ApplyQualityLevel(-3);
	REQUIRE(settings.qualityLevel == 0);
	settings.ApplyQualityLevel(42);
	REQUIRE(settings.qualityLevel == 4);

	settings.SetMarchDistance(1000.0f);
	REQUIRE(settings.marchDistance == Approx(AtmospherePassSettings::MaxMarchDistance));
	settings.SetMarchDistance(-5.0f);
	REQUIRE(settings.marchDistance == Approx(0.0f));

	settings.SetDebugMode(7);
	REQUIRE(settings.debugMode == 3);
	settings.SetDebugMode(-1);
	REQUIRE(settings.debugMode == 0);
}
```

`src/tests/deferred_shading_tests/test_bloom_settings.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/bloom_settings.h"

using namespace mmo;

TEST_CASE("BloomSettings defaults to High quality", "[bloom]")
{
	const BloomSettings settings;
	REQUIRE(settings.qualityLevel == 2);
	REQUIRE(settings.startDivisor == 2);
	REQUIRE(settings.levelCount == 6);
	REQUIRE(settings.intensity == Approx(0.08f));
	REQUIRE(settings.threshold == Approx(0.8f));
	REQUIRE(settings.knee == Approx(0.5f));
	REQUIRE(settings.IsEnabled());
}

TEST_CASE("BloomSettings quality levels map to start resolution and level count", "[bloom]")
{
	BloomSettings settings;

	settings.ApplyQualityLevel(0);
	REQUIRE(settings.levelCount == 0);
	REQUIRE_FALSE(settings.IsEnabled());

	settings.ApplyQualityLevel(1);
	REQUIRE(settings.startDivisor == 4);
	REQUIRE(settings.levelCount == 4);

	settings.ApplyQualityLevel(2);
	REQUIRE(settings.startDivisor == 2);
	REQUIRE(settings.levelCount == 6);

	settings.ApplyQualityLevel(9);
	REQUIRE(settings.qualityLevel == 2);
	settings.ApplyQualityLevel(-9);
	REQUIRE(settings.qualityLevel == 0);
}

TEST_CASE("BloomSettings clamps intensity and threshold", "[bloom]")
{
	BloomSettings settings;
	settings.SetIntensity(-1.0f);
	REQUIRE(settings.intensity == Approx(0.0f));
	settings.SetIntensity(3.0f);
	REQUIRE(settings.intensity == Approx(1.0f));
	settings.SetThreshold(-2.0f);
	REQUIRE(settings.threshold == Approx(0.0f));
	settings.SetThreshold(100.0f);
	REQUIRE(settings.threshold == Approx(16.0f));
}
```

`src/tests/deferred_shading_tests/test_tonemap_settings.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/tonemap_settings.h"

using namespace mmo;

TEST_CASE("TonemapSettings defaults leave the image unchanged", "[tonemap]")
{
	const TonemapSettings settings;
	REQUIRE(settings.exposure == Approx(1.0f));
	REQUIRE(settings.ditherStrength == Approx(0.5f));
}

TEST_CASE("TonemapSettings clamps exposure", "[tonemap]")
{
	TonemapSettings settings;
	settings.SetExposure(0.0f);
	REQUIRE(settings.exposure == Approx(0.1f));
	settings.SetExposure(20.0f);
	REQUIRE(settings.exposure == Approx(8.0f));
	settings.SetExposure(1.25f);
	REQUIRE(settings.exposure == Approx(1.25f));
}
```

- [ ] **Step 2: Run them to verify they fail**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t deferred_shading_tests
```

Expected: compile errors for the four missing headers.

- [ ] **Step 3: Write the implementations**

`src/shared/scene_graph/atmosphere_settings.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief User-tunable base values of the height fog, set from cvars or the editor.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct AtmosphereParameters
	{
		/// @brief Extinction per metre at BaseHeight.
		float density = 0.02f;

		/// @brief Exponential falloff of the density per metre of height above BaseHeight.
		float heightFalloff = 0.05f;

		/// @brief World Y at which the density equals `density`.
		float baseHeight = 0.0f;

		/// @brief Henyey-Greenstein g of the sun scattering lobe. Larger = tighter sun glow.
		float anisotropy = 0.7f;

		/// @brief Multiplier on the sun's in-scattered light (the shafts and the sun glow).
		float shaftStrength = 1.0f;

		/// @brief Sets the base density, clamped to [0, 1].
		void SetDensity(const float value) { density = Clamp(value, 0.0f, 1.0f); }

		/// @brief Sets the height falloff, clamped to [0, 1].
		void SetHeightFalloff(const float value) { heightFalloff = Clamp(value, 0.0f, 1.0f); }

		/// @brief Sets the base height, clamped to [-10000, 10000].
		void SetBaseHeight(const float value) { baseHeight = Clamp(value, -10000.0f, 10000.0f); }

		/// @brief Sets the anisotropy, clamped to [0, 0.95]; g >= 1 is singular.
		void SetAnisotropy(const float value) { anisotropy = Clamp(value, 0.0f, 0.95f); }

		/// @brief Sets the shaft strength, clamped to [0, 16].
		void SetShaftStrength(const float value) { shaftStrength = Clamp(value, 0.0f, 16.0f); }

	private:
		static float Clamp(const float value, const float minValue, const float maxValue)
		{
			if (value < minValue)
			{
				return minValue;
			}

			if (value > maxValue)
			{
				return maxValue;
			}

			return value;
		}
	};

	/// @brief Values the time-of-day curves produce for the current hour. Written by SkyComponent.
	struct AtmosphereTimeOfDay
	{
		/// @brief Ambient radiance of the fog, linear RGB. Defaults to the legacy fog colour.
		float fogTint[3]{ 0.447f, 0.638f, 1.0f };

		/// @brief Multiplier on AtmosphereParameters::density.
		float densityMultiplier = 1.0f;

		/// @brief Tint of the sun light inside the fog, linear RGB.
		float sunScatterColor[3]{ 1.0f, 1.0f, 1.0f };

		/// @brief Multiplier on AtmosphereParameters::shaftStrength.
		float shaftMultiplier = 1.0f;
	};

	/// @brief The final per-frame fog values uploaded to the camera constant buffer.
	struct AtmosphereConstants
	{
		float density = 0.0f;
		float heightFalloff = 0.0f;
		float baseHeight = 0.0f;
		float anisotropy = 0.0f;
		float fogTint[3]{ 0.0f, 0.0f, 0.0f };
		float sunScatterColor[3]{ 0.0f, 0.0f, 0.0f };
		float shaftStrength = 0.0f;
	};

	/// @brief Combines base parameters with the time-of-day values.
	/// @param parameters Base values from cvars or the editor.
	/// @param timeOfDay Curve values for the current hour.
	/// @param fogEnabled When false, density is zero, which makes every fog term vanish.
	/// @return The values for the camera constant buffer. Curve overshoot never goes negative.
	[[nodiscard]] inline AtmosphereConstants CombineAtmosphere(const AtmosphereParameters& parameters,
		const AtmosphereTimeOfDay& timeOfDay, const bool fogEnabled)
	{
		const auto nonNegative = [](const float value) { return value < 0.0f ? 0.0f : value; };

		AtmosphereConstants constants;
		constants.density = fogEnabled ? parameters.density * nonNegative(timeOfDay.densityMultiplier) : 0.0f;
		constants.heightFalloff = parameters.heightFalloff;
		constants.baseHeight = parameters.baseHeight;
		constants.anisotropy = parameters.anisotropy;
		constants.shaftStrength = parameters.shaftStrength * nonNegative(timeOfDay.shaftMultiplier);

		for (int i = 0; i < 3; ++i)
		{
			constants.fogTint[i] = nonNegative(timeOfDay.fogTint[i]);
			constants.sunScatterColor[i] = nonNegative(timeOfDay.sunScatterColor[i]);
		}

		return constants;
	}
}
```

`src/shared/deferred_shading/atmosphere_pass_settings.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Quality and debug settings of the atmosphere (fog + light shaft) pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct AtmospherePassSettings
	{
		/// @brief Upper bound of the march distance: the cascaded shadow map's maximum distance.
		static constexpr float MaxMarchDistance = 300.0f;

		/// @brief Current preset: 0 Off (analytic fog only), 1 Low, 2 Medium, 3 High, 4 Ultra.
		int32 qualityLevel = 3;

		/// @brief March target resolution = G-Buffer resolution / this.
		uint32 resolutionDivisor = 2;

		/// @brief Shadow-map steps per pixel. 0 disables the march entirely.
		uint32 stepCount = 48;

		/// @brief Number of separable bilateral blur iterations on the march target.
		uint32 blurIterations = 2;

		/// @brief Metres of each view ray that are ray-marched; the rest uses the closed form.
		float marchDistance = 200.0f;

		/// @brief Distance at which sky pixels (no geometry) are fogged.
		float skyDistance = 2000.0f;

		/// @brief 0 off, 1 in-scatter only, 2 transmittance, 3 march shadow term.
		uint32 debugMode = 0;

		/// @brief Applies a quality preset. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			level = level < 0 ? 0 : (level > 4 ? 4 : level);
			qualityLevel = level;

			switch (level)
			{
			case 0:
				resolutionDivisor = 1;
				stepCount = 0;
				blurIterations = 0;
				break;
			case 1:
				resolutionDivisor = 4;
				stepCount = 12;
				blurIterations = 1;
				break;
			case 2:
				resolutionDivisor = 2;
				stepCount = 24;
				blurIterations = 1;
				break;
			case 3:
				resolutionDivisor = 2;
				stepCount = 48;
				blurIterations = 2;
				break;
			default:
				resolutionDivisor = 1;
				stepCount = 64;
				blurIterations = 2;
				break;
			}
		}

		/// @brief Sets the march distance, clamped to [0, MaxMarchDistance].
		void SetMarchDistance(const float value)
		{
			marchDistance = value < 0.0f ? 0.0f : (value > MaxMarchDistance ? MaxMarchDistance : value);
		}

		/// @brief Sets the debug view, clamped to [0, 3].
		void SetDebugMode(const int mode)
		{
			debugMode = static_cast<uint32>(mode < 0 ? 0 : (mode > 3 ? 3 : mode));
		}

		/// @brief Whether the shadowed march runs at all.
		[[nodiscard]] bool IsMarchEnabled() const { return stepCount > 0 && marchDistance > 0.0f; }
	};
}
```

`src/shared/deferred_shading/bloom_settings.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Settings of the bloom pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct BloomSettings
	{
		/// @brief Current preset: 0 Off, 1 Low, 2 High.
		int32 qualityLevel = 2;

		/// @brief First downsample level = scene resolution / this.
		uint32 startDivisor = 2;

		/// @brief Number of downsample levels. 0 disables bloom.
		uint32 levelCount = 6;

		/// @brief Weight of the bloom when added to the scene.
		float intensity = 0.08f;

		/// @brief Linear brightness at which the soft threshold is centred.
		float threshold = 0.8f;

		/// @brief Width of the soft threshold knee.
		float knee = 0.5f;

		/// @brief Applies a quality preset. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			level = level < 0 ? 0 : (level > 2 ? 2 : level);
			qualityLevel = level;

			if (level == 0)
			{
				levelCount = 0;
			}
			else if (level == 1)
			{
				startDivisor = 4;
				levelCount = 4;
			}
			else
			{
				startDivisor = 2;
				levelCount = 6;
			}
		}

		/// @brief Sets the intensity, clamped to [0, 1].
		void SetIntensity(const float value)
		{
			intensity = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
		}

		/// @brief Sets the threshold, clamped to [0, 16].
		void SetThreshold(const float value)
		{
			threshold = value < 0.0f ? 0.0f : (value > 16.0f ? 16.0f : value);
		}

		/// @brief Whether the bloom pass runs.
		[[nodiscard]] bool IsEnabled() const { return levelCount > 0; }
	};
}
```

`src/shared/deferred_shading/tonemap_settings.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Settings of the final tonemap pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct TonemapSettings
	{
		/// @brief Linear multiplier applied before ACES. 1 leaves the image unchanged.
		float exposure = 1.0f;

		/// @brief Dither amplitude in 8-bit steps (0.5 = ±half a step).
		float ditherStrength = 0.5f;

		/// @brief Sets the exposure, clamped to [0.1, 8].
		void SetExposure(const float value)
		{
			exposure = value < 0.1f ? 0.1f : (value > 8.0f ? 8.0f : value);
		}
	};
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t deferred_shading_tests
.\bin\Debug\deferred_shading_tests.exe
```

Expected: `All tests passed` (the pre-existing SSAO, contact shadow and water tests included).

- [ ] **Step 5: Commit**

```powershell
git add src/shared/scene_graph/atmosphere_settings.h src/shared/deferred_shading/atmosphere_pass_settings.h src/shared/deferred_shading/bloom_settings.h src/shared/deferred_shading/tonemap_settings.h src/tests/deferred_shading_tests/test_atmosphere_settings.cpp src/tests/deferred_shading_tests/test_atmosphere_pass_settings.cpp src/tests/deferred_shading_tests/test_bloom_settings.cpp src/tests/deferred_shading_tests/test_tonemap_settings.cpp
git commit -m "feat(render): settings for atmosphere, bloom and tonemap passes"
```

---

### Task 3: Linear-HDR frame and TonemapPass (regression checkpoint 1)

Moves ACES + gamma out of the lighting pass and the forward materials into a new final pass. The legacy linear fog stays in place for this task so the image must not change.

**Files:**
- Create: `tools/render_compare/capture_client.ps1`, `tools/render_compare/compare_screenshots.py`
- Create: `src/shared/deferred_shading/tonemap_pass.h`, `src/shared/deferred_shading/tonemap_pass.cpp`, `src/shared/deferred_shading/shaders/PS_Tonemap.hlsl`
- Modify: `src/shared/deferred_shading/deferred_renderer.h`, `deferred_renderer.cpp` (constructor ~line 138, `Resize` ~319, `Render` ~442-510, `GetFinalRenderTarget` ~869)
- Modify: `src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl:585-589`
- Modify: `src/shared/scene_graph/scene.h` (~line 347 and ~729), `scene.cpp:41-62` and `:1192-1194`
- Modify: `src/shared/graphics_d3d11/material_compiler_d3d11.cpp:851-857`, `:925`, `:1709-1731`
- Modify: `.gitignore:65`

**Interfaces:**
- Consumes: `TonemapSettings` (Task 2).
- Produces:
  - `Scene::SetForwardOutputLinear(bool)`, `[[nodiscard]] bool Scene::IsForwardOutputLinear() const`
  - camera cbuffer field `forwardOutputLinear` (C++ `PsCameraConstantBuffer::forwardOutputLinear`, HLSL `forwardOutputLinear` in generated materials), in the slot formerly `_forwardPad0`
  - generated HLSL functions `InverseACESFilm(float3)` and `InverseTonemap(float3)` in every material pixel shader
  - `class TonemapPass { TonemapPass(GraphicsDevice&, uint32, uint32); void Resize(uint32, uint32); void Render(RenderTexture& hdrScene, const TexturePtr& bloom, float bloomScale, VertexBuffer& quad, ShaderBase& fullscreenVs); RenderTexturePtr GetResult() const; TonemapSettings& GetSettings(); }`
  - `DeferredRenderer::SetExposure(float)`, `[[nodiscard]] float DeferredRenderer::GetExposure() const`

- [ ] **Step 1: Add the screenshot helpers**

`tools/render_compare/capture_client.ps1`:

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Captures the client area of the running mmo_client window to a PNG.
# Usage: powershell -File tools/render_compare/capture_client.ps1 -OutFile shot.png
param(
    [Parameter(Mandatory = $true)][string]$OutFile
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class MmoCapture
{
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
}
"@

[MmoCapture]::SetProcessDPIAware() | Out-Null
$process = Get-Process mmo_client -ErrorAction Stop | Select-Object -First 1
$hwnd = $process.MainWindowHandle
[MmoCapture]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 500

$rect = New-Object MmoCapture+RECT
[MmoCapture]::GetClientRect($hwnd, [ref]$rect) | Out-Null
$origin = New-Object MmoCapture+POINT
[MmoCapture]::ClientToScreen($hwnd, [ref]$origin) | Out-Null

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size $width, $height))
$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Output "Saved $OutFile ($width x $height)"
```

`tools/render_compare/compare_screenshots.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Compares two client screenshots for the rendering regression checkpoint.

Prints the largest per-channel difference and the share of pixels beyond the noise tolerance,
optionally writes a 16x amplified difference image, and exits 0 on PASS.
Requires Pillow (python -m pip install pillow).
"""
import argparse
import sys

from PIL import Image, ImageChops


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--diff", help="write an amplified difference image here")
    parser.add_argument("--tolerance", type=int, default=2,
                        help="per-channel difference (8-bit levels) treated as dither noise")
    parser.add_argument("--max-fraction", type=float, default=0.005,
                        help="share of pixels allowed beyond the tolerance")
    args = parser.parse_args()

    baseline = Image.open(args.baseline).convert("RGB")
    candidate = Image.open(args.candidate).convert("RGB")
    if baseline.size != candidate.size:
        print(f"FAIL: size mismatch {baseline.size} vs {candidate.size}")
        return 1

    diff = ImageChops.difference(baseline, candidate)
    pixels = list(diff.getdata())
    largest = max(max(pixel) for pixel in pixels)
    over = sum(1 for pixel in pixels if max(pixel) > args.tolerance)
    fraction = over / len(pixels)
    print(f"max channel difference: {largest}; pixels over tolerance: {over} ({fraction:.4%})")

    if args.diff:
        diff.point(lambda value: min(255, value * 16)).save(args.diff)

    passed = fraction <= args.max_fraction
    print("PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Capture the baseline before any rendering change**

Build the unchanged client (make sure no `*_server` or `mmo_client` process holds the exe: `Get-Process *_server, mmo_client -ErrorAction SilentlyContinue`):

```powershell
cmake --build build --config Debug -t mmo_client
```

Start the dev stack (`login_server`, `realm_server`, `world_server` from `bin/Debug`, a few seconds apart) and launch `bin/Debug/mmo_client.exe`; `bin/Debug/config/RunOnce.cfg` logs in automatically. Pick three views and leave the character standing still in each:
1. outdoors with water in view, no NPCs moving;
2. the same spot with a spell visual playing (cast any visual spell);
3. underwater.

For each view run (N = 1, 2, 3):

```powershell
New-Item -ItemType Directory -Force build/render_compare | Out-Null
powershell -File tools/render_compare/capture_client.ps1 -OutFile build/render_compare/baseline_N.png
```

Do not move the character between the baseline and candidate captures: position and orientation persist across relaunches, so the same view comes back. Close the client.

- [ ] **Step 3: Add the forward output flag to Scene**

In `src/shared/scene_graph/scene.h`, next to `SetForwardTransparentOnly` add:

```cpp
		/// @brief Makes forward materials write linear HDR instead of tone-mapped display colour.
		/// @remark Only DeferredRenderer sets this, around its own forward pass, because its
		///         TonemapPass tone maps the whole frame afterwards. Every standalone forward render
		///         (editor previews, the client's model frames, the minimap baker) leaves it false.
		void SetForwardOutputLinear(const bool linear) { m_forwardOutputLinear = linear; }

		/// @brief Whether forward materials currently write linear HDR.
		[[nodiscard]] bool IsForwardOutputLinear() const { return m_forwardOutputLinear; }
```

and next to `bool m_forwardTransparentOnly = false;` add:

```cpp
		/// @brief See SetForwardOutputLinear.
		bool m_forwardOutputLinear = false;
```

In `src/shared/scene_graph/scene.cpp` rename the struct field `float _forwardPad0;` (line 59) to:

```cpp
		float forwardOutputLinear;	// 1 = forward materials skip tone mapping (DeferredRenderer's forward pass)
```

and replace `buffer._forwardPad0   = 0.0f;` (line 1193) with:

```cpp
		buffer.forwardOutputLinear = m_forwardOutputLinear ? 1.0f : 0.0f;
```

- [ ] **Step 4: Make generated forward materials honour the flag**

In `src/shared/graphics_d3d11/material_compiler_d3d11.cpp`, directly after the `ACESFilm` emission (after line 857) add:

```cpp
		// Exact inverse of ACESFilm followed by gamma. Unlit forward materials author display-referred
		// colour; inside DeferredRenderer's forward pass they convert it to the linear value the
		// TonemapPass will map back onto that same display colour.
		m_pixelShaderStream
			<< "float3 InverseACESFilm(float3 y) {\n"
			<< "\tfloat a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;\n"
			<< "\tfloat3 qa = y * c - a; float3 qb = y * d - b; float3 qc = y * e;\n"
			<< "\treturn (-qb - sqrt(max(qb * qb - 4.0 * qa * qc, 0.0))) / (2.0 * qa);\n"
			<< "}\n\n"
			<< "float3 InverseTonemap(float3 displayColor) {\n"
			<< "\treturn InverseACESFilm(pow(clamp(displayColor, 0.0, 0.999), 2.2));\n"
			<< "}\n\n";
```

In the `CameraParameters` cbuffer emission replace the line `<< "\tfloat _forwardPad0;\n"` (line 925) with:

```cpp
			<< "\tfloat forwardOutputLinear;	// 1 inside DeferredRenderer's forward pass: output linear HDR\n"
```

Replace the unlit block (lines 1709-1714) with:

```cpp
					m_pixelShaderStream
						<< "\tfloat3 color = pow(baseColor, 2.2);\n"
						<< "\tfloat fogDistance = length(input.worldPos - cameraPos);\n"
						<< "\tfloat fogFactor = saturate((fogDistance - fogStart) / (fogEnd - fogStart));\n"
						<< "\tfloat3 displayFogColor = pow(ACESFilm(fogColor), (1.0f/2.2f).xxx);\n"
						<< "\tcolor = lerp(color, displayFogColor, fogFactor);\n"
						<< "\tif (forwardOutputLinear > 0.5) { color = InverseTonemap(color); }\n";
```

Replace the tone mapping emission of the lit block (lines 1727-1731) with:

```cpp
					// ACES + gamma only when rendering standalone. Inside DeferredRenderer the
					// TonemapPass tone maps the whole frame, so the colour stays linear here.
					m_pixelShaderStream
						<< "\tif (forwardOutputLinear < 0.5)\n"
						<< "\t{\n"
						<< "\t\tcolor = ACESFilm(color);\n"
						<< "\t\tcolor = pow(color, (1.0f/2.2f).xxx);\n"
						<< "\t}\n";
```

Update the comment above the unlit block: replace the sentence "No ACES/tonemap either: unlit colours are display-referred as authored." with "No ACES either: unlit colours are display-referred as authored, and inside DeferredRenderer they are inverse-tonemapped so the final TonemapPass reproduces them."

- [ ] **Step 5: Write the tonemap shader**

`src/shared/deferred_shading/shaders/PS_Tonemap.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Final pass of the deferred frame: adds bloom to the linear HDR scene, applies exposure, ACES and
// gamma, and dithers. Everything earlier in the frame must stay linear.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D BloomTexture : register(t1);
SamplerState LinearSampler : register(s0);

cbuffer TonemapBuffer : register(b2)
{
    float Exposure;
    float BloomScale;       // bloom intensity already divided by the level count; 0 without bloom
    float DitherStrength;   // in 8-bit steps
    float _TonemapPadding;
};

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float InterleavedGradientNoise(float2 position)
{
    return frac(52.9829189f * frac(dot(position, float2(0.06711056f, 0.00583715f))));
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 scene = SceneTexture.Load(int3(input.Position.xy, 0));
    float3 bloom = BloomTexture.SampleLevel(LinearSampler, input.TexCoord, 0).rgb;

    float3 hdr = scene.rgb + bloom * BloomScale;
    float3 color = pow(ACESFilm(hdr * Exposure), 1.0f / 2.2f);

    // Triangular noise in [-1, 1]: two uniform samples summed. Breaks the 8-bit banding the fog
    // gradients would otherwise show once the frame lands in the back buffer.
    float noise = InterleavedGradientNoise(input.Position.xy) + InterleavedGradientNoise(input.Position.xy + 17.0f) - 1.0f;
    color += noise * (DitherStrength / 255.0f);

    // Alpha passes through: the world frame quad has always received the lit scene's alpha.
    return float4(color, scene.a);
}
```

Add to `.gitignore` after line 65:

```
/src/shared/deferred_shading/shaders/PS_Tonemap.h
```

- [ ] **Step 6: Write the TonemapPass class**

`src/shared/deferred_shading/tonemap_pass.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "tonemap_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"

namespace mmo
{
	/// @brief The last pass of the deferred frame: bloom composite, exposure, ACES, gamma, dither.
	/// @remark Backend-neutral like SsaoPass; the shader blob choice is isolated in tonemap_pass.cpp.
	class TonemapPass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass and its full-resolution output target.
		TonemapPass(GraphicsDevice& device, uint32 width, uint32 height);

		~TonemapPass() override = default;

	public:
		/// @brief Resizes the output target.
		void Resize(uint32 width, uint32 height);

		/// @brief Tone maps the linear HDR scene into the output target.
		/// @param hdrScene The finished linear HDR scene (after the forward pass).
		/// @param bloom The bloom result, or nullptr for none.
		/// @param bloomScale Weight of the bloom texture; ignored when bloom is nullptr.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		void Render(RenderTexture& hdrScene, const TexturePtr& bloom, float bloomScale, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the display-referred output.
		[[nodiscard]] RenderTexturePtr GetResult() const { return m_outputRT; }

		/// @brief Gets the mutable settings.
		[[nodiscard]] TonemapSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const TonemapSettings& GetSettings() const { return m_settings; }

	private:
		GraphicsDevice& m_device;

		TonemapSettings m_settings;

		uint32 m_width;
		uint32 m_height;

		RenderTexturePtr m_outputRT;

		/// @brief 1x1 black stand-in bound when there is no bloom.
		TexturePtr m_blackTexture;

		ConstantBufferPtr m_tonemapBuffer;

		ShaderPtr m_tonemapPs;
	};
}
```

`src/shared/deferred_shading/tonemap_pass.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "tonemap_pass.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_Tonemap.h"
#	define MMO_TONEMAP_PS_BYTECODE g_PS_Tonemap
#	define MMO_TONEMAP_PS_SIZE std::size(g_PS_Tonemap)
#else
#	define MMO_TONEMAP_PS_BYTECODE nullptr
#	define MMO_TONEMAP_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors the TonemapBuffer cbuffer in PS_Tonemap.hlsl (b2).
		struct alignas(16) TonemapConstants
		{
			float exposure;
			float bloomScale;
			float ditherStrength;
			float padding0;
		};
	}

	TonemapPass::TonemapPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_width(width)
		, m_height(height)
	{
		m_tonemapBuffer = m_device.CreateConstantBuffer(sizeof(TonemapConstants), nullptr);
		ASSERT(m_tonemapBuffer);

		m_tonemapPs = m_device.CreateShader(ShaderType::PixelShader, MMO_TONEMAP_PS_BYTECODE, MMO_TONEMAP_PS_SIZE);
		ASSERT(m_tonemapPs);

		// R16G16B16A16 like the scene target: the underwater pass reads this output and its sun
		// shafts add above 1.0.
		m_outputRT = m_device.CreateRenderTexture("TonemapOutput", static_cast<uint16>(width), static_cast<uint16>(height),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		ASSERT(m_outputRT);

		m_blackTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_blackTexture);
		uint32 blackPixel = 0xFF000000;
		m_blackTexture->LoadRaw(&blackPixel, sizeof(blackPixel));
		m_blackTexture->SetDebugName("TonemapNoBloom");
	}

	void TonemapPass::Resize(const uint32 width, const uint32 height)
	{
		m_width = width;
		m_height = height;
		m_outputRT->Resize(width, height);
	}

	void TonemapPass::Render(RenderTexture& hdrScene, const TexturePtr& bloom, const float bloomScale, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		TonemapConstants constants{};
		constants.exposure = m_settings.exposure;
		constants.bloomScale = bloom ? bloomScale : 0.0f;
		constants.ditherStrength = m_settings.ditherStrength;
		m_tonemapBuffer->Update(&constants);

		m_outputRT->Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_width), static_cast<int32>(m_height), 0.0f, 1.0f);

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);

		hdrScene.Bind(ShaderType::PixelShader, 0);
		m_device.BindTexture(bloom ? bloom : m_blackTexture, ShaderType::PixelShader, 1);

		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);
		fullscreenVs.Set();
		m_tonemapPs->Set();
		quad.Set(0);
		m_tonemapBuffer->BindToStage(ShaderType::PixelShader, 2);

		m_device.Draw(6, 0);

		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
	}
}
```

- [ ] **Step 7: Make the lighting pass linear and wire the TonemapPass**

In `PS_DeferredLighting.hlsl` delete lines 585-589 (the `// Apply ACES tone mapping` block and `// Apply gamma correction` block). Leave the `ACESFilm` function definition; it is removed in Task 4.

In `deferred_renderer.h`: add `#include "tonemap_pass.h"` after `#include "post_process_pass.h"`; add after `m_postProcessPass`:

```cpp
        /// @brief Final pass: bloom composite, exposure, ACES, gamma and dither. Everything before
        ///        it works in linear HDR.
        std::unique_ptr<TonemapPass> m_tonemapPass;
```

and in the public section after `SetContactShadowDebugVisualization`:

```cpp
        /// @brief Sets the exposure applied before tone mapping. Clamped to [0.1, 8].
        void SetExposure(float exposure) { m_tonemapPass->GetSettings().SetExposure(exposure); }

        /// @brief Gets the exposure applied before tone mapping.
        [[nodiscard]] float GetExposure() const { return m_tonemapPass->GetSettings().exposure; }
```

In `deferred_renderer.cpp`:
- constructor, after `m_postProcessPass = ...` add `m_tonemapPass = std::make_unique<TonemapPass>(m_device, width, height);`
- `Resize`: add `m_tonemapPass->Resize(width, height);`
- `Render`: replace

```cpp
        scene.SetForwardTransparentOnly(true);
        scene.Render(camera, PixelShaderType::Forward);
        scene.SetForwardTransparentOnly(false);
```

with

```cpp
        scene.SetForwardTransparentOnly(true);
        // The forward pass renders into the linear HDR scene, so materials must leave tone mapping
        // to the TonemapPass below.
        scene.SetForwardOutputLinear(true);
        scene.Render(camera, PixelShaderType::Forward);
        scene.SetForwardOutputLinear(false);
        scene.SetForwardTransparentOnly(false);
```

  and directly after the two `m_device.BindTexture(nullptr, ... kSceneColorTextureSlot / kSceneDepthTextureSlot)` lines add:

```cpp
        // Linear HDR -> display. The underwater post-process below still receives display-referred
        // colour, so its thresholds and tuning are untouched.
        m_tonemapPass->Render(*m_renderTexture, nullptr, 0.0f, *m_quadBuffer, *m_deferredLightVs);
```

  In both `m_postProcessPass->Render(...)` calls replace the argument `*m_renderTexture` with `*m_tonemapPass->GetResult()`. Update the comment above `if (m_postProcessPass)` to say it hands back the tonemap output when dry.
- `GetFinalRenderTarget`: replace `return m_renderTexture;` with `return m_tonemapPass->GetResult();`

Add `/src/shared/deferred_shading/shaders/PS_Tonemap.h` to `.gitignore` if not done in Step 5.

- [ ] **Step 8: Build everything that compiles materials or renders**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t mmo_client mmo_edit deferred_shading_tests scene_graph_tests
.\bin\Debug\scene_graph_tests.exe
.\bin\Debug\deferred_shading_tests.exe
```

Expected: builds succeed; both suites pass (`test_material_compiler_*.cpp` assert that generated forward shaders still compile with an empty error string).

- [ ] **Step 9: Rebuild all materials**

Launch `bin/Debug/mmo_edit.exe`, open any material editor so its Tools menu is present, choose **Tools → Rebuild All Materials**, wait for completion, close the editor. Then:

```powershell
git -C data/client status --short
git -C data/editor status --short
```

Record which material files the rebuild changed (compare against the pre-existing modifications). Do not commit inside the submodules yet — Task 4 rebuilds again; the combined data commit happens at the end of Task 4.

- [ ] **Step 10: Regression checkpoint 1**

Launch the client, return to the three baseline views and capture `build/render_compare/candidate_N.png` with the same command as Step 2. Then for N = 1, 2, 3:

```powershell
python tools/render_compare/compare_screenshots.py build/render_compare/baseline_N.png build/render_compare/candidate_N.png --diff build/render_compare/diff_N.png
```

Expected: view 1 and 3 print `PASS`. View 2 (animated spell) may FAIL on count; open `diff_N.png` and confirm the differences are confined to the moving effect and translucent edges (spec note 2). Any difference on opaque terrain, sky, characters or water body is a bug — stop and fix before continuing (typical causes: `forwardOutputLinear` not reaching the material because the material was not rebuilt, or the tonemap pass sampling with the wrong filter).

Also open the editor's material preview and a mesh preview: they must look exactly as before (standalone forward keeps tonemapping).

- [ ] **Step 11: Commit**

```powershell
git add .gitignore tools/render_compare src/shared/deferred_shading/tonemap_pass.h src/shared/deferred_shading/tonemap_pass.cpp src/shared/deferred_shading/shaders/PS_Tonemap.hlsl src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp src/shared/scene_graph/scene.h src/shared/scene_graph/scene.cpp src/shared/graphics_d3d11/material_compiler_d3d11.cpp
git commit -m "feat(render): linear HDR frame with a final tonemap pass"
```

---

### Task 4: Atmosphere data flow and forward height fog

Replaces the legacy linear fog with the height-fog parameters end to end: `Scene` state, camera cbuffer, `SkyComponent` curves, generated forward materials, and the shared HLSL header. After this task forward surfaces (water, particles) use height fog. Opaque deferred geometry is unfogged until Task 5 wires the composite — expected, not a bug.

**Files:**
- Create: `src/shared/deferred_shading/shaders/AtmosphereCommon.hlsli`
- Modify: `src/shared/deferred_shading/CMakeLists.txt:7-20`
- Modify: `src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl` (lines 61-81, 178-186, 580-583)
- Modify: `src/shared/scene_graph/scene.h` (lines 347-363, 717-721), `scene.cpp` (41-62, 1136-1197)
- Modify: `src/shared/graphics_d3d11/material_compiler_d3d11.cpp` (909-928, 1696-1732)
- Modify: `src/shared/graphics/sky_component.h`, `sky_component.cpp` (65-136, 328-329)
- Modify: `src/mmo_client/game_states/world_state.cpp:1433`
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.cpp:126-129`, `:2062-2064`
- Modify: `src/mmo_edit/editors/world_editor/world_settings_panel.cpp:172-177`

**Interfaces:**
- Consumes: `AtmosphereParameters`, `AtmosphereTimeOfDay`, `CombineAtmosphere` (Task 2); formulas from Task 1; `forwardOutputLinear` (Task 3).
- Produces:
  - `void Scene::SetAtmosphereParameters(const AtmosphereParameters&)`, `[[nodiscard]] const AtmosphereParameters& Scene::GetAtmosphereParameters() const`
  - `void Scene::SetAtmosphereTimeOfDay(const AtmosphereTimeOfDay&)`, `[[nodiscard]] const AtmosphereTimeOfDay& Scene::GetAtmosphereTimeOfDay() const`
  - removed: `Scene::SetFogRange`, `GetFogStart`, `GetFogEnd`, `SetFogColor`
  - camera cbuffer b1 layout (176 bytes), HLSL names in `AtmosphereCommon.hlsli`: `CameraPosition, FogDensity, FogHeightFalloff, FogTint, InverseViewMatrix, Time, FogBaseHeight, FogAnisotropy, _CameraPadding0, SunDirection, SunIntensity, SunColor, ForwardOutputLinear, CameraAmbientColor, _CameraPadding1, SunScatterColor, ShaftStrength`
  - HLSL functions in `AtmosphereCommon.hlsli`: `float FogDensityAt(float y)`, `float FogOpticalDepth(float originY, float dirY, float len)`, `float ScatterPhase(float cosTheta)`, `float3 FogSource(float cosTheta, float sunVisibility)`, `float3 WorldViewRay(float2 uv)`, `float InterleavedGradientNoise(float2 position)`; `cbuffer ViewMatrices : register(b12) { matView; matProj; matInvView; InverseProjection; }`

- [ ] **Step 1: Stop compiling `.hlsli` files as shaders**

In `src/shared/deferred_shading/CMakeLists.txt` replace lines 8-13 with:

```cmake
	file(GLOB d3d11_vs_shaders "shaders/VS_*.hlsl")
	file(GLOB d3d11_ps_shaders "shaders/PS_*.hlsl")
	file(GLOB d3d11_shader_includes "shaders/*.hlsli")

	target_sources(deferred_shading PRIVATE ${d3d11_vs_shaders} ${d3d11_ps_shaders} ${d3d11_shader_includes})
	source_group(src\\shaders FILES ${d3d11_vs_shaders})
	source_group(src\\shaders FILES ${d3d11_ps_shaders})
	source_group(src\\shaders FILES ${d3d11_shader_includes})

	# Include files are pulled in by #include only; MSBuild must not try to compile them.
	set_source_files_properties(${d3d11_shader_includes} PROPERTIES VS_TOOL_OVERRIDE "None")
```

(The old glob gave `.hlsli` files the vertex-shader properties; nothing matched until now.) Note: MSBuild's FxCompile does not track `#include` dependencies — after editing an `.hlsli`, touch the including `.hlsl` files or rebuild `deferred_shading`.

- [ ] **Step 2: Write the shared HLSL header**

`src/shared/deferred_shading/shaders/AtmosphereCommon.hlsli`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// View matrices, the camera constant buffer and the height-fog model shared by the deferred
// lighting and atmosphere shaders. The generated forward material shaders carry a copy of the fog
// functions (MaterialCompilerD3D11), and deferred_shading/atmosphere_math.h mirrors them in C++ for
// the unit tests - change all three together.

#ifndef ATMOSPHERE_COMMON_HLSLI
#define ATMOSPHERE_COMMON_HLSLI

// Per-view matrices (b12). Must match the layout uploaded by GraphicsDeviceD3D11 (see
// kPerViewMatrixBufferSlot).
cbuffer ViewMatrices : register(b12)
{
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix InverseProjection;
};

// MUST stay field-for-field in sync with PsCameraConstantBuffer in scene.cpp and the
// CameraParameters cbuffer emitted by MaterialCompilerD3D11.
cbuffer CameraBuffer : register(b1)
{
    float3 CameraPosition;
    float FogDensity;               // extinction per metre at FogBaseHeight; 0 = fog off
    float FogHeightFalloff;
    float3 FogTint;                 // ambient radiance of the fog
    column_major matrix InverseViewMatrix;
    float Time;
    float FogBaseHeight;
    float FogAnisotropy;
    float _CameraPadding0;
    float3 SunDirection;            // world space, toward the sun
    float SunIntensity;
    float3 SunColor;
    float ForwardOutputLinear;
    float3 CameraAmbientColor;
    float _CameraPadding1;
    float3 SunScatterColor;
    float ShaftStrength;
};

static const float FOG_MAX_DENSITY_EXPONENT = 12.0f;

// Extinction coefficient at world height y.
float FogDensityAt(float y)
{
    return FogDensity * exp(min(-FogHeightFalloff * (y - FogBaseHeight), FOG_MAX_DENSITY_EXPONENT));
}

// Closed-form optical depth along a ray segment of length len starting at height originY.
float FogOpticalDepth(float originY, float dirY, float len)
{
    float sigmaStart = FogDensityAt(originY);
    float sigmaEnd = FogDensityAt(originY + dirY * len);
    float k = FogHeightFalloff * dirY * len;
    if (abs(k) < 1e-3f)
    {
        return len * 0.5f * (sigmaStart + sigmaEnd);
    }

    return len * (sigmaStart - sigmaEnd) / k;
}

// Henyey-Greenstein blended 80/20 with isotropic, normalised so isotropic = 1.
float ScatterPhase(float cosTheta)
{
    float g = FogAnisotropy;
    float g2 = g * g;
    float hg = (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
    return lerp(1.0f, hg, 0.8f);
}

// Radiance the fog scatters toward the camera per unit of (1 - transmittance).
// sunVisibility is the shadow term at the scattering point (1 = lit).
float3 FogSource(float cosTheta, float sunVisibility)
{
    return FogTint + SunScatterColor * SunColor * SunIntensity * ShaftStrength * ScatterPhase(cosTheta) * sunVisibility;
}

// World-space, normalised view ray through a screen UV.
float3 WorldViewRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewH = mul(float4(ndc, 1.0f, 1.0f), InverseProjection);
    float3 viewRay = normalize(viewH.xyz / viewH.w);
    return normalize(mul(float4(viewRay, 0.0f), matInvView).xyz);
}

float InterleavedGradientNoise(float2 position)
{
    return frac(52.9829189f * frac(dot(position, float2(0.06711056f, 0.00583715f))));
}

#endif
```

- [ ] **Step 3: Point the lighting shader at the header**

In `PS_DeferredLighting.hlsl`:
- replace the `ViewMatrices` and `CameraBuffer` blocks (lines 61-81, from the `// Per-view matrices (b12)` comment through the closing `}` of `CameraBuffer`) with `#include "AtmosphereCommon.hlsli"`;
- delete the `ACESFilm` function (lines 178-186);
- delete the fog block (lines 580-583: `// Apply fog` through `lighting = lerp(lighting, FogColor, fogFactor);`).

The shader reads `CameraPosition`, `matInvView` and `InverseProjection`, all still declared by the header.

- [ ] **Step 4: Rework Scene's fog state and the camera cbuffer**

`scene.h`: add `#include "atmosphere_settings.h"` with the other includes. Replace lines 357-363 (`GetFogStart` … `SetFogColor`) with:

```cpp
		/// @brief Sets the tunable base values of the height fog (cvars, editor sliders).
		void SetAtmosphereParameters(const AtmosphereParameters& parameters) { m_atmosphereParameters = parameters; }

		/// @brief Gets the tunable base values of the height fog.
		[[nodiscard]] const AtmosphereParameters& GetAtmosphereParameters() const { return m_atmosphereParameters; }

		/// @brief Sets the time-of-day fog values. Written by SkyComponent every update.
		void SetAtmosphereTimeOfDay(const AtmosphereTimeOfDay& timeOfDay) { m_atmosphereTimeOfDay = timeOfDay; }

		/// @brief Gets the time-of-day fog values.
		[[nodiscard]] const AtmosphereTimeOfDay& GetAtmosphereTimeOfDay() const { return m_atmosphereTimeOfDay; }
```

Replace the members at lines 717-721 (`m_fogColor`, `m_fogStart`, `m_fogEnd`) with:

```cpp
		AtmosphereParameters m_atmosphereParameters;

		AtmosphereTimeOfDay m_atmosphereTimeOfDay;
```

`scene.cpp`: replace `struct PsCameraConstantBuffer` (lines 41-62) with:

```cpp
	struct alignas(16) PsCameraConstantBuffer
	{
		// MUST stay field-for-field in sync with CameraBuffer in AtmosphereCommon.hlsli and the
		// CameraParameters cbuffer emitted by MaterialCompilerD3D11.
		Vector3 cameraPosition;
		float fogDensity;			// 0 when fog is disabled
		float fogHeightFalloff;
		Vector3 fogTint;
		Matrix4 inverseViewMatrix;
		float time;
		float fogBaseHeight;
		float fogAnisotropy;
		float _cameraPadding0;

		// Forward lighting, populated from the scene's primary directional light.
		Vector3 sunDirection;		// World-space direction *toward* the sun (normalised)
		float sunIntensity;
		Vector3 sunColor;
		float forwardOutputLinear;	// 1 = forward materials skip tone mapping (DeferredRenderer's forward pass)
		Vector3 ambientColor;
		float _forwardPad1;

		// Sun light inside the fog.
		Vector3 sunScatterColor;
		float shaftStrength;
	};

	static_assert(sizeof(PsCameraConstantBuffer) == 176, "PsCameraConstantBuffer must match the 176-byte HLSL CameraBuffer layout");
```

Delete `Scene::SetFogRange` and `Scene::SetFogColor` (lines 1136-1147). In `RefreshCameraBuffer` replace everything from `PsCameraConstantBuffer buffer;` through `buffer._padding[2] = 0.0f;` with:

```cpp
		const AtmosphereConstants atmosphere = CombineAtmosphere(m_atmosphereParameters, m_atmosphereTimeOfDay, m_fogEnabled);

		PsCameraConstantBuffer buffer;
		buffer.cameraPosition = camera.GetDerivedPosition();
		buffer.fogDensity = atmosphere.density;
		buffer.fogHeightFalloff = atmosphere.heightFalloff;
		buffer.fogTint = Vector3(atmosphere.fogTint[0], atmosphere.fogTint[1], atmosphere.fogTint[2]);
		buffer.inverseViewMatrix = camera.GetViewMatrix().Inverse();
		buffer.time = m_elapsedTime;
		buffer.fogBaseHeight = atmosphere.baseHeight;
		buffer.fogAnisotropy = atmosphere.anisotropy;
		buffer._cameraPadding0 = 0.0f;
```

and after `buffer._forwardPad1   = 0.0f;` add:

```cpp
		buffer.sunScatterColor = Vector3(atmosphere.sunScatterColor[0], atmosphere.sunScatterColor[1], atmosphere.sunScatterColor[2]);
		buffer.shaftStrength = atmosphere.shaftStrength;
```

- [ ] **Step 5: Generated forward materials use height fog**

In `material_compiler_d3d11.cpp` replace the `CameraParameters` cbuffer emission (lines 909-928) with:

```cpp
		m_pixelShaderStream
			<< "cbuffer CameraParameters : register(b" << bufferRegister++ << ")\n"
			<< "{\n"
			// MUST stay field-for-field in sync with PsCameraConstantBuffer in scene.cpp.
			<< "\tfloat3 cameraPos;	// Camera position in world space\n"
			<< "\tfloat fogDensity;	// Height fog extinction per metre at fogBaseHeight; 0 = off\n"
			<< "\tfloat fogHeightFalloff;\n"
			<< "\tfloat3 fogTint;	// Ambient radiance of the fog\n"
			<< "\trow_major matrix inverseCameraView;	// Inverse view matrix\n"
			<< "\tfloat time;		// Time in seconds since game start\n"
			<< "\tfloat fogBaseHeight;\n"
			<< "\tfloat fogAnisotropy;\n"
			<< "\tfloat _cameraPadding0;\n"
			<< "\tfloat3 sunDirection;	// World-space direction toward the sun (normalised)\n"
			<< "\tfloat sunIntensity;\n"
			<< "\tfloat3 sunColor;\n"
			<< "\tfloat forwardOutputLinear;	// 1 inside DeferredRenderer's forward pass: output linear HDR\n"
			<< "\tfloat3 ambientColor;\n"
			<< "\tfloat _forwardPad1;\n"
			<< "\tfloat3 sunScatterColor;\n"
			<< "\tfloat shaftStrength;\n"
			<< "};\n\n";

		if (type == PixelShaderType::Forward)
		{
			// Height fog, identical to AtmosphereCommon.hlsli (and atmosphere_math.h). Change all three
			// together. Emitted after the cbuffer because the functions read its fields.
			m_pixelShaderStream
				<< "static const float FOG_MAX_DENSITY_EXPONENT = 12.0;\n\n"
				<< "float FogDensityAt(float y) {\n"
				<< "\treturn fogDensity * exp(min(-fogHeightFalloff * (y - fogBaseHeight), FOG_MAX_DENSITY_EXPONENT));\n"
				<< "}\n\n"
				<< "float FogOpticalDepth(float originY, float dirY, float len) {\n"
				<< "\tfloat sigmaStart = FogDensityAt(originY);\n"
				<< "\tfloat sigmaEnd = FogDensityAt(originY + dirY * len);\n"
				<< "\tfloat k = fogHeightFalloff * dirY * len;\n"
				<< "\tif (abs(k) < 1e-3) { return len * 0.5 * (sigmaStart + sigmaEnd); }\n"
				<< "\treturn len * (sigmaStart - sigmaEnd) / k;\n"
				<< "}\n\n"
				<< "float FogScatterPhase(float cosTheta) {\n"
				<< "\tfloat g2 = fogAnisotropy * fogAnisotropy;\n"
				<< "\tfloat hg = (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * fogAnisotropy * cosTheta, 1e-4), 1.5);\n"
				<< "\treturn lerp(1.0, hg, 0.8);\n"
				<< "}\n\n"
				<< "float3 ApplyHeightFog(float3 color, float3 worldPos) {\n"
				<< "\tfloat3 toPixel = worldPos - cameraPos;\n"
				<< "\tfloat dist = length(toPixel);\n"
				<< "\tfloat3 dir = toPixel / max(dist, 1e-4);\n"
				<< "\tfloat transmittance = exp(-FogOpticalDepth(cameraPos.y, dir.y, dist));\n"
				<< "\tfloat3 source = fogTint + sunScatterColor * sunColor * sunIntensity * shaftStrength * FogScatterPhase(dot(dir, sunDirection));\n"
				<< "\treturn color * transmittance + source * (1.0 - transmittance);\n"
				<< "}\n\n";
		}
```

Replace the whole forward output branch body from `if (!m_lit)` (line 1696) through the closing `}` of the lit `else` (line 1732) with:

```cpp
				if (!m_lit)
				{
					// Unlit materials (glowing particles, FX sprites …) author display-referred colour:
					// color = pow(baseColor, 2.2), and the Emissive pin is deliberately NOT added (FX
					// graphs often wire the same value into BaseColor and Emissive for the deferred unlit
					// path, which would double it). The colour is inverse-tonemapped to linear so the
					// height fog composes with the scene, then tone-mapped back only when rendering
					// standalone; inside DeferredRenderer the TonemapPass does that for the whole frame.
					m_pixelShaderStream
						<< "\tfloat3 color = ApplyHeightFog(InverseTonemap(pow(baseColor, 2.2)), input.worldPos);\n"
						<< "\tif (forwardOutputLinear < 0.5)\n"
						<< "\t{\n"
						<< "\t\tcolor = pow(ACESFilm(color), (1.0f/2.2f).xxx);\n"
						<< "\t}\n";
				}
				else
				{
					// Height fog in linear HDR, identical to the deferred atmosphere composite, so
					// translucent surfaces fog out to the same colour as the opaque scene behind them.
					m_pixelShaderStream
						<< "\tcolor = ApplyHeightFog(color, input.worldPos);\n";

					// ACES + gamma only when rendering standalone. Inside DeferredRenderer the
					// TonemapPass tone maps the whole frame, so the colour stays linear here.
					m_pixelShaderStream
						<< "\tif (forwardOutputLinear < 0.5)\n"
						<< "\t{\n"
						<< "\t\tcolor = ACESFilm(color);\n"
						<< "\t\tcolor = pow(color, (1.0f/2.2f).xxx);\n"
						<< "\t}\n";
				}
```

Also update the comment block at lines 1691-1695 to say "height fog" instead of "distance fog".

- [ ] **Step 6: SkyComponent writes the time-of-day fog values**

`sky_component.h`: after `m_cloudColorCurve` add:

```cpp
        std::unique_ptr<ColorCurve> m_fogColorCurve;    ///< Fog tint (rgb) and density multiplier (a) over time
        std::unique_ptr<ColorCurve> m_sunScatterCurve;  ///< Sun colour inside fog (rgb) and shaft multiplier (a) over time
```

`sky_component.cpp`: add `#include "scene_graph/atmosphere_settings.h"` with the includes. At the end of `LoadColorCurves` (before its closing brace, after the cloud curve block) add:

```cpp
        // Unlike the curves above, these fall back to defaults when the file is missing too: an empty
        // curve evaluates to zero, which would silently switch the fog and the shafts off.
        m_fogColorCurve = std::make_unique<ColorCurve>();
        bool fogColorLoaded = false;
        if (const auto file = AssetRegistry::OpenFile("Models/FogColor.hccv"))
        {
            io::StreamSource stream(*file);
            io::Reader reader(stream);
            fogColorLoaded = m_fogColorCurve->Deserialize(reader);
            if (!fogColorLoaded)
            {
                ELOG("Failed to load fog color curve");
            }
        }

        if (!fogColorLoaded)
        {
            m_fogColorCurve->Clear();
            m_fogColorCurve->AddKey(0.0f, Vector4(0.02f, 0.04f, 0.08f, 1.5f));     // Night
            m_fogColorCurve->AddKey(0.25f, Vector4(0.85f, 0.6f, 0.45f, 2.5f));     // Dawn
            m_fogColorCurve->AddKey(0.5f, Vector4(0.55f, 0.7f, 0.9f, 1.0f));       // Midday
            m_fogColorCurve->AddKey(0.75f, Vector4(0.85f, 0.55f, 0.4f, 2.0f));     // Dusk
            m_fogColorCurve->AddKey(1.0f, Vector4(0.02f, 0.04f, 0.08f, 1.5f));     // Night
            m_fogColorCurve->CalculateTangents();
        }

        m_sunScatterCurve = std::make_unique<ColorCurve>();
        bool sunScatterLoaded = false;
        if (const auto file = AssetRegistry::OpenFile("Models/SunScatter.hccv"))
        {
            io::StreamSource stream(*file);
            io::Reader reader(stream);
            sunScatterLoaded = m_sunScatterCurve->Deserialize(reader);
            if (!sunScatterLoaded)
            {
                ELOG("Failed to load sun scatter curve");
            }
        }

        if (!sunScatterLoaded)
        {
            m_sunScatterCurve->Clear();
            m_sunScatterCurve->AddKey(0.0f, Vector4(0.3f, 0.4f, 0.65f, 0.3f));     // Night (moon)
            m_sunScatterCurve->AddKey(0.25f, Vector4(1.0f, 0.7f, 0.4f, 1.0f));     // Dawn
            m_sunScatterCurve->AddKey(0.5f, Vector4(1.0f, 0.97f, 0.92f, 0.35f));   // Midday
            m_sunScatterCurve->AddKey(0.75f, Vector4(1.0f, 0.65f, 0.35f, 1.0f));   // Dusk
            m_sunScatterCurve->AddKey(1.0f, Vector4(0.3f, 0.4f, 0.65f, 0.3f));     // Night (moon)
            m_sunScatterCurve->CalculateTangents();
        }
```

In `UpdateLighting` replace

```cpp
        // Update fog color based on horizon color
        m_scene.SetFogColor(Vector3(horizonColor.x, horizonColor.y, horizonColor.z));
```

with

```cpp
        // Time-of-day fog values; the renderer multiplies them into the cvar/editor base values.
        const Vector4 fogColorKey = m_fogColorCurve->Evaluate(normalizedTime);
        const Vector4 sunScatterKey = m_sunScatterCurve->Evaluate(normalizedTime);

        AtmosphereTimeOfDay timeOfDay;
        timeOfDay.fogTint[0] = fogColorKey.x;
        timeOfDay.fogTint[1] = fogColorKey.y;
        timeOfDay.fogTint[2] = fogColorKey.z;
        timeOfDay.densityMultiplier = fogColorKey.w;
        timeOfDay.sunScatterColor[0] = sunScatterKey.x;
        timeOfDay.sunScatterColor[1] = sunScatterKey.y;
        timeOfDay.sunScatterColor[2] = sunScatterKey.z;
        timeOfDay.shaftMultiplier = sunScatterKey.w;
        m_scene.SetAtmosphereTimeOfDay(timeOfDay);
```

- [ ] **Step 7: Remove the legacy fog call sites**

- `world_state.cpp:1433`: delete `m_scene->SetFogRange(60.0f, 500.0f);`
- `world_editor_instance.cpp:126-129`: delete the `SetFogRange` line, the blank line, the `fogColor` line and the `SetFogColor` line.
- `world_editor_instance.cpp:2062-2063`: replace

```cpp
					m_scene.SetFogRange(10000.0f, 100000.0f);
					m_scene.Render(*renderCam, PixelShaderType::Forward);
```

with

```cpp
					// Minimap tiles are a top-down map, not a view through the atmosphere. (The old
					// SetFogRange here was never undone and silently removed the viewport fog.)
					const bool fogWasEnabled = m_scene.IsFogEnabled();
					m_scene.SetFogEnabled(false);
					m_scene.Render(*renderCam, PixelShaderType::Forward);
					m_scene.SetFogEnabled(fogWasEnabled);
```

- `world_settings_panel.cpp:172-177`: delete the `// Fog fade range controls` block (Task 7 adds the new sliders).

Search for leftovers; expected output is empty:

```powershell
git grep -n -e "SetFogRange" -e "SetFogColor" -e "GetFogStart" -e "GetFogEnd" -e "fogStart" -- src ':!src/shared/scene_graph/world_model*'
```

- [ ] **Step 8: Build and test**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t mmo_client mmo_edit deferred_shading_tests scene_graph_tests
.\bin\Debug\scene_graph_tests.exe
.\bin\Debug\deferred_shading_tests.exe
```

Expected: builds succeed (the `static_assert` on the 176-byte buffer holds), tests pass.

- [ ] **Step 9: Rebuild materials and check forward fog**

Run **Tools → Rebuild All Materials** in `mmo_edit` again. Launch the client at the baseline water view (view 1): water now fades into a peach/blue haze with distance while the terrain behind it stays crisp — expected until Task 5. Particles must still look as authored up close. In the editor, material and mesh previews look unchanged (fog is disabled in preview scenes).

- [ ] **Step 10: Commit**

```powershell
git add src/shared/deferred_shading/CMakeLists.txt src/shared/deferred_shading/shaders/AtmosphereCommon.hlsli src/shared/deferred_shading/shaders/PS_DeferredLighting.hlsl src/shared/scene_graph/scene.h src/shared/scene_graph/scene.cpp src/shared/graphics_d3d11/material_compiler_d3d11.cpp src/shared/graphics/sky_component.h src/shared/graphics/sky_component.cpp src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_editor_instance.cpp src/mmo_edit/editors/world_editor/world_settings_panel.cpp
git commit -m "feat(render): height fog parameters, time-of-day curves and forward fog"
```

Then show the user the list of material files the two rebuilds changed in `data/client` / `data/editor` and ask whether to commit them inside the submodules and bump the pointers (commit message `data: rebuild materials for linear HDR and height fog`). Do not include files that were already modified before Task 3.

---

### Task 5: AtmospherePass (shadowed light shafts and the fog composite)

**Files:**
- Create: `src/shared/deferred_shading/shaders/PS_AtmosphereMarch.hlsl`, `PS_AtmosphereBlur.hlsl`, `PS_AtmosphereComposite.hlsl`
- Create: `src/shared/deferred_shading/atmosphere_pass.h`, `atmosphere_pass.cpp`
- Modify: `src/shared/deferred_shading/deferred_renderer.h`, `deferred_renderer.cpp` (constructor, `Resize`, `Render` lines ~422-440, `RenderLightingPass` lines 631-636)
- Modify: `.gitignore`

**Interfaces:**
- Consumes: `AtmospherePassSettings` (Task 2), `AtmosphereCommon.hlsli` (Task 4), `NUM_SHADOW_CASCADES` from `cascaded_shadow_camera_setup.h`.
- Produces:
  - `class AtmospherePass { AtmospherePass(GraphicsDevice&, uint32, uint32); void Resize(uint32, uint32); void Render(Camera& camera, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output, const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer, ConstantBuffer& cameraBuffer, const std::function<void()>& bindShadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs); AtmospherePassSettings& GetSettings(); }`
  - `DeferredRenderer::SetAtmosphereQuality(int)`, `SetAtmosphereMarchDistance(float)`, `SetAtmosphereDebugMode(int)`; private `DeferredRenderer::BindShadowSampler()`

- [ ] **Step 1: Write the march shader**

`src/shared/deferred_shading/shaders/PS_AtmosphereMarch.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Reduced-resolution march of the height fog through the cascaded shadow maps. Output rgb is the
// light scattered toward the camera over the first MarchDistance metres of the view ray, a is the
// transmittance over that stretch. PS_AtmosphereComposite adds the closed-form remainder.

#include "AtmosphereCommon.hlsli"

static const uint NUM_SHADOW_CASCADES = 4;

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// G-Buffer normal target: rgb = normal, a = linear radial depth (0 where nothing was drawn).
Texture2D NormalTexture : register(t1);

Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

SamplerComparisonState ShadowSampler : register(s1);

// MUST stay field-for-field in sync with struct ShadowBuffer in deferred_renderer.cpp and the
// ShadowBuffer cbuffer in PS_DeferredLighting.hlsl. Only the leading fields are read here.
cbuffer ShadowBuffer : register(b3)
{
    column_major matrix CascadeViewProj[NUM_SHADOW_CASCADES];
    float4 CascadeSplitDistances;
    float ShadowBias;
    float NormalBiasScale;
    float ShadowSoftness;
    float BlockerSearchRadius;
    float LightSize;
    uint CascadeCount;
};

// MUST stay in sync with AtmosphereMarchConstants in atmosphere_pass.cpp.
cbuffer AtmosphereMarchBuffer : register(b2)
{
    float MarchDistance;
    float SkyDistance;
    uint StepCount;
    uint Divisor;
    float2 FullResolution;
    uint DebugMode;
    float _MarchPadding;
};

// One hardware-PCF tap in the cascade covering this radial distance. 1 = lit.
float SampleSunVisibility(float3 worldPos, float distanceFromCamera)
{
    if (CascadeCount == 0)
    {
        return 1.0f;
    }

    uint cascade = CascadeCount - 1;
    for (uint i = 0; i < CascadeCount; ++i)
    {
        if (distanceFromCamera <= CascadeSplitDistances[i])
        {
            cascade = i;
            break;
        }
    }

    float4 lightSpace = mul(float4(worldPos, 1.0f), CascadeViewProj[cascade]);
    if (lightSpace.w <= 0.0f)
    {
        return 1.0f;
    }

    float3 ndc = lightSpace.xyz / lightSpace.w;
    float2 uv = ndc.xy * float2(1.0f, -1.0f) * 0.5f + 0.5f;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return 1.0f;
    }

    float compareDepth = ndc.z - ShadowBias;
    switch (cascade)
    {
        case 0: return ShadowMapCascade0.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
        case 1: return ShadowMapCascade1.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
        case 2: return ShadowMapCascade2.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
        default: return ShadowMapCascade3.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
    }
}

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 lowPixel = int2(input.Position.xy);

    // The composite and the blur look up this texel's depth at the same full-resolution pixel,
    // which keeps the bilateral weights consistent with what was marched.
    int2 fullPixel = lowPixel * int(Divisor);
    float depth = NormalTexture.Load(int3(fullPixel, 0)).a;
    float sceneDistance = depth <= 0.0f ? SkyDistance : depth;
    float marchLength = min(sceneDistance, MarchDistance);

    if (marchLength <= 0.0f || StepCount == 0)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float2 uv = (float2(fullPixel) + 0.5f) / FullResolution;
    float3 rayDir = WorldViewRay(uv);
    float cosTheta = dot(rayDir, SunDirection);

    // Static per-pixel offset: without temporal accumulation a per-frame offset would shimmer.
    float jitter = InterleavedGradientNoise(float2(lowPixel));
    float stepCount = float(StepCount);

    float3 inscatter = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;
    float visibilitySum = 0.0f;

    [loop]
    for (uint i = 0; i < StepCount; ++i)
    {
        // Quadratic spacing puts most samples near the camera, where shafts are readable.
        float segmentStart = marchLength * pow(float(i) / stepCount, 2.0f);
        float segmentEnd = marchLength * pow(float(i + 1) / stepCount, 2.0f);
        float sampleDistance = marchLength * pow((float(i) + jitter) / stepCount, 2.0f);

        float visibility = SampleSunVisibility(CameraPosition + rayDir * sampleDistance, sampleDistance);
        visibilitySum += visibility;

        float segmentTau = FogOpticalDepth(CameraPosition.y + rayDir.y * segmentStart, rayDir.y, segmentEnd - segmentStart);
        float segmentTransmittance = exp(-segmentTau);

        inscatter += transmittance * FogSource(cosTheta, visibility) * (1.0f - segmentTransmittance);
        transmittance *= segmentTransmittance;
    }

    if (DebugMode == 3)
    {
        return float4((visibilitySum / stepCount).xxx, 1.0f);
    }

    return float4(inscatter, transmittance);
}
```

- [ ] **Step 2: Write the blur shader**

`src/shared/deferred_shading/shaders/PS_AtmosphereBlur.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Separable depth-aware blur of the atmosphere march target. Resolves the static jitter pattern
// without smearing shafts across depth discontinuities such as tree trunks against the sky.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D MarchTexture : register(t0);
Texture2D NormalTexture : register(t1);

// MUST stay in sync with AtmosphereBlurConstants in atmosphere_pass.cpp.
cbuffer AtmosphereBlurBuffer : register(b2)
{
    float2 Direction;
    int2 LowResSize;
    uint Divisor;
    float3 _BlurPadding;
};

static const float BLUR_WEIGHTS[4] = { 0.2270270f, 0.1945946f, 0.1216216f, 0.0540541f };

float LowResDistance(int2 lowPixel)
{
    float depth = NormalTexture.Load(int3(lowPixel * int(Divisor), 0)).a;
    return depth <= 0.0f ? 100000.0f : depth;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 center = int2(input.Position.xy);
    float centerDistance = LowResDistance(center);
    int2 stepDirection = int2(Direction);

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;

    [unroll]
    for (int k = -3; k <= 3; ++k)
    {
        int2 tapPixel = clamp(center + stepDirection * k, int2(0, 0), LowResSize - 1);
        float tapDistance = LowResDistance(tapPixel);
        float depthWeight = exp(-abs(tapDistance - centerDistance) / max(centerDistance * 0.05f, 0.25f));
        float weight = BLUR_WEIGHTS[abs(k)] * depthWeight;

        sum += MarchTexture.Load(int3(tapPixel, 0)) * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 1e-5f);
}
```

- [ ] **Step 3: Write the composite shader**

`src/shared/deferred_shading/shaders/PS_AtmosphereComposite.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Full-resolution atmosphere composite: bilaterally upsamples the march result and adds the
// closed-form height fog for the rest of each view ray, including everything past the shadow
// range and the sky.

#include "AtmosphereCommon.hlsli"

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D MarchTexture : register(t2);

// MUST stay in sync with AtmosphereCompositeConstants in atmosphere_pass.cpp.
cbuffer AtmosphereCompositeBuffer : register(b2)
{
    float MarchDistance;    // 0 when the march did not run
    float SkyDistance;
    uint Divisor;
    uint DebugMode;
    int2 LowResSize;
    float2 _CompositePadding;
};

float DistanceOrSky(float depth)
{
    return depth <= 0.0f ? SkyDistance : depth;
}

float4 UpsampleMarch(int2 pixel, float pixelDistance)
{
    if (Divisor <= 1)
    {
        return MarchTexture.Load(int3(min(pixel, LowResSize - 1), 0));
    }

    uint fullWidth;
    uint fullHeight;
    NormalTexture.GetDimensions(fullWidth, fullHeight);

    float2 lowPosition = (float2(pixel) + 0.5f) / float(Divisor) - 0.5f;
    int2 basePixel = int2(floor(lowPosition));
    float2 fraction = lowPosition - float2(basePixel);

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    float bestDelta = 1e20f;
    float4 nearest = float4(0.0f, 0.0f, 0.0f, 1.0f);

    [unroll]
    for (int y = 0; y <= 1; ++y)
    {
        [unroll]
        for (int x = 0; x <= 1; ++x)
        {
            int2 lowPixel = clamp(basePixel + int2(x, y), int2(0, 0), LowResSize - 1);
            int2 fullPixel = min(lowPixel * int(Divisor), int2(fullWidth, fullHeight) - 1);
            float neighbourDistance = DistanceOrSky(NormalTexture.Load(int3(fullPixel, 0)).a);

            float bilinear = (x == 0 ? 1.0f - fraction.x : fraction.x) * (y == 0 ? 1.0f - fraction.y : fraction.y);
            float delta = abs(neighbourDistance - pixelDistance);
            float depthWeight = 1.0f / (1e-3f + delta / max(pixelDistance, 1.0f));

            float4 value = MarchTexture.Load(int3(lowPixel, 0));
            sum += value * bilinear * depthWeight;
            weightSum += bilinear * depthWeight;

            if (delta < bestDelta)
            {
                bestDelta = delta;
                nearest = value;
            }
        }
    }

    return weightSum > 1e-4f ? sum / weightSum : nearest;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 pixel = int2(input.Position.xy);
    float4 scene = SceneTexture.Load(int3(pixel, 0));
    float sceneDistance = DistanceOrSky(NormalTexture.Load(int3(pixel, 0)).a);

    float3 rayDir = WorldViewRay(input.TexCoord);
    float nearDistance = min(sceneDistance, MarchDistance);
    float4 march = MarchDistance > 0.0f ? UpsampleMarch(pixel, sceneDistance) : float4(0.0f, 0.0f, 0.0f, 1.0f);

    float tailTau = FogOpticalDepth(CameraPosition.y + rayDir.y * nearDistance, rayDir.y, sceneDistance - nearDistance);
    float tailTransmittance = exp(-tailTau);
    float3 tailInscatter = march.a * FogSource(dot(rayDir, SunDirection), 1.0f) * (1.0f - tailTransmittance);

    if (DebugMode == 1)
    {
        return float4(march.rgb + tailInscatter, 1.0f);
    }

    if (DebugMode == 2)
    {
        return float4((march.a * tailTransmittance).xxx, 1.0f);
    }

    if (DebugMode == 3)
    {
        return float4(march.rgb, 1.0f);
    }

    return float4(scene.rgb * march.a * tailTransmittance + march.rgb + tailInscatter, scene.a);
}
```

Add to `.gitignore`:

```
/src/shared/deferred_shading/shaders/PS_AtmosphereMarch.h
/src/shared/deferred_shading/shaders/PS_AtmosphereBlur.h
/src/shared/deferred_shading/shaders/PS_AtmosphereComposite.h
```

- [ ] **Step 4: Write the AtmospherePass class**

`src/shared/deferred_shading/atmosphere_pass.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "atmosphere_pass_settings.h"
#include "cascaded_shadow_camera_setup.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <array>
#include <functional>

namespace mmo
{
	class Camera;

	/// @brief Height fog and shadowed light shafts over the lit opaque scene.
	/// @remark Marches the cascaded shadow maps at reduced resolution, blurs the result with a
	///         depth-aware filter, and composites it at full resolution together with the closed-form
	///         fog for the rest of each view ray. With the march disabled only the closed form runs.
	/// @remark Backend-neutral like SsaoPass; the one D3D11 dependency, the comparison sampler, is
	///         bound by the caller through a callback.
	class AtmospherePass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass.
		AtmospherePass(GraphicsDevice& device, uint32 width, uint32 height);

		~AtmospherePass() override = default;

	public:
		/// @brief Records a new G-Buffer size. March targets are rebuilt lazily.
		void Resize(uint32 width, uint32 height);

		/// @brief Composites the atmosphere onto sceneColor, writing the result into output.
		/// @param camera Camera the frame is rendered with.
		/// @param sceneColor The lit opaque scene (linear HDR). Read only.
		/// @param gbufferNormalRT G-Buffer normal target (a = radial depth).
		/// @param output Full-resolution target receiving the fogged scene. Must differ from sceneColor.
		/// @param cascadeShadowMaps The cascade depth maps.
		/// @param shadowBuffer The ShadowBuffer cbuffer already filled for this frame.
		/// @param cameraBuffer The scene camera cbuffer already refreshed for this camera.
		/// @param bindShadowSampler Binds the comparison sampler at s1; called right before the march draw.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		void Render(Camera& camera, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
			const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
			ConstantBuffer& cameraBuffer, const std::function<void()>& bindShadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the mutable settings.
		[[nodiscard]] AtmospherePassSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const AtmospherePassSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief (Re)creates the march and blur targets for the current divisor and G-Buffer size.
		void EnsureTargets();

		/// @brief Releases the march and blur targets.
		void ReleaseTargets();

	private:
		GraphicsDevice& m_device;

		AtmospherePassSettings m_settings;

		uint32 m_gbufferWidth;
		uint32 m_gbufferHeight;

		uint32 m_targetWidth = 0;
		uint32 m_targetHeight = 0;
		uint32 m_targetDivisor = 0;

		RenderTexturePtr m_marchRT;
		RenderTexturePtr m_blurRT;

		/// @brief 1x1 (0, 0, 0, 1) stand-in for the march result: no in-scatter, full transmittance.
		TexturePtr m_neutralTexture;

		ConstantBufferPtr m_marchBuffer;
		ConstantBufferPtr m_blurBuffer;
		ConstantBufferPtr m_compositeBuffer;

		ShaderPtr m_marchPs;
		ShaderPtr m_blurPs;
		ShaderPtr m_compositePs;
	};
}
```

`src/shared/deferred_shading/atmosphere_pass.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "atmosphere_pass.h"

#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_AtmosphereMarch.h"
#	include "shaders/PS_AtmosphereBlur.h"
#	include "shaders/PS_AtmosphereComposite.h"
#	define MMO_ATMOSPHERE_MARCH_PS_BYTECODE g_PS_AtmosphereMarch
#	define MMO_ATMOSPHERE_MARCH_PS_SIZE std::size(g_PS_AtmosphereMarch)
#	define MMO_ATMOSPHERE_BLUR_PS_BYTECODE g_PS_AtmosphereBlur
#	define MMO_ATMOSPHERE_BLUR_PS_SIZE std::size(g_PS_AtmosphereBlur)
#	define MMO_ATMOSPHERE_COMPOSITE_PS_BYTECODE g_PS_AtmosphereComposite
#	define MMO_ATMOSPHERE_COMPOSITE_PS_SIZE std::size(g_PS_AtmosphereComposite)
#else
#	define MMO_ATMOSPHERE_MARCH_PS_BYTECODE nullptr
#	define MMO_ATMOSPHERE_MARCH_PS_SIZE 0
#	define MMO_ATMOSPHERE_BLUR_PS_BYTECODE nullptr
#	define MMO_ATMOSPHERE_BLUR_PS_SIZE 0
#	define MMO_ATMOSPHERE_COMPOSITE_PS_BYTECODE nullptr
#	define MMO_ATMOSPHERE_COMPOSITE_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		/// @brief Mirrors AtmosphereMarchBuffer in PS_AtmosphereMarch.hlsl (b2).
		struct alignas(16) AtmosphereMarchConstants
		{
			float marchDistance;
			float skyDistance;
			uint32 stepCount;
			uint32 divisor;

			float fullWidth;
			float fullHeight;
			uint32 debugMode;
			float padding0;
		};

		/// @brief Mirrors AtmosphereBlurBuffer in PS_AtmosphereBlur.hlsl (b2).
		struct alignas(16) AtmosphereBlurConstants
		{
			float directionX;
			float directionY;
			int32 lowWidth;
			int32 lowHeight;

			uint32 divisor;
			float padding0;
			float padding1;
			float padding2;
		};

		/// @brief Mirrors AtmosphereCompositeBuffer in PS_AtmosphereComposite.hlsl (b2).
		struct alignas(16) AtmosphereCompositeConstants
		{
			float marchDistance;
			float skyDistance;
			uint32 divisor;
			uint32 debugMode;

			int32 lowWidth;
			int32 lowHeight;
			float padding0;
			float padding1;
		};

		static_assert(sizeof(AtmosphereMarchConstants) == 32, "AtmosphereMarchConstants must match the HLSL layout");
		static_assert(sizeof(AtmosphereBlurConstants) == 32, "AtmosphereBlurConstants must match the HLSL layout");
		static_assert(sizeof(AtmosphereCompositeConstants) == 32, "AtmosphereCompositeConstants must match the HLSL layout");
	}

	AtmospherePass::AtmospherePass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_gbufferWidth(width)
		, m_gbufferHeight(height)
	{
		m_marchBuffer = m_device.CreateConstantBuffer(sizeof(AtmosphereMarchConstants), nullptr);
		m_blurBuffer = m_device.CreateConstantBuffer(sizeof(AtmosphereBlurConstants), nullptr);
		m_compositeBuffer = m_device.CreateConstantBuffer(sizeof(AtmosphereCompositeConstants), nullptr);
		ASSERT(m_marchBuffer && m_blurBuffer && m_compositeBuffer);

		m_marchPs = m_device.CreateShader(ShaderType::PixelShader, MMO_ATMOSPHERE_MARCH_PS_BYTECODE, MMO_ATMOSPHERE_MARCH_PS_SIZE);
		m_blurPs = m_device.CreateShader(ShaderType::PixelShader, MMO_ATMOSPHERE_BLUR_PS_BYTECODE, MMO_ATMOSPHERE_BLUR_PS_SIZE);
		m_compositePs = m_device.CreateShader(ShaderType::PixelShader, MMO_ATMOSPHERE_COMPOSITE_PS_BYTECODE, MMO_ATMOSPHERE_COMPOSITE_PS_SIZE);
		ASSERT(m_marchPs && m_blurPs && m_compositePs);

		m_neutralTexture = m_device.CreateTexture(1, 1, BufferUsage::Static);
		ASSERT(m_neutralTexture);
		uint32 neutralPixel = 0xFF000000;
		m_neutralTexture->LoadRaw(&neutralPixel, sizeof(neutralPixel));
		m_neutralTexture->SetDebugName("AtmosphereNoMarch");
	}

	void AtmospherePass::Resize(const uint32 width, const uint32 height)
	{
		m_gbufferWidth = width;
		m_gbufferHeight = height;
		ReleaseTargets();
	}

	void AtmospherePass::ReleaseTargets()
	{
		m_marchRT.reset();
		m_blurRT.reset();
		m_targetWidth = 0;
		m_targetHeight = 0;
		m_targetDivisor = 0;
	}

	void AtmospherePass::EnsureTargets()
	{
		const uint32 divisor = m_settings.resolutionDivisor > 0 ? m_settings.resolutionDivisor : 1u;
		const uint32 desiredWidth = m_gbufferWidth / divisor > 0 ? m_gbufferWidth / divisor : 1u;
		const uint32 desiredHeight = m_gbufferHeight / divisor > 0 ? m_gbufferHeight / divisor : 1u;

		if (m_marchRT && m_targetWidth == desiredWidth && m_targetHeight == desiredHeight && m_targetDivisor == divisor)
		{
			return;
		}

		m_marchRT = m_device.CreateRenderTexture("AtmosphereMarch", static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		m_blurRT = m_device.CreateRenderTexture("AtmosphereBlur", static_cast<uint16>(desiredWidth), static_cast<uint16>(desiredHeight),
			RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16);
		ASSERT(m_marchRT && m_blurRT);

		m_targetWidth = desiredWidth;
		m_targetHeight = desiredHeight;
		m_targetDivisor = divisor;
	}

	void AtmospherePass::Render(Camera& camera, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
		const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
		ConstantBuffer& cameraBuffer, const std::function<void()>& bindShadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		const bool marchEnabled = m_settings.IsMarchEnabled();
		if (marchEnabled)
		{
			EnsureTargets();
		}
		else if (m_marchRT)
		{
			// Off: hold no targets, so a disabled march costs no video memory.
			ReleaseTargets();
		}

		// The shaders rebuild view rays from b12.
		m_device.SetTransformMatrix(World, Matrix4::Identity);
		m_device.SetTransformMatrix(View, camera.GetViewMatrix());
		m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::None);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		quad.Set(0);
		cameraBuffer.BindToStage(ShaderType::PixelShader, 1);

		if (marchEnabled)
		{
			// --- March ------------------------------------------------------------------
			AtmosphereMarchConstants marchConstants{};
			marchConstants.marchDistance = m_settings.marchDistance;
			marchConstants.skyDistance = m_settings.skyDistance;
			marchConstants.stepCount = m_settings.stepCount;
			marchConstants.divisor = m_targetDivisor;
			marchConstants.fullWidth = static_cast<float>(m_gbufferWidth);
			marchConstants.fullHeight = static_cast<float>(m_gbufferHeight);
			marchConstants.debugMode = m_settings.debugMode;
			m_marchBuffer->Update(&marchConstants);

			m_marchRT->Activate();
			m_marchRT->Clear(ClearFlags::Color);
			m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);

			gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				cascadeShadowMaps[i]->Bind(ShaderType::PixelShader, 5 + i);
			}

			shadowBuffer.BindToStage(ShaderType::PixelShader, 3);
			m_marchBuffer->BindToStage(ShaderType::PixelShader, 2);
			m_marchPs->Set();

			// Last, so no device state call above can replace the comparison sampler.
			bindShadowSampler();
			m_device.Draw(6, 0);

			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				m_device.BindTexture(nullptr, ShaderType::PixelShader, 5 + i);
			}

			// --- Blur: march -> blur (horizontal) -> march (vertical) --------------------
			m_blurPs->Set();
			m_blurBuffer->BindToStage(ShaderType::PixelShader, 2);

			AtmosphereBlurConstants blurConstants{};
			blurConstants.lowWidth = static_cast<int32>(m_targetWidth);
			blurConstants.lowHeight = static_cast<int32>(m_targetHeight);
			blurConstants.divisor = m_targetDivisor;

			for (uint32 iteration = 0; iteration < m_settings.blurIterations; ++iteration)
			{
				blurConstants.directionX = 1.0f;
				blurConstants.directionY = 0.0f;
				m_blurBuffer->Update(&blurConstants);
				m_blurRT->Activate();
				m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
				m_device.BindTexture(m_marchRT, ShaderType::PixelShader, 0);
				m_device.Draw(6, 0);
				m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);

				blurConstants.directionX = 0.0f;
				blurConstants.directionY = 1.0f;
				m_blurBuffer->Update(&blurConstants);
				m_marchRT->Activate();
				m_device.SetViewport(0, 0, static_cast<int32>(m_targetWidth), static_cast<int32>(m_targetHeight), 0.0f, 1.0f);
				m_device.BindTexture(m_blurRT, ShaderType::PixelShader, 0);
				m_device.Draw(6, 0);
				m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
			}

			m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		}

		// --- Composite ------------------------------------------------------------------
		AtmosphereCompositeConstants compositeConstants{};
		compositeConstants.marchDistance = marchEnabled ? m_settings.marchDistance : 0.0f;
		compositeConstants.skyDistance = m_settings.skyDistance;
		compositeConstants.divisor = marchEnabled ? m_targetDivisor : 1u;
		compositeConstants.debugMode = m_settings.debugMode;
		compositeConstants.lowWidth = marchEnabled ? static_cast<int32>(m_targetWidth) : 1;
		compositeConstants.lowHeight = marchEnabled ? static_cast<int32>(m_targetHeight) : 1;
		m_compositeBuffer->Update(&compositeConstants);

		output.Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_gbufferWidth), static_cast<int32>(m_gbufferHeight), 0.0f, 1.0f);

		sceneColor.Bind(ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		const TexturePtr marchResult = marchEnabled ? TexturePtr(m_marchRT) : m_neutralTexture;
		m_device.BindTexture(marchResult, ShaderType::PixelShader, 2);

		m_compositeBuffer->BindToStage(ShaderType::PixelShader, 2);
		m_compositePs->Set();
		m_device.Draw(6, 0);

		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 2);
	}
}
```

- [ ] **Step 5: Wire the pass into DeferredRenderer**

`deferred_renderer.h`: add `#include "atmosphere_pass.h"`; member after `m_contactShadowPass`:

```cpp
        /// @brief Height fog and light shafts. Runs after lighting, before the forward pass.
        std::unique_ptr<AtmospherePass> m_atmospherePass;
```

private declaration next to `FillShadowBufferCommonSettings`:

```cpp
        /// @brief Binds the cascade comparison sampler at pixel shader slot s1.
        void BindShadowSampler();
```

public setters after `SetExposure`:

```cpp
        /// @brief Applies the atmosphere quality preset: 0 Off (closed-form fog only) ... 4 Ultra.
        void SetAtmosphereQuality(int level) { m_atmospherePass->GetSettings().ApplyQualityLevel(level); }

        /// @brief Sets how many metres of each view ray are shadow-marched (clamped to [0, 300]).
        void SetAtmosphereMarchDistance(float distance) { m_atmospherePass->GetSettings().SetMarchDistance(distance); }

        /// @brief Sets the atmosphere debug view: 0 off, 1 in-scatter, 2 transmittance, 3 march shadow term.
        void SetAtmosphereDebugMode(int mode) { m_atmospherePass->GetSettings().SetDebugMode(mode); }
```

`deferred_renderer.cpp`:
- add `#include "atmosphere_pass.h"` with the pass includes;
- constructor: `m_atmospherePass = std::make_unique<AtmospherePass>(m_device, width, height);` after `m_contactShadowPass`;
- `Resize`: `m_atmospherePass->Resize(width, height);`
- add the helper after `FillShadowBufferCommonSettings`:

```cpp
    void DeferredRenderer::BindShadowSampler()
    {
#ifdef WIN32
        GraphicsDeviceD3D11& d3ddev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
        ID3D11DeviceContext& d3d11ctx = d3ddev;
        ID3D11SamplerState* samplers[1] = { m_shadowSampler.Get() };
        d3d11ctx.PSSetSamplers(1, 1, samplers);
#endif
    }
```

- in `RenderLightingPass` replace the `#ifdef WIN32 … PSSetSamplers … #endif` block (lines 631-636) with `BindShadowSampler();`
- in `Render`, replace the block from `m_sceneColorCopy->ApplyPendingResize();` through the closing `#endif` of the `CopyResource` block (lines 430-440) with:

```cpp
        m_sceneColorCopy->ApplyPendingResize();

        // Height fog and light shafts over the lit opaque scene. The composite reads m_renderTexture
        // and writes m_sceneColorCopy, which stays the refraction source; the single CopyResource
        // below then carries the fogged scene back into m_renderTexture for the forward pass.
        // Skipped while submerged (the underwater pass has its own fog) or with fog turned off; the
        // copy then runs in its old direction.
        const bool runAtmosphere = scene.IsFogEnabled() && !m_underwaterState.active;
        if (runAtmosphere)
        {
            m_atmospherePass->Render(camera, *m_renderTexture, m_gBuffer.GetNormalRT(), *m_sceneColorCopy,
                m_cascadeShadowMaps, *m_shadowBuffer, *scene.GetCameraBuffer(),
                [this]() { BindShadowSampler(); }, *m_quadBuffer, *m_deferredLightVs);
        }

#ifdef WIN32
        {
            GraphicsDeviceD3D11& d3dDev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
            ID3D11DeviceContext& d3dCtx = d3dDev;
            auto* sceneRt = static_cast<RenderTextureD3D11*>(m_renderTexture.get());
            auto* copyRt = static_cast<RenderTextureD3D11*>(m_sceneColorCopy.get());
            if (runAtmosphere)
            {
                d3dCtx.CopyResource(sceneRt->GetTex2D(), copyRt->GetTex2D());
            }
            else
            {
                d3dCtx.CopyResource(copyRt->GetTex2D(), sceneRt->GetTex2D());
            }
        }
#endif
```

Keep the long explanatory comment about `ApplyPendingResize` that precedes this block.

- [ ] **Step 6: Build**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t mmo_client mmo_edit deferred_shading_tests
.\bin\Debug\deferred_shading_tests.exe
```

Expected: success; FXC errors show up as build errors naming the `.hlsl` line.

- [ ] **Step 7: Visual verification**

Launch the client (fog is enabled by `WorldState`; defaults: density 0.02, quality High). Check, capturing screenshots with `capture_client.ps1` into `build/render_compare/task5_*.png`:
1. A forest area facing a low sun (dawn or dusk; use a GM time command if available, otherwise wait for it): visible shafts between trunks, a warm glow around the sun, trees silhouetted.
2. The same area side-lit and with the sun behind the camera: shafts still visible where light crosses shadowed air; no glow behind.
3. Terrain edges against the sky and trunks in front of distant hills: no halos wider than about 2 pixels.
4. Water surface in view: its fog matches the terrain fog behind it (no brighter or darker band).
5. Underwater: no atmosphere, underwater effect unchanged; surfacing: fog returns immediately.
6. Editor world viewport with "Show Fog" on and off; material preview unchanged.

If shafts are absent: check that tree canopies cast into the CSM (`RenderShadows 1`), that `CascadeCount` is non-zero, and — before the Task 7 cvars exist — temporarily set `m_settings.debugMode = 3` in the pass constructor to view the march shadow term (revert afterwards). If the image goes black or white: the camera cbuffer layout is out of sync (compare `scene.cpp`, `AtmosphereCommon.hlsli` and the material compiler field by field).

- [ ] **Step 8: Commit**

```powershell
git add .gitignore src/shared/deferred_shading/atmosphere_pass.h src/shared/deferred_shading/atmosphere_pass.cpp src/shared/deferred_shading/shaders/PS_AtmosphereMarch.hlsl src/shared/deferred_shading/shaders/PS_AtmosphereBlur.hlsl src/shared/deferred_shading/shaders/PS_AtmosphereComposite.hlsl src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp
git commit -m "feat(render): ray-marched height fog and light shafts"
```

---

### Task 6: BloomPass

**Files:**
- Create: `src/shared/deferred_shading/shaders/PS_BloomDownsample.hlsl`, `PS_BloomUpsample.hlsl`
- Create: `src/shared/deferred_shading/bloom_pass.h`, `bloom_pass.cpp`
- Modify: `src/shared/deferred_shading/deferred_renderer.h`, `deferred_renderer.cpp`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: `BloomSettings` (Task 2); `TonemapPass::Render(RenderTexture&, const TexturePtr& bloom, float bloomScale, …)` (Task 3).
- Produces:
  - `class BloomPass { BloomPass(GraphicsDevice&, uint32, uint32); void Resize(uint32, uint32); void Render(RenderTexture& sceneColor, VertexBuffer& quad, ShaderBase& fullscreenVs); TexturePtr GetResult() const; float GetResultScale() const; BloomSettings& GetSettings(); }` — `GetResult()` is nullptr when disabled; `GetResultScale()` is `intensity / levelCount`.
  - `DeferredRenderer::SetBloomQuality(int)`, `SetBloomIntensity(float)`, `SetBloomThreshold(float)`

- [ ] **Step 1: Write the bloom shaders**

`src/shared/deferred_shading/shaders/PS_BloomDownsample.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare").
// The first level Karis-averages each 2x2 group so single bright texels cannot flicker, and applies
// the soft brightness threshold.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

// MUST stay in sync with BloomDownsampleConstants in bloom_pass.cpp.
cbuffer BloomDownsampleBuffer : register(b2)
{
    float2 SourceTexelSize;     // tap spacing in source UV units
    uint ApplyPrefilter;        // 1 on the first level only
    float Threshold;
    float Knee;
    float3 _DownsamplePadding;
};

float3 Tap(float2 uv)
{
    return SourceTexture.SampleLevel(LinearSampler, uv, 0).rgb;
}

float KarisWeight(float3 color)
{
    return 1.0f / (1.0f + dot(color, float3(0.2126f, 0.7152f, 0.0722f)));
}

float3 KarisAverage(float3 a, float3 b, float3 c, float3 d)
{
    float wa = KarisWeight(a);
    float wb = KarisWeight(b);
    float wc = KarisWeight(c);
    float wd = KarisWeight(d);
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

float3 SoftThreshold(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - Threshold + Knee, 0.0f, 2.0f * Knee);
    soft = soft * soft / (4.0f * Knee + 1e-5f);
    float contribution = max(soft, brightness - Threshold) / max(brightness, 1e-5f);
    return color * contribution;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 t = SourceTexelSize;

    float3 a = Tap(uv + t * float2(-2.0f, -2.0f));
    float3 b = Tap(uv + t * float2( 0.0f, -2.0f));
    float3 c = Tap(uv + t * float2( 2.0f, -2.0f));
    float3 d = Tap(uv + t * float2(-1.0f, -1.0f));
    float3 e = Tap(uv + t * float2( 1.0f, -1.0f));
    float3 f = Tap(uv + t * float2(-2.0f,  0.0f));
    float3 g = Tap(uv);
    float3 h = Tap(uv + t * float2( 2.0f,  0.0f));
    float3 i = Tap(uv + t * float2(-1.0f,  1.0f));
    float3 j = Tap(uv + t * float2( 1.0f,  1.0f));
    float3 k = Tap(uv + t * float2(-2.0f,  2.0f));
    float3 l = Tap(uv + t * float2( 0.0f,  2.0f));
    float3 m = Tap(uv + t * float2( 2.0f,  2.0f));

    float3 result;
    if (ApplyPrefilter != 0)
    {
        result = KarisAverage(d, e, i, j) * 0.5f
            + KarisAverage(a, b, f, g) * 0.125f
            + KarisAverage(b, c, g, h) * 0.125f
            + KarisAverage(f, g, k, l) * 0.125f
            + KarisAverage(g, h, l, m) * 0.125f;
        result = SoftThreshold(result);
    }
    else
    {
        result = (d + e + i + j) * 0.125f
            + (a + b + f + g) * 0.03125f
            + (b + c + g + h) * 0.03125f
            + (f + g + k + l) * 0.03125f
            + (g + h + l + m) * 0.03125f;
    }

    return float4(max(result, 0.0f), 1.0f);
}
```

`src/shared/deferred_shading/shaders/PS_BloomUpsample.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// 3x3 tent upsample of the coarser bloom level, added to the downsample of the current level.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D LowerTexture : register(t0);      // coarser level (upsample result or last downsample)
Texture2D CurrentTexture : register(t1);    // downsample at this level
SamplerState LinearSampler : register(s0);

// MUST stay in sync with BloomUpsampleConstants in bloom_pass.cpp.
cbuffer BloomUpsampleBuffer : register(b2)
{
    float2 LowerTexelSize;
    float Radius;
    float _UpsamplePadding;
};

float3 Tap(float2 uv)
{
    return LowerTexture.SampleLevel(LinearSampler, uv, 0).rgb;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 t = LowerTexelSize * Radius;

    float3 tent = Tap(uv + float2(-t.x, -t.y))
        + Tap(uv + float2(0.0f, -t.y)) * 2.0f
        + Tap(uv + float2(t.x, -t.y))
        + Tap(uv + float2(-t.x, 0.0f)) * 2.0f
        + Tap(uv) * 4.0f
        + Tap(uv + float2(t.x, 0.0f)) * 2.0f
        + Tap(uv + float2(-t.x, t.y))
        + Tap(uv + float2(0.0f, t.y)) * 2.0f
        + Tap(uv + float2(t.x, t.y));

    float3 current = CurrentTexture.SampleLevel(LinearSampler, uv, 0).rgb;
    return float4(current + tent / 16.0f, 1.0f);
}
```

Add to `.gitignore`:

```
/src/shared/deferred_shading/shaders/PS_BloomDownsample.h
/src/shared/deferred_shading/shaders/PS_BloomUpsample.h
```

- [ ] **Step 2: Write the BloomPass class**

`src/shared/deferred_shading/bloom_pass.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bloom_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <vector>

namespace mmo
{
	/// @brief Physically-inspired bloom: a 13-tap downsample chain and a tent-filter upsample chain
	///        over the linear HDR scene. The TonemapPass adds the result.
	/// @remark Backend-neutral like SsaoPass; the shader blob choice is isolated in bloom_pass.cpp.
	class BloomPass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass. Targets are allocated on the first Render.
		BloomPass(GraphicsDevice& device, uint32 width, uint32 height);

		~BloomPass() override = default;

	public:
		/// @brief Records a new scene size. Targets are rebuilt lazily.
		void Resize(uint32 width, uint32 height);

		/// @brief Builds the bloom texture from the linear HDR scene.
		/// @remark Does nothing (and releases its targets) when the settings disable bloom.
		void Render(RenderTexture& sceneColor, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the bloom texture, or nullptr when bloom is disabled.
		[[nodiscard]] TexturePtr GetResult() const;

		/// @brief Gets the weight the TonemapPass applies: intensity divided by the level count,
		///        because every level adds its own copy of the light.
		[[nodiscard]] float GetResultScale() const;

		/// @brief Gets the mutable settings.
		[[nodiscard]] BloomSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const BloomSettings& GetSettings() const { return m_settings; }

	private:
		void EnsureTargets();
		void ReleaseTargets();

	private:
		GraphicsDevice& m_device;

		BloomSettings m_settings;

		uint32 m_sceneWidth;
		uint32 m_sceneHeight;

		/// @brief Configuration the current targets were built for.
		uint32 m_builtDivisor = 0;
		uint32 m_builtLevels = 0;
		uint32 m_builtWidth = 0;
		uint32 m_builtHeight = 0;

		std::vector<RenderTexturePtr> m_downTargets;
		std::vector<RenderTexturePtr> m_upTargets;
		std::vector<uint32> m_levelWidths;
		std::vector<uint32> m_levelHeights;

		ConstantBufferPtr m_downsampleBuffer;
		ConstantBufferPtr m_upsampleBuffer;

		ShaderPtr m_downsamplePs;
		ShaderPtr m_upsamplePs;
	};
}
```

`src/shared/deferred_shading/bloom_pass.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bloom_pass.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/PS_BloomDownsample.h"
#	include "shaders/PS_BloomUpsample.h"
#	define MMO_BLOOM_DOWN_PS_BYTECODE g_PS_BloomDownsample
#	define MMO_BLOOM_DOWN_PS_SIZE std::size(g_PS_BloomDownsample)
#	define MMO_BLOOM_UP_PS_BYTECODE g_PS_BloomUpsample
#	define MMO_BLOOM_UP_PS_SIZE std::size(g_PS_BloomUpsample)
#else
#	define MMO_BLOOM_DOWN_PS_BYTECODE nullptr
#	define MMO_BLOOM_DOWN_PS_SIZE 0
#	define MMO_BLOOM_UP_PS_BYTECODE nullptr
#	define MMO_BLOOM_UP_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

#include <string>

namespace mmo
{
	namespace
	{
		/// @brief Mirrors BloomDownsampleBuffer in PS_BloomDownsample.hlsl (b2).
		struct alignas(16) BloomDownsampleConstants
		{
			float texelWidth;
			float texelHeight;
			uint32 applyPrefilter;
			float threshold;

			float knee;
			float padding0;
			float padding1;
			float padding2;
		};

		/// @brief Mirrors BloomUpsampleBuffer in PS_BloomUpsample.hlsl (b2).
		struct alignas(16) BloomUpsampleConstants
		{
			float lowerTexelWidth;
			float lowerTexelHeight;
			float radius;
			float padding0;
		};

		static_assert(sizeof(BloomDownsampleConstants) == 32, "BloomDownsampleConstants must match the HLSL layout");
		static_assert(sizeof(BloomUpsampleConstants) == 16, "BloomUpsampleConstants must match the HLSL layout");
	}

	BloomPass::BloomPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_sceneWidth(width)
		, m_sceneHeight(height)
	{
		m_downsampleBuffer = m_device.CreateConstantBuffer(sizeof(BloomDownsampleConstants), nullptr);
		m_upsampleBuffer = m_device.CreateConstantBuffer(sizeof(BloomUpsampleConstants), nullptr);
		ASSERT(m_downsampleBuffer && m_upsampleBuffer);

		m_downsamplePs = m_device.CreateShader(ShaderType::PixelShader, MMO_BLOOM_DOWN_PS_BYTECODE, MMO_BLOOM_DOWN_PS_SIZE);
		m_upsamplePs = m_device.CreateShader(ShaderType::PixelShader, MMO_BLOOM_UP_PS_BYTECODE, MMO_BLOOM_UP_PS_SIZE);
		ASSERT(m_downsamplePs && m_upsamplePs);
	}

	void BloomPass::Resize(const uint32 width, const uint32 height)
	{
		m_sceneWidth = width;
		m_sceneHeight = height;
		ReleaseTargets();
	}

	void BloomPass::ReleaseTargets()
	{
		m_downTargets.clear();
		m_upTargets.clear();
		m_levelWidths.clear();
		m_levelHeights.clear();
		m_builtDivisor = 0;
		m_builtLevels = 0;
		m_builtWidth = 0;
		m_builtHeight = 0;
	}

	void BloomPass::EnsureTargets()
	{
		const uint32 divisor = m_settings.startDivisor > 0 ? m_settings.startDivisor : 1u;
		const uint32 levels = m_settings.levelCount;

		if (!m_downTargets.empty() && m_builtDivisor == divisor && m_builtLevels == levels
			&& m_builtWidth == m_sceneWidth && m_builtHeight == m_sceneHeight)
		{
			return;
		}

		ReleaseTargets();

		uint32 levelWidth = m_sceneWidth / divisor > 0 ? m_sceneWidth / divisor : 1u;
		uint32 levelHeight = m_sceneHeight / divisor > 0 ? m_sceneHeight / divisor : 1u;

		for (uint32 level = 0; level < levels; ++level)
		{
			m_levelWidths.push_back(levelWidth);
			m_levelHeights.push_back(levelHeight);

			m_downTargets.push_back(m_device.CreateRenderTexture("BloomDown" + std::to_string(level),
				static_cast<uint16>(levelWidth), static_cast<uint16>(levelHeight),
				RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16));
			ASSERT(m_downTargets.back());

			// The coarsest level has no upsample target: it is the start of the upsample chain.
			if (level + 1 < levels)
			{
				m_upTargets.push_back(m_device.CreateRenderTexture("BloomUp" + std::to_string(level),
					static_cast<uint16>(levelWidth), static_cast<uint16>(levelHeight),
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, PixelFormat::R16G16B16A16));
				ASSERT(m_upTargets.back());
			}

			levelWidth = levelWidth / 2 > 0 ? levelWidth / 2 : 1u;
			levelHeight = levelHeight / 2 > 0 ? levelHeight / 2 : 1u;
		}

		m_builtDivisor = divisor;
		m_builtLevels = levels;
		m_builtWidth = m_sceneWidth;
		m_builtHeight = m_sceneHeight;
	}

	TexturePtr BloomPass::GetResult() const
	{
		if (!m_settings.IsEnabled() || m_downTargets.empty())
		{
			return nullptr;
		}

		return m_upTargets.empty() ? TexturePtr(m_downTargets.front()) : TexturePtr(m_upTargets.front());
	}

	float BloomPass::GetResultScale() const
	{
		return m_settings.levelCount > 0 ? m_settings.intensity / static_cast<float>(m_settings.levelCount) : 0.0f;
	}

	void BloomPass::Render(RenderTexture& sceneColor, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		if (!m_settings.IsEnabled())
		{
			if (!m_downTargets.empty())
			{
				ReleaseTargets();
			}

			return;
		}

		EnsureTargets();

		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);
		fullscreenVs.Set();
		quad.Set(0);

		// --- Downsample chain -----------------------------------------------------------
		m_downsamplePs->Set();
		m_downsampleBuffer->BindToStage(ShaderType::PixelShader, 2);

		const uint32 levels = static_cast<uint32>(m_downTargets.size());
		for (uint32 level = 0; level < levels; ++level)
		{
			BloomDownsampleConstants constants{};
			if (level == 0)
			{
				// Taps are spaced so the 13-tap footprint covers the whole start divisor.
				const float tapScale = static_cast<float>(m_builtDivisor) * 0.5f;
				constants.texelWidth = tapScale / static_cast<float>(m_sceneWidth);
				constants.texelHeight = tapScale / static_cast<float>(m_sceneHeight);
				constants.applyPrefilter = 1;
			}
			else
			{
				constants.texelWidth = 1.0f / static_cast<float>(m_levelWidths[level - 1]);
				constants.texelHeight = 1.0f / static_cast<float>(m_levelHeights[level - 1]);
				constants.applyPrefilter = 0;
			}

			constants.threshold = m_settings.threshold;
			constants.knee = m_settings.knee;
			m_downsampleBuffer->Update(&constants);

			m_downTargets[level]->Activate();
			m_device.SetViewport(0, 0, static_cast<int32>(m_levelWidths[level]), static_cast<int32>(m_levelHeights[level]), 0.0f, 1.0f);

			if (level == 0)
			{
				sceneColor.Bind(ShaderType::PixelShader, 0);
			}
			else
			{
				m_device.BindTexture(m_downTargets[level - 1], ShaderType::PixelShader, 0);
			}

			m_device.Draw(6, 0);
			m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		}

		// --- Upsample chain: up[i] = down[i] + tent(coarser) ------------------------------
		m_upsamplePs->Set();
		m_upsampleBuffer->BindToStage(ShaderType::PixelShader, 2);

		for (int32 level = static_cast<int32>(levels) - 2; level >= 0; --level)
		{
			const uint32 coarser = static_cast<uint32>(level) + 1;
			const TexturePtr lower = (coarser == levels - 1) ? TexturePtr(m_downTargets[coarser]) : TexturePtr(m_upTargets[coarser]);

			BloomUpsampleConstants constants{};
			constants.lowerTexelWidth = 1.0f / static_cast<float>(m_levelWidths[coarser]);
			constants.lowerTexelHeight = 1.0f / static_cast<float>(m_levelHeights[coarser]);
			constants.radius = 1.0f;
			m_upsampleBuffer->Update(&constants);

			m_upTargets[level]->Activate();
			m_device.SetViewport(0, 0, static_cast<int32>(m_levelWidths[level]), static_cast<int32>(m_levelHeights[level]), 0.0f, 1.0f);
			m_device.BindTexture(lower, ShaderType::PixelShader, 0);
			m_device.BindTexture(m_downTargets[level], ShaderType::PixelShader, 1);
			m_device.Draw(6, 0);
			m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
			m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		}
	}
}
```

- [ ] **Step 3: Wire bloom into DeferredRenderer**

`deferred_renderer.h`: `#include "bloom_pass.h"`; member after `m_atmospherePass`:

```cpp
        /// @brief Bloom over the finished linear HDR scene, added by the TonemapPass.
        std::unique_ptr<BloomPass> m_bloomPass;
```

public setters after `SetAtmosphereDebugMode`:

```cpp
        /// @brief Applies the bloom quality preset: 0 Off, 1 Low, 2 High.
        void SetBloomQuality(int level) { m_bloomPass->GetSettings().ApplyQualityLevel(level); }

        /// @brief Sets the bloom intensity, clamped to [0, 1].
        void SetBloomIntensity(float intensity) { m_bloomPass->GetSettings().SetIntensity(intensity); }

        /// @brief Sets the bloom soft threshold (linear brightness), clamped to [0, 16].
        void SetBloomThreshold(float threshold) { m_bloomPass->GetSettings().SetThreshold(threshold); }
```

`deferred_renderer.cpp`: `#include "bloom_pass.h"`; constructor `m_bloomPass = std::make_unique<BloomPass>(m_device, width, height);`; `Resize`: `m_bloomPass->Resize(width, height);`. In `Render` replace

```cpp
        m_tonemapPass->Render(*m_renderTexture, nullptr, 0.0f, *m_quadBuffer, *m_deferredLightVs);
```

with

```cpp
        // Bloom sits out while submerged: the underwater pass has its own shafts and a bright
        // surface seen from below would otherwise bloom through the water fog.
        TexturePtr bloom;
        float bloomScale = 0.0f;
        if (!m_underwaterState.active)
        {
            m_bloomPass->Render(*m_renderTexture, *m_quadBuffer, *m_deferredLightVs);
            bloom = m_bloomPass->GetResult();
            bloomScale = m_bloomPass->GetResultScale();
        }

        m_tonemapPass->Render(*m_renderTexture, bloom, bloomScale, *m_quadBuffer, *m_deferredLightVs);
```

- [ ] **Step 4: Build and verify**

```powershell
cmake -S . -B build
cmake --build build --config Debug -t mmo_client mmo_edit
```

In the client check: the canopy opening facing the sun and bright sky patches glow softly; lit terrain at noon does not bloom (below threshold); particles with bright cores glow slightly; no flickering sparkles on water speculars while the camera turns slowly. Temporarily setting `BloomSettings::intensity` default to `0.0f` must give the same image as before this task — check once, then revert.

- [ ] **Step 5: Commit**

```powershell
git add .gitignore src/shared/deferred_shading/bloom_pass.h src/shared/deferred_shading/bloom_pass.cpp src/shared/deferred_shading/shaders/PS_BloomDownsample.hlsl src/shared/deferred_shading/shaders/PS_BloomUpsample.hlsl src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp
git commit -m "feat(render): bloom pass"
```

---

### Task 7: Cvars, options menu and editor sliders

**Files:**
- Modify: `src/mmo_client/game_states/world_state.h` (handler declarations near line 273)
- Modify: `src/mmo_client/game_states/world_state.cpp` (static cvars near line 145, `SetupWorldScene` ~1432, `RegisterGameplayCommands` ~2082 and ~2190, `RemoveGameplayCommands` ~2220, handlers next to `OnContactShadowDebugChanged` ~6123)
- Modify: `data/client/Interface/GameUI/OptionsFrame.lua` (after the SSAO radius entry, line 139)
- Modify: `data/client/Locales/Locale_{enUS,deDE,frFR,ruRU}/Localization.txt` (after `OPTIONS_SSAO_RADIUS`, line 76; after `OPTIONS_QUALITY_ULTRA`, line 103)
- Modify: `src/mmo_edit/editors/world_editor/world_settings_panel.cpp` (after the "Show Fog" checkbox, line 138)

**Interfaces:**
- Consumes: `Scene::Get/SetAtmosphereParameters` (Task 4), `DeferredRenderer::SetAtmosphereQuality/SetAtmosphereMarchDistance/SetAtmosphereDebugMode` (Task 5), `SetBloomQuality/SetBloomIntensity/SetBloomThreshold` (Task 6), `SetExposure` (Task 3), `WorldState::GetWorldDeferredRenderer()` (existing, line 1348).
- Produces: cvars `gxAtmosphereQuality`, `gxFogDensity`, `gxFogHeightFalloff`, `gxFogBaseHeight`, `gxFogAnisotropy`, `gxShaftStrength`, `gxAtmosphereMarchDistance`, `gxAtmosphereDebug`, `gxBloomQuality`, `gxBloomIntensity`, `gxBloomThreshold`, `gxExposure`; localization keys `OPTIONS_ATMOSPHERE_QUALITY`, `OPTIONS_BLOOM`, `OPTIONS_BRIGHTNESS`, `OPTIONS_QUALITY_OFF`.

- [ ] **Step 1: Declare the handlers**

In `world_state.h`, after `OnContactShadowDebugChanged`:

```cpp
		/// @brief Called when gxFogDensity, gxFogHeightFalloff, gxFogBaseHeight, gxFogAnisotropy or gxShaftStrength changed.
		void OnAtmosphereParametersChanged(ConsoleVar &var, const std::string &oldValue);

		/// @brief Called when gxAtmosphereQuality, gxAtmosphereMarchDistance or gxAtmosphereDebug changed.
		void OnAtmosphereRenderingChanged(ConsoleVar &var, const std::string &oldValue);

		/// @brief Called when gxBloomQuality, gxBloomIntensity or gxBloomThreshold changed.
		void OnBloomChanged(ConsoleVar &var, const std::string &oldValue);

		/// @brief Called when gxExposure changed.
		void OnExposureChanged(ConsoleVar &var, const std::string &oldValue);
```

- [ ] **Step 2: Register the cvars**

In `world_state.cpp`, after `static ConsoleVar *s_contactShadowDebugVar = nullptr;`:

```cpp
		static ConsoleVar *s_atmosphereQualityVar = nullptr;
		static ConsoleVar *s_fogDensityVar = nullptr;
		static ConsoleVar *s_fogHeightFalloffVar = nullptr;
		static ConsoleVar *s_fogBaseHeightVar = nullptr;
		static ConsoleVar *s_fogAnisotropyVar = nullptr;
		static ConsoleVar *s_shaftStrengthVar = nullptr;
		static ConsoleVar *s_atmosphereMarchDistanceVar = nullptr;
		static ConsoleVar *s_atmosphereDebugVar = nullptr;
		static ConsoleVar *s_bloomQualityVar = nullptr;
		static ConsoleVar *s_bloomIntensityVar = nullptr;
		static ConsoleVar *s_bloomThresholdVar = nullptr;
		static ConsoleVar *s_exposureVar = nullptr;
```

In `RegisterGameplayCommands`, after the `gxContactShadowDebug` registration (line 2082):

```cpp
		// Height fog, light shafts and bloom. Fog base values are multiplied by the time-of-day curves
		// Models/FogColor.hccv and Models/SunScatter.hccv. All distances are metres.
		s_atmosphereQualityVar = ConsoleVarMgr::RegisterConsoleVar("gxAtmosphereQuality", "Light shaft quality: 0 = Off (height fog only), 1 = Low (quarter resolution, 12 steps), 2 = Medium (half, 24), 3 = High (half, 48), 4 = Ultra (full, 64).", "3");
		m_cvarChangedSignals += s_atmosphereQualityVar->Changed.connect(this, &WorldState::OnAtmosphereRenderingChanged);

		s_atmosphereMarchDistanceVar = ConsoleVarMgr::RegisterConsoleVar("gxAtmosphereMarchDistance", "How many metres of each view ray are marched for light shafts (at most 300, the shadow range). Fog beyond uses a closed-form estimate.", "200");
		m_cvarChangedSignals += s_atmosphereMarchDistanceVar->Changed.connect(this, &WorldState::OnAtmosphereRenderingChanged);

		s_atmosphereDebugVar = ConsoleVarMgr::RegisterConsoleVar("gxAtmosphereDebug", "Atmosphere debug view: 0 = off, 1 = scattered light only, 2 = transmittance, 3 = light shaft shadow term.", "0");
		m_cvarChangedSignals += s_atmosphereDebugVar->Changed.connect(this, &WorldState::OnAtmosphereRenderingChanged);

		s_fogDensityVar = ConsoleVarMgr::RegisterConsoleVar("gxFogDensity", "Height fog extinction per metre at gxFogBaseHeight. 0 disables the fog.", "0.02");
		m_cvarChangedSignals += s_fogDensityVar->Changed.connect(this, &WorldState::OnAtmosphereParametersChanged);

		s_fogHeightFalloffVar = ConsoleVarMgr::RegisterConsoleVar("gxFogHeightFalloff", "How quickly the fog thins with height, per metre. Higher values keep fog in valleys.", "0.05");
		m_cvarChangedSignals += s_fogHeightFalloffVar->Changed.connect(this, &WorldState::OnAtmosphereParametersChanged);

		s_fogBaseHeightVar = ConsoleVarMgr::RegisterConsoleVar("gxFogBaseHeight", "World height (Y) at which the fog has density gxFogDensity.", "0");
		m_cvarChangedSignals += s_fogBaseHeightVar->Changed.connect(this, &WorldState::OnAtmosphereParametersChanged);

		s_fogAnisotropyVar = ConsoleVarMgr::RegisterConsoleVar("gxFogAnisotropy", "Forward scattering of sunlight in the fog (0 to 0.95). Higher values make a tighter, brighter glow around the sun.", "0.7");
		m_cvarChangedSignals += s_fogAnisotropyVar->Changed.connect(this, &WorldState::OnAtmosphereParametersChanged);

		s_shaftStrengthVar = ConsoleVarMgr::RegisterConsoleVar("gxShaftStrength", "Multiplier on sunlight scattered by the fog (light shafts and sun glow).", "1.0");
		m_cvarChangedSignals += s_shaftStrengthVar->Changed.connect(this, &WorldState::OnAtmosphereParametersChanged);

		s_bloomQualityVar = ConsoleVarMgr::RegisterConsoleVar("gxBloomQuality", "Bloom quality: 0 = Off, 1 = Low (quarter resolution, 4 levels), 2 = High (half resolution, 6 levels).", "2");
		m_cvarChangedSignals += s_bloomQualityVar->Changed.connect(this, &WorldState::OnBloomChanged);

		s_bloomIntensityVar = ConsoleVarMgr::RegisterConsoleVar("gxBloomIntensity", "Bloom strength, from 0 to 1.", "0.08");
		m_cvarChangedSignals += s_bloomIntensityVar->Changed.connect(this, &WorldState::OnBloomChanged);

		s_bloomThresholdVar = ConsoleVarMgr::RegisterConsoleVar("gxBloomThreshold", "Linear brightness above which light blooms (soft knee).", "0.8");
		m_cvarChangedSignals += s_bloomThresholdVar->Changed.connect(this, &WorldState::OnBloomChanged);

		s_exposureVar = ConsoleVarMgr::RegisterConsoleVar("gxExposure", "Scene brightness multiplier applied before tone mapping (0.1 to 8).", "1.0");
		m_cvarChangedSignals += s_exposureVar->Changed.connect(this, &WorldState::OnExposureChanged);
```

At the end of `RegisterGameplayCommands`, after `OnContactShadowDebugChanged(*s_contactShadowDebugVar, "");`:

```cpp
		OnAtmosphereParametersChanged(*s_fogDensityVar, "");
		OnAtmosphereRenderingChanged(*s_atmosphereQualityVar, "");
		OnBloomChanged(*s_bloomQualityVar, "");
		OnExposureChanged(*s_exposureVar, "");
```

In `RemoveGameplayCommands`, after `ConsoleVarMgr::UnregisterConsoleVar("gxContactShadowDebug");`:

```cpp
		ConsoleVarMgr::UnregisterConsoleVar("gxAtmosphereQuality");
		ConsoleVarMgr::UnregisterConsoleVar("gxAtmosphereMarchDistance");
		ConsoleVarMgr::UnregisterConsoleVar("gxAtmosphereDebug");
		ConsoleVarMgr::UnregisterConsoleVar("gxFogDensity");
		ConsoleVarMgr::UnregisterConsoleVar("gxFogHeightFalloff");
		ConsoleVarMgr::UnregisterConsoleVar("gxFogBaseHeight");
		ConsoleVarMgr::UnregisterConsoleVar("gxFogAnisotropy");
		ConsoleVarMgr::UnregisterConsoleVar("gxShaftStrength");
		ConsoleVarMgr::UnregisterConsoleVar("gxBloomQuality");
		ConsoleVarMgr::UnregisterConsoleVar("gxBloomIntensity");
		ConsoleVarMgr::UnregisterConsoleVar("gxBloomThreshold");
		ConsoleVarMgr::UnregisterConsoleVar("gxExposure");
```

- [ ] **Step 3: Implement the handlers**

In `world_state.cpp`, after `OnContactShadowDebugChanged`:

```cpp
	void WorldState::OnAtmosphereParametersChanged(ConsoleVar &var, const std::string &oldValue)
	{
		// The scene is rebuilt on every world change, and SetupWorldScene re-applies these.
		if (!m_scene || !s_fogDensityVar)
		{
			return;
		}

		AtmosphereParameters parameters = m_scene->GetAtmosphereParameters();
		parameters.SetDensity(s_fogDensityVar->GetFloatValue());
		parameters.SetHeightFalloff(s_fogHeightFalloffVar->GetFloatValue());
		parameters.SetBaseHeight(s_fogBaseHeightVar->GetFloatValue());
		parameters.SetAnisotropy(s_fogAnisotropyVar->GetFloatValue());
		parameters.SetShaftStrength(s_shaftStrengthVar->GetFloatValue());
		m_scene->SetAtmosphereParameters(parameters);
	}

	void WorldState::OnAtmosphereRenderingChanged(ConsoleVar &var, const std::string &oldValue)
	{
		DeferredRenderer* renderer = GetWorldDeferredRenderer();
		if (!renderer || !s_atmosphereQualityVar)
		{
			return;
		}

		renderer->SetAtmosphereQuality(s_atmosphereQualityVar->GetIntValue());
		renderer->SetAtmosphereMarchDistance(s_atmosphereMarchDistanceVar->GetFloatValue());
		renderer->SetAtmosphereDebugMode(s_atmosphereDebugVar->GetIntValue());
	}

	void WorldState::OnBloomChanged(ConsoleVar &var, const std::string &oldValue)
	{
		DeferredRenderer* renderer = GetWorldDeferredRenderer();
		if (!renderer || !s_bloomQualityVar)
		{
			return;
		}

		renderer->SetBloomQuality(s_bloomQualityVar->GetIntValue());
		renderer->SetBloomIntensity(s_bloomIntensityVar->GetFloatValue());
		renderer->SetBloomThreshold(s_bloomThresholdVar->GetFloatValue());
	}

	void WorldState::OnExposureChanged(ConsoleVar &var, const std::string &oldValue)
	{
		if (DeferredRenderer* renderer = GetWorldDeferredRenderer())
		{
			renderer->SetExposure(var.GetFloatValue());
		}
	}
```

In `SetupWorldScene`, directly after `m_scene = std::make_unique<OctreeScene>();`:

```cpp
		// A fresh scene starts from AtmosphereParameters defaults; re-apply the player's cvars.
		if (s_fogDensityVar)
		{
			OnAtmosphereParametersChanged(*s_fogDensityVar, "");
		}
```

If `world_state.cpp` does not already include `scene_graph/atmosphere_settings.h` transitively through `scene.h`, the build will say so; `scene.h` includes it since Task 4.

- [ ] **Step 4: Options menu entries**

In `OptionsFrame.lua`, after the `OPTIONS_SSAO_RADIUS` slider entry (ends line 139) insert:

```lua
			{
				type = "dropdown",
				labelKey = "OPTIONS_ATMOSPHERE_QUALITY",
				cvar = "gxAtmosphereQuality",
				defaultValue = "3",
				items = {
					{ labelKey = "OPTIONS_QUALITY_OFF",    value = "0" },
					{ labelKey = "OPTIONS_QUALITY_LOW",    value = "1" },
					{ labelKey = "OPTIONS_QUALITY_MEDIUM", value = "2" },
					{ labelKey = "OPTIONS_QUALITY_HIGH",   value = "3" },
					{ labelKey = "OPTIONS_QUALITY_ULTRA",  value = "4" },
				},
			},
			{
				type = "dropdown",
				labelKey = "OPTIONS_BLOOM",
				cvar = "gxBloomQuality",
				defaultValue = "2",
				items = {
					{ labelKey = "OPTIONS_QUALITY_OFF",  value = "0" },
					{ labelKey = "OPTIONS_QUALITY_LOW",  value = "1" },
					{ labelKey = "OPTIONS_QUALITY_HIGH", value = "2" },
				},
			},
			{
				type = "slider",
				labelKey = "OPTIONS_BRIGHTNESS",
				cvar = "gxExposure",
				defaultValue = "1.0",
				min = 0.5,
				max = 2.0,
				step = 0.05,
				format = "%.2f",
			},
```

- [ ] **Step 5: Localized strings in all four locales**

After the `OPTIONS_SSAO_RADIUS` line (76) of each file add, and after the `OPTIONS_QUALITY_ULTRA` line (103) add the `OFF` line:

`Locale_enUS/Localization.txt`:

```
	(key = "OPTIONS_ATMOSPHERE_QUALITY", string = "Fog and Light Shafts")
	(key = "OPTIONS_BLOOM", string = "Bloom")
	(key = "OPTIONS_BRIGHTNESS", string = "Brightness")
```
```
	(key = "OPTIONS_QUALITY_OFF", string = "Off")
```

`Locale_deDE/Localization.txt`:

```
	(key = "OPTIONS_ATMOSPHERE_QUALITY", string = "Nebel und Lichtstrahlen")
	(key = "OPTIONS_BLOOM", string = "Bloom")
	(key = "OPTIONS_BRIGHTNESS", string = "Helligkeit")
```
```
	(key = "OPTIONS_QUALITY_OFF", string = "Aus")
```

`Locale_frFR/Localization.txt`:

```
	(key = "OPTIONS_ATMOSPHERE_QUALITY", string = "Brouillard et rayons de lumière")
	(key = "OPTIONS_BLOOM", string = "Flou lumineux")
	(key = "OPTIONS_BRIGHTNESS", string = "Luminosité")
```
```
	(key = "OPTIONS_QUALITY_OFF", string = "Désactivé")
```

`Locale_ruRU/Localization.txt`:

```
	(key = "OPTIONS_ATMOSPHERE_QUALITY", string = "Туман и лучи света")
	(key = "OPTIONS_BLOOM", string = "Свечение")
	(key = "OPTIONS_BRIGHTNESS", string = "Яркость")
```
```
	(key = "OPTIONS_QUALITY_OFF", string = "Выкл.")
```

Save the files as UTF-8 (matching their current encoding; check with `git diff` that no unrelated lines changed).

- [ ] **Step 6: Editor sliders**

In `world_settings_panel.cpp`, directly after the "Show Fog" checkbox block (after line 138) insert:

```cpp
                if (showFog)
                {
                    ImGui::Indent();

                    // Base values; the sky's time-of-day curves (FogColor.hccv, SunScatter.hccv)
                    // multiply density and shaft strength on top, exactly as in the client.
                    AtmosphereParameters atmosphere = m_terrain.GetScene().GetAtmosphereParameters();
                    bool atmosphereChanged = false;

                    float density = atmosphere.density;
                    if (ImGui::DragFloat("Fog Density", &density, 0.0005f, 0.0f, 1.0f, "%.4f"))
                    {
                        atmosphere.SetDensity(density);
                        atmosphereChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Extinction per metre at the base height (client cvar gxFogDensity).");
                    }

                    float falloff = atmosphere.heightFalloff;
                    if (ImGui::DragFloat("Fog Height Falloff", &falloff, 0.001f, 0.0f, 1.0f, "%.3f"))
                    {
                        atmosphere.SetHeightFalloff(falloff);
                        atmosphereChanged = true;
                    }

                    float baseHeight = atmosphere.baseHeight;
                    if (ImGui::DragFloat("Fog Base Height", &baseHeight, 0.5f, -10000.0f, 10000.0f, "%.1f"))
                    {
                        atmosphere.SetBaseHeight(baseHeight);
                        atmosphereChanged = true;
                    }

                    float anisotropy = atmosphere.anisotropy;
                    if (ImGui::SliderFloat("Sun Glow Tightness", &anisotropy, 0.0f, 0.95f, "%.2f"))
                    {
                        atmosphere.SetAnisotropy(anisotropy);
                        atmosphereChanged = true;
                    }

                    float shaftStrength = atmosphere.shaftStrength;
                    if (ImGui::DragFloat("Light Shaft Strength", &shaftStrength, 0.01f, 0.0f, 16.0f, "%.2f"))
                    {
                        atmosphere.SetShaftStrength(shaftStrength);
                        atmosphereChanged = true;
                    }

                    if (atmosphereChanged)
                    {
                        m_terrain.GetScene().SetAtmosphereParameters(atmosphere);
                    }

                    ImGui::Unindent();
                }
```

- [ ] **Step 7: Build and verify**

```powershell
cmake --build build --config Debug -t mmo_client mmo_edit
```

In the client: open the console and set `gxAtmosphereDebug 3` (shaft shadow term visible), `gxAtmosphereDebug 0`, `gxAtmosphereQuality 0` (shafts vanish, height fog remains), `gxBloomQuality 0`, `gxExposure 1.5`, `gxFogDensity 0` (fog gone). Open Options → Graphics: the three new entries show localized labels, change the image live, and persist across a client restart. Switch the locale to deDE once and confirm no raw `OPTIONS_` keys appear. In the editor world viewport, the sliders change the fog live.

- [ ] **Step 8: Commit**

```powershell
git add src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_settings_panel.cpp
git commit -m "feat(render): atmosphere and bloom cvars and editor sliders"
```

The Lua and locale files live in the `data/client` submodule: show the user the diff and ask before committing there (message `ui: atmosphere, bloom and brightness options`) and bumping the pointer.

---

### Task 8: GPU timing, tuning, documentation and gate

**Files:**
- Modify: `src/shared/deferred_shading/deferred_renderer.h:436-440`, `deferred_renderer.cpp` (`GpuTimerEndAndCollect` lines 303-311, `GpuTimerMark` calls in `Render`)
- Create: `docs/rendering-atmosphere.md`
- Modify: `docs/console_commands.md:65-70`
- Modify (only if tuning changes defaults): `atmosphere_settings.h`, `atmosphere_pass_settings.h`, `bloom_settings.h`, `sky_component.cpp` fallback keys, `world_state.cpp` cvar defaults, `OptionsFrame.lua` defaults, and the matching unit tests and spec defaults

**Interfaces:**
- Consumes: everything above.
- Produces: perf overlay rows `GPU: Atmosphere`, `GPU: Bloom`, `GPU: Tonemap`.

- [ ] **Step 1: Timestamp points for the new passes**

In `deferred_renderer.h` change the comment and count:

```cpp
        // Timestamp points: 0 = start, 1 = after shadows, 2 = after G-Buffer, 3 = after SSAO,
        // 4 = after contact shadows, 5 = after lighting, 6 = after atmosphere, 7 = after forward,
        // 8 = after bloom, 9 = after tonemap (end). Differences give per-pass GPU time.
        static constexpr uint32 GpuTimerPointCount = 10;
```

In `Render` add marks, keeping the existing pattern `#ifdef _WIN32 if (m_gpuTimingActiveThisFrame) { GpuTimerMark(n); } #endif`:
- after the atmosphere block and its `CopyResource`: `GpuTimerMark(6); // after atmosphere`
- after the forward pass (after unbinding t14/t15, before the bloom block): `GpuTimerMark(7); // after forward`
- after the bloom block, before `m_tonemapPass->Render`: `GpuTimerMark(8); // after bloom`
- remove the old final block (`GpuTimerMark(6); // after forward/translucent pass` + `GpuTimerEndAndCollect();`) and replace it with the two blocks shown below: mark 9 directly after `m_tonemapPass->Render(...)`, and `GpuTimerEndAndCollect()` at the very end of `Render`. The underwater pass in between stays unmeasured, as today.

In `GpuTimerEndAndCollect` replace the emit list with:

```cpp
                emit("GPU: Shadows", 0, 1);
                emit("GPU: GBuffer", 1, 2);
                emit("GPU: SSAO", 2, 3);
                emit("GPU: ContactShadows", 3, 4);
                emit("GPU: Lighting", 4, 5);
                emit("GPU: Atmosphere", 5, 6);
                emit("GPU: Forward", 6, 7);
                emit("GPU: Bloom", 7, 8);
                emit("GPU: Tonemap", 8, 9);
                emit("GPU: Total (passes)", 0, 9);
```

Because `GpuTimerMark(9)` must run before `GpuTimerEndAndCollect()`, the closing block of `Render` becomes:

```cpp
#ifdef _WIN32
        if (m_gpuTimingActiveThisFrame)
        {
            GpuTimerMark(9); // after tonemap
        }
#endif

        // (underwater post-process block, unchanged)

#ifdef _WIN32
        if (m_gpuTimingActiveThisFrame)
        {
            GpuTimerEndAndCollect();
        }
#endif
```

- [ ] **Step 2: Build a Release client for measurements**

Debug timings are not representative, and even in Release the overlay's per-frame values fluctuate: compare the Avg column, and treat differences under ~0.5 ms as noise.

```powershell
cmake --build build --config Release -t mmo_client
```

- [ ] **Step 3: Tune the look against the reference**

Run `bin/Release/mmo_client.exe` at 1920x1080, `gxRenderScale 1`, `perf 1`. In a forest area, at dawn, noon, dusk and night, adjust in the console until the look matches the reference screenshot's character (dense warm haze in the distance, readable shafts through canopy gaps, soft glow around the sky opening, nothing blown out at noon):
`gxFogDensity`, `gxFogHeightFalloff`, `gxFogBaseHeight`, `gxFogAnisotropy`, `gxShaftStrength`, `gxBloomIntensity`, `gxBloomThreshold`.

Record the final values. If they differ from the defaults, change them in all places listed under **Files** above (cvar default string, struct default, OptionsFrame `defaultValue` where applicable, unit test expectations, and the spec's cvar table) and re-run `.\bin\Debug\deferred_shading_tests.exe` after a Debug build. If the curve fallbacks need changing, edit the keys in `SkyComponent::LoadColorCurves`; optionally also author `Models/FogColor.hccv` and `Models/SunScatter.hccv` in the editor's colour curve editor (they then override the fallbacks — note that this is data in a submodule and needs the user's go-ahead to commit).

Capture one screenshot per time of day with `tools/render_compare/capture_client.ps1` into `build/render_compare/final_*.png` and send them to the user.

- [ ] **Step 4: Measure performance per quality level**

For each combination below, stand still in the same forest view, wait for the perf overlay averages to settle, and note `GPU: Atmosphere`, `GPU: Bloom`, `GPU: Tonemap` (Avg column):

| gxAtmosphereQuality | gxBloomQuality | Target (sum of the three rows) |
|---|---|---|
| 4 | 2 | informational |
| 3 | 2 | ≤ ~3 ms on an RTX 3070-class GPU at 1080p |
| 2 | 1 | informational |
| 1 | 1 | ≤ ~1 ms |
| 0 | 0 | ~0.2 ms (closed-form fog + tonemap only) |

Report the table with the GPU model to the user. If High/High exceeds the target, first lower the High preset's steps (48 → 32) in `AtmospherePassSettings::ApplyQualityLevel` and its test, then re-measure; do not change the Low preset below 12 steps without asking.

- [ ] **Step 5: Write the documentation**

`docs/rendering-atmosphere.md`:

```markdown
# Atmosphere: Height Fog, Light Shafts, Bloom

Design: [superpowers/specs/2026-09-13-volumetric-atmosphere-design.md](superpowers/specs/2026-09-13-volumetric-atmosphere-design.md)

## Frame order (DeferredRenderer::Render)

1. Cascaded shadow maps → G-Buffer → SSAO → contact shadows
2. Lighting (`PS_DeferredLighting`) — **linear HDR**, no fog, no tonemap
3. `AtmospherePass` — march (reduced resolution) → bilateral blur → composite into `m_sceneColorCopy`,
   copied back into the scene target. Skipped while submerged or with `Scene::IsFogEnabled()` false.
4. Forward pass — `Scene::SetForwardOutputLinear(true)`; materials apply the same height fog analytically
5. `BloomPass` — 13-tap downsample chain, tent upsample chain. Skipped while submerged.
6. `TonemapPass` — scene + bloom, exposure, ACES, gamma, dither
7. `PostProcessPass` (underwater only) — display-referred input

## The linear-HDR contract

Nothing before the TonemapPass may tone map or gamma-encode. Forward materials rendered outside
DeferredRenderer (editor previews, the client's model frames, the minimap baker) keep tone mapping
in the material because they never set `forwardOutputLinear`. Unlit forward materials author
display-referred colour and inverse-tonemap it inside the deferred forward pass.

## Fog model

Density `σ(y) = gxFogDensity · densityMultiplier · exp(min(−gxFogHeightFalloff · (y − gxFogBaseHeight), 12))`.
Per metre the fog scatters `σ · (FogTint + SunScatterColor · SunColor · SunIntensity · shaftStrength ·
Phase(cosθ) · shadow)` toward the camera, with a Henyey-Greenstein lobe (g = gxFogAnisotropy) blended
80/20 with isotropic. The first `gxAtmosphereMarchDistance` metres are marched through the shadow maps;
the rest of the ray uses the closed-form integral with the sun unshadowed.

The formulas exist three times and must change together:
`shaders/AtmosphereCommon.hlsli`, the forward fog emitted by `MaterialCompilerD3D11`, and
`deferred_shading/atmosphere_math.h` (unit-tested).

The camera cbuffer (b1, 176 bytes) is declared three times as well: `PsCameraConstantBuffer` in
`scene.cpp`, `CameraBuffer` in `AtmosphereCommon.hlsli`, `CameraParameters` in the material compiler.
Any change there requires **Tools → Rebuild All Materials** in the editor.

## Time of day

`SkyComponent` evaluates two colour curves (editable in the colour curve editor) and writes
`Scene::SetAtmosphereTimeOfDay`:

| File | rgb | alpha |
|---|---|---|
| `Models/FogColor.hccv` | fog ambient radiance | density multiplier |
| `Models/SunScatter.hccv` | sun colour inside fog | shaft strength multiplier |

Missing or unreadable files fall back to built-in keys. The cvars are base values; the curves multiply them.

## Console variables

| Cvar | Default | Meaning |
|---|---|---|
| `gxAtmosphereQuality` | 3 | 0 Off (height fog only), 1 Low, 2 Medium, 3 High, 4 Ultra |
| `gxAtmosphereMarchDistance` | 200 | metres marched for shafts (≤ 300) |
| `gxAtmosphereDebug` | 0 | 1 scattered light, 2 transmittance, 3 shaft shadow term |
| `gxFogDensity` | 0.02 | extinction per metre at the base height |
| `gxFogHeightFalloff` | 0.05 | falloff per metre of height |
| `gxFogBaseHeight` | 0 | world Y of `gxFogDensity` |
| `gxFogAnisotropy` | 0.7 | sun glow tightness |
| `gxShaftStrength` | 1.0 | sun scattering multiplier |
| `gxBloomQuality` | 2 | 0 Off, 1 Low, 2 High |
| `gxBloomIntensity` | 0.08 | bloom weight |
| `gxBloomThreshold` | 0.8 | soft-knee threshold (linear) |
| `gxExposure` | 1.0 | brightness before tone mapping |

## Known limitations

- `gxFogBaseHeight` is absolute; maps far from Y = 0 need per-area values (planned per-zone time of day).
- Shafts need shadow-casting geometry and end at the 300 m shadow range.
- Shafts are not drawn over forward surfaces (water, particles); those get closed-form fog only.
- Point and spot lights do not scatter in the fog.
```

In `docs/console_commands.md`, after the `perf` line (70) add:

```markdown
- `gxRenderScale` - 3D render resolution scale, 0.25 to 1.0 (default: 1.0)
- `gxAtmosphereQuality` - Fog and light shaft quality, 0 = Off … 4 = Ultra (default: 3)
- `gxAtmosphereMarchDistance` - Metres marched for light shafts, at most 300 (default: 200)
- `gxAtmosphereDebug` - Atmosphere debug view, 0-3 (default: 0)
- `gxFogDensity` / `gxFogHeightFalloff` / `gxFogBaseHeight` - Height fog base values (defaults: 0.02 / 0.05 / 0)
- `gxFogAnisotropy` / `gxShaftStrength` - Sun glow tightness and sun scattering strength (defaults: 0.7 / 1.0)
- `gxBloomQuality` / `gxBloomIntensity` / `gxBloomThreshold` - Bloom (defaults: 2 / 0.08 / 0.8)
- `gxExposure` - Brightness before tone mapping (default: 1.0)
- `gxSsao*`, `gxContactShadow*`, `ShadowQuality`, `ShadowTextureSize` - see `WorldState::RegisterGameplayCommands`

See [rendering-atmosphere.md](rendering-atmosphere.md) for the fog model.
```

(Adjust the defaults in both documents to the tuned values from Step 3.)

- [ ] **Step 6: Commit**

```powershell
git add src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp docs/rendering-atmosphere.md docs/console_commands.md
git commit -m "docs(render): atmosphere pipeline documentation and GPU pass timings"
```

(Include any files changed by tuning in Step 3, and the spec if its defaults changed.)

- [ ] **Step 7: Run the gate**

Confirm with the user that the data submodule commits from Tasks 4 and 7 are in place (the gate's E2E run uses the working tree's data), then run `/gate`. On green, report the results and offer `/ship`; do not merge without the user's go-ahead.

