# Colour Grading (LUT and Sliders) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Environment profiles grade the final image with a strip LUT plus saturation, contrast and colour filter, applied after ACES in the tonemap pass and cross-faded at zone borders.

**Architecture:** A dependency-free `color_grading.h` holds the slider settings and the strip-LUT addressing (unit-tested, mirrored in `PS_Tonemap.hlsl`). Profiles carry six new fields; `EnvironmentController::Evaluate` picks the LUT pair (target LUT, strongest other profile's LUT, blend). `DeferredRenderer::SetColorGrading` feeds the tonemap pass, which loads LUT textures once, binds them at t2/t3 with a dedicated sampler at s4, and grades after gamma.

**Tech Stack:** C++17, D3D11 / HLSL, protobuf (proto2), ImGui (mmo_edit), Catch2, Python 3 (numpy, PIL) for the neutral LUT.

**Spec:** `docs/superpowers/specs/2026-09-21-color-grading-design.md`

## Global Constraints

- Branch `feature/fog-light-scattering`. Never push. Never merge.
- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `camelCase` locals and anonymous-namespace free functions, `#pragma once`, Doxygen on public members, `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new source file (HLSL and Python included as a `#` comment).
- No exceptions; use `ASSERT` / `VERIFY` / `ELOG` / `WLOG`.
- Profile fields (both protos, identical numbers): `color_lut = 26` (string), `saturation = 27 [default = 1]`, `contrast = 28 [default = 1]`, `color_filter_r/g/b = 29/30/31 [default = 1]`. Saturation, contrast and each filter channel clamp to [0, 2].
- Grading order after `pow(ACES(hdr * Exposure), 1/2.2)`: colour filter multiply, saturation `lerp(luma, c, s)` with Rec. 709 luma `(0.2126, 0.7152, 0.0722)`, contrast `saturate((c - 0.5) * k + 0.5)`, then `lerp(LutFrom(c), Lut(c), LutBlend)`, then dither.
- Strip LUT of edge N: texture `N*N` x `N`; blue selects the slice `b * (N - 1)`, red the column, green the row; `u = (slice * N + r * (N - 1) + 0.5) / (N * N)`, `v = (g * (N - 1) + 0.5) / N`; two reads at mip 0 blended by the fractional slice. Size 0 = no LUT (colour passes through).
- A LUT whose width is not the square of its height, or that fails to load, counts as no LUT and logs one warning per path.
- Tonemap cbuffer b2 is exactly 48 bytes; C++ `TonemapConstants` and HLSL `TonemapBuffer` change together. LUT textures at `t2` (target) / `t3` (from); LUT sampler at `s4` (linear, clamp).
- LUT pair rule: `colorLut` = target entry's LUT, `colorLutFrom` = highest-weight non-target entry's LUT (empty if none), `colorLutBlend` = target weight (1 when alone).
- Build: `cmake --build build --config Debug -t <target>`; tests are executables in `bin/Debug/`. On LNK1168 (exe locked by a running client/editor/server) report it; never kill processes you did not start.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Only Task 4 writes to `data/client` (commit inside the submodule, never bump the pointer). No other task touches `data/client` or `data/editor`.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/shared/deferred_shading/color_grading.h` (new) | `ColorGradingSettings` with clamps; `color_grading::IsStripLut`, `StripLutCoordinates`. |
| `src/shared/proto_data/environment_profiles.proto`, `src/shared/client_data/environment_profiles.proto` | Fields 26-31. |
| `src/shared/scene_graph/environment_profile.h`, `environment_profile_proto.h`, `environment_state.h/.cpp`, `environment_controller.cpp` | Runtime values, loader clamps, blending, LUT pair. |
| `src/mmo_edit/editor_windows/environment_profile_editor_window.cpp` | "Color Grading" section. |
| `src/shared/deferred_shading/tonemap_settings.h`, `tonemap_pass.h/.cpp`, `shaders/PS_Tonemap.hlsl`, `deferred_renderer.h` | Grading in the tonemap pass. |
| `src/mmo_client/game_states/world_state.cpp`, `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` | Feed the grade each frame. |
| `tools/color_grading/make_luts.py` (new), `data/client/Textures/ColorGrading/NeutralLUT32.png/.htex` (new) | Neutral LUT and a test grade. |
| `docs/color-grading.md` (new), `docs/rendering-atmosphere.md` | Workflow and pipeline docs. |

---

### Task 1: Colour grading math header

**Files:**
- Create: `src/shared/deferred_shading/color_grading.h`
- Test: `src/tests/deferred_shading_tests/test_color_grading.cpp` (the suite globs its directory; it links `base` and `math`)

**Interfaces:**
- Produces:
  - `struct ColorGradingSettings { float saturation = 1; float contrast = 1; Vector3 colorFilter{1, 1, 1}; void SetSaturation(float); void SetContrast(float); void SetColorFilter(const Vector3&); }` — setters clamp to [0, 2] (each filter channel separately).
  - `namespace color_grading { constexpr float MaxGradingValue = 2.0f; bool IsStripLut(uint32 width, uint32 height); struct StripCoordinates { float u0; float u1; float v; float sliceBlend; }; StripCoordinates StripLutCoordinates(float r, float g, float b, float size); }`

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/color_grading.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace mmo;

namespace
{
	/// A neutral strip LUT of edge size, 8-bit quantized like an imported PNG.
	struct StripImage
	{
		uint32 size = 0;
		std::vector<Vector3> texels;

		explicit StripImage(const uint32 edge)
			: size(edge)
			, texels(static_cast<size_t>(edge) * edge * edge)
		{
			const float scale = static_cast<float>(edge - 1);
			for (uint32 y = 0; y < edge; ++y)
			{
				for (uint32 x = 0; x < edge * edge; ++x)
				{
					const uint32 slice = x / edge;
					const uint32 column = x % edge;
					const auto quantize = [](const float value) { return std::round(value * 255.0f) / 255.0f; };
					texels[static_cast<size_t>(y) * edge * edge + x] = Vector3(
						quantize(static_cast<float>(column) / scale),
						quantize(static_cast<float>(y) / scale),
						quantize(static_cast<float>(slice) / scale));
				}
			}
		}

		[[nodiscard]] Vector3 Texel(int x, int y) const
		{
			const int width = static_cast<int>(size * size);
			x = std::clamp(x, 0, width - 1);
			y = std::clamp(y, 0, static_cast<int>(size) - 1);
			return texels[static_cast<size_t>(y) * width + x];
		}

		/// Bilinear sample at mip 0 with clamp addressing, like the LUT sampler.
		[[nodiscard]] Vector3 Sample(const float u, const float v) const
		{
			const float x = u * static_cast<float>(size * size) - 0.5f;
			const float y = v * static_cast<float>(size) - 0.5f;
			const int x0 = static_cast<int>(std::floor(x));
			const int y0 = static_cast<int>(std::floor(y));
			const float fx = x - static_cast<float>(x0);
			const float fy = y - static_cast<float>(y0);
			const Vector3 top = Texel(x0, y0) * (1.0f - fx) + Texel(x0 + 1, y0) * fx;
			const Vector3 bottom = Texel(x0, y0 + 1) * (1.0f - fx) + Texel(x0 + 1, y0 + 1) * fx;
			return top * (1.0f - fy) + bottom * fy;
		}

		/// The shader's two-read slice blend.
		[[nodiscard]] Vector3 Lookup(const float r, const float g, const float b) const
		{
			const color_grading::StripCoordinates c = color_grading::StripLutCoordinates(r, g, b, static_cast<float>(size));
			return Sample(c.u0, c.v) * (1.0f - c.sliceBlend) + Sample(c.u1, c.v) * c.sliceBlend;
		}
	};
}

TEST_CASE("Colour grading settings clamp to their range", "[color_grading]")
{
	ColorGradingSettings settings;
	CHECK(settings.saturation == Approx(1.0f));
	CHECK(settings.contrast == Approx(1.0f));
	CHECK(settings.colorFilter.x == Approx(1.0f));

	settings.SetSaturation(5.0f);
	CHECK(settings.saturation == Approx(2.0f));
	settings.SetSaturation(-1.0f);
	CHECK(settings.saturation == Approx(0.0f));

	settings.SetContrast(3.0f);
	CHECK(settings.contrast == Approx(2.0f));

	settings.SetColorFilter(Vector3(-1.0f, 0.5f, 9.0f));
	CHECK(settings.colorFilter.x == Approx(0.0f));
	CHECK(settings.colorFilter.y == Approx(0.5f));
	CHECK(settings.colorFilter.z == Approx(2.0f));
}

TEST_CASE("Strip LUTs are N*N wide and N high", "[color_grading]")
{
	CHECK(color_grading::IsStripLut(256, 16));
	CHECK(color_grading::IsStripLut(1024, 32));
	CHECK_FALSE(color_grading::IsStripLut(512, 32));
	CHECK_FALSE(color_grading::IsStripLut(1, 1));
	CHECK_FALSE(color_grading::IsStripLut(0, 0));
}

TEST_CASE("Strip coordinates address texel centres inside their slice", "[color_grading]")
{
	const color_grading::StripCoordinates black = color_grading::StripLutCoordinates(0.0f, 0.0f, 0.0f, 16.0f);
	CHECK(black.u0 == Approx(0.5f / 256.0f));
	CHECK(black.v == Approx(0.5f / 16.0f));
	CHECK(black.sliceBlend == Approx(0.0f));

	const color_grading::StripCoordinates white = color_grading::StripLutCoordinates(1.0f, 1.0f, 1.0f, 16.0f);
	CHECK(white.u0 == Approx(255.5f / 256.0f));
	CHECK(white.u1 == Approx(white.u0));
	CHECK(white.v == Approx(15.5f / 16.0f));

	// Blue halfway between slices 7 and 8 of a 16^3 LUT.
	const color_grading::StripCoordinates mid = color_grading::StripLutCoordinates(0.0f, 0.0f, 7.5f / 15.0f, 16.0f);
	CHECK(mid.u0 == Approx((7.0f * 16.0f + 0.5f) / 256.0f));
	CHECK(mid.u1 == Approx((8.0f * 16.0f + 0.5f) / 256.0f));
	CHECK(mid.sliceBlend == Approx(0.5f));

	// Out-of-range input clamps.
	const color_grading::StripCoordinates over = color_grading::StripLutCoordinates(2.0f, -1.0f, 3.0f, 32.0f);
	CHECK(over.u0 == Approx(1023.5f / 1024.0f));
	CHECK(over.v == Approx(0.5f / 32.0f));
}

TEST_CASE("A neutral strip LUT maps colours to themselves", "[color_grading]")
{
	for (const uint32 edge : { 16u, 32u })
	{
		const StripImage lut(edge);
		for (int ri = 0; ri <= 10; ++ri)
		{
			for (int gi = 0; gi <= 10; ++gi)
			{
				for (int bi = 0; bi <= 10; ++bi)
				{
					const float r = static_cast<float>(ri) / 10.0f;
					const float g = static_cast<float>(gi) / 10.0f;
					const float b = static_cast<float>(bi) / 10.0f;
					const Vector3 out = lut.Lookup(r, g, b);
					CHECK(std::abs(out.x - r) <= 1.0f / 255.0f + 1e-4f);
					CHECK(std::abs(out.y - g) <= 1.0f / 255.0f + 1e-4f);
					CHECK(std::abs(out.z - b) <= 1.0f / 255.0f + 1e-4f);
				}
			}
		}
	}
}
```

Check `Vector3` operator names (`operator*` with float, `operator+`) in `src/shared/math/vector3.h` and adapt.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t deferred_shading_tests`
Expected: compile error, `deferred_shading/color_grading.h` not found.

- [ ] **Step 3: Write the header**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Parametric colour grading applied after tone mapping (see docs/color-grading.md).
	/// @remark Dependency-free apart from base and math so the headless deferred_shading_tests target can compile it.
	struct ColorGradingSettings
	{
		/// @brief 0 = grey, 1 = unchanged.
		float saturation = 1.0f;

		/// @brief Contrast around mid-grey, 1 = unchanged.
		float contrast = 1.0f;

		/// @brief Multiplies the graded colour, 1 = unchanged.
		Vector3 colorFilter { 1.0f, 1.0f, 1.0f };

		/// @brief Sets the saturation, clamped to [0, 2].
		void SetSaturation(const float value)
		{
			saturation = std::clamp(value, 0.0f, 2.0f);
		}

		/// @brief Sets the contrast, clamped to [0, 2].
		void SetContrast(const float value)
		{
			contrast = std::clamp(value, 0.0f, 2.0f);
		}

		/// @brief Sets the colour filter, each channel clamped to [0, 2].
		void SetColorFilter(const Vector3& value)
		{
			colorFilter = Vector3(std::clamp(value.x, 0.0f, 2.0f), std::clamp(value.y, 0.0f, 2.0f), std::clamp(value.z, 0.0f, 2.0f));
		}
	};

	/// @brief Strip LUT addressing shared with PS_Tonemap.hlsl - change both together.
	namespace color_grading
	{
		/// @brief Largest saturation, contrast or colour filter channel.
		constexpr float MaxGradingValue = 2.0f;

		/// @brief Whether a texture is a strip LUT: N*N wide, N high, N >= 2.
		[[nodiscard]] inline bool IsStripLut(const uint32 width, const uint32 height)
		{
			return height >= 2 && width == height * height;
		}

		/// @brief Texture coordinates of the two reads that look up one colour.
		struct StripCoordinates
		{
			/// @brief u of the read in the lower slice.
			float u0;

			/// @brief u of the read in the upper slice.
			float u1;

			/// @brief v of both reads.
			float v;

			/// @brief Weight of the upper slice.
			float sliceBlend;
		};

		/// @brief Where to read colour (r, g, b) in a strip LUT of edge size. Input is clamped to [0, 1].
		[[nodiscard]] inline StripCoordinates StripLutCoordinates(float r, float g, float b, const float size)
		{
			r = std::clamp(r, 0.0f, 1.0f);
			g = std::clamp(g, 0.0f, 1.0f);
			b = std::clamp(b, 0.0f, 1.0f);

			const float scale = size - 1.0f;
			const float slice = b * scale;
			const float slice0 = std::floor(slice);
			const float slice1 = std::min(slice0 + 1.0f, scale);
			const float column = r * scale + 0.5f;
			const float width = size * size;

			StripCoordinates coordinates;
			coordinates.u0 = (slice0 * size + column) / width;
			coordinates.u1 = (slice1 * size + column) / width;
			coordinates.v = (g * scale + 0.5f) / size;
			coordinates.sliceBlend = slice - slice0;
			return coordinates;
		}
	}
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --config Debug -t deferred_shading_tests` then `bin/Debug/deferred_shading_tests.exe "[color_grading]"` and the whole suite.
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/deferred_shading/color_grading.h src/tests/deferred_shading_tests/test_color_grading.cpp
git commit -m "feat(render): colour grading settings and strip LUT addressing"
```

---

### Task 2: Grading fields in environment profiles

**Files:**
- Modify: both `environment_profiles.proto` copies (after `light_scattering = 25`)
- Modify: `src/shared/scene_graph/environment_profile.h`, `environment_profile_proto.h`, `environment_state.h`, `environment_state.cpp`, `environment_controller.cpp` (`Evaluate`)
- Modify: `src/mmo_edit/editor_windows/environment_profile_editor_window.cpp`
- Test: `src/tests/scene_graph_tests/test_environment_color_grading.cpp`

**Interfaces:**
- Produces: `EnvironmentProfile::colorLut (String), saturation, contrast, colorFilter (Vector3)`; `EnvironmentState::colorLut, colorLutFrom (String), colorLutBlend (float, default 1), saturation, contrast, colorFilter`.

Mirror every place `lightScattering` / `light_scattering` is handled (grep `lightScattering\|light_scattering` in `src/`) for the five numeric values; `color_lut` is a string handled as below.

- [ ] **Step 1: Write the failing test**

Model includes and helpers on `src/tests/scene_graph_tests/test_environment_light_scattering.cpp` and `test_environment_controller.cpp` (how they build profiles and drive `EnvironmentController`).

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_controller.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

#include <memory>

using namespace mmo;

namespace
{
	std::shared_ptr<const EnvironmentProfile> makeProfile(const String& lut, const float transitionSeconds)
	{
		EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
		profile.colorLut = lut;
		profile.transitionSeconds = transitionSeconds;
		return std::make_shared<const EnvironmentProfile>(profile);
	}
}

TEST_CASE("Colour grading defaults leave the image unchanged", "[environment]")
{
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
	CHECK(profile.colorLut.empty());
	CHECK(profile.saturation == Approx(1.0f));
	CHECK(profile.contrast == Approx(1.0f));
	CHECK(profile.colorFilter.x == Approx(1.0f));

	const EnvironmentState state = EvaluateEnvironment(profile, 0.5f);
	CHECK(state.colorLut.empty());
	CHECK(state.colorLutFrom.empty());
	CHECK(state.colorLutBlend == Approx(1.0f));
}

TEST_CASE("Colour grading loads clamped from the profile record", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(3);
	record.set_name("Graded");
	CHECK(record.saturation() == Approx(1.0f));
	CHECK(record.color_filter_b() == Approx(1.0f));

	record.set_color_lut("Textures/ColorGrading/Test.htex");
	record.set_saturation(9.0f);
	record.set_contrast(-1.0f);
	record.set_color_filter_r(0.5f);
	record.set_color_filter_g(3.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.colorLut == "Textures/ColorGrading/Test.htex");
	CHECK(profile.saturation == Approx(2.0f));
	CHECK(profile.contrast == Approx(0.0f));
	CHECK(profile.colorFilter.x == Approx(0.5f));
	CHECK(profile.colorFilter.y == Approx(2.0f));
	CHECK(profile.colorFilter.z == Approx(1.0f));
}

TEST_CASE("Colour grading sliders blend linearly", "[environment]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.saturation = 1.0f;
	b.saturation = 1.4f;
	a.contrast = 0.8f;
	b.contrast = 1.2f;
	a.colorFilter = Vector3(1.0f, 1.0f, 1.0f);
	b.colorFilter = Vector3(1.2f, 1.0f, 0.8f);

	const EnvironmentState half = LerpEnvironment(a, b, 0.5f);
	CHECK(half.saturation == Approx(1.2f));
	CHECK(half.contrast == Approx(1.0f));
	CHECK(half.colorFilter.x == Approx(1.1f));
	CHECK(half.colorFilter.z == Approx(0.9f));
}

TEST_CASE("The controller fades from the previous zone's LUT to the new one", "[environment]")
{
	EnvironmentController controller;
	controller.SetTarget(makeProfile("A.htex", 2.0f), true);
	CHECK(controller.GetState().colorLut == "A.htex");
	CHECK(controller.GetState().colorLutFrom.empty());
	CHECK(controller.GetState().colorLutBlend == Approx(1.0f));

	controller.SetTarget(makeProfile("B.htex", 2.0f), false);
	controller.Update(0.5f, 0.5f);
	CHECK(controller.GetState().colorLut == "B.htex");
	CHECK(controller.GetState().colorLutFrom == "A.htex");
	CHECK(controller.GetState().colorLutBlend == Approx(0.25f));

	controller.Update(2.0f, 0.5f);
	CHECK(controller.GetState().colorLut == "B.htex");
	CHECK(controller.GetState().colorLutBlend == Approx(1.0f));
}

TEST_CASE("With three profiles blending the strongest other LUT is kept", "[environment]")
{
	EnvironmentController controller;
	controller.SetTarget(makeProfile("A.htex", 4.0f), true);
	controller.SetTarget(makeProfile("B.htex", 4.0f), false);
	controller.Update(3.0f, 0.5f);           // B at 0.75, A at 0.25
	controller.SetTarget(makeProfile("C.htex", 4.0f), false);
	controller.Update(0.4f, 0.5f);           // C at 0.1; B is the strongest other entry

	CHECK(controller.GetState().colorLut == "C.htex");
	CHECK(controller.GetState().colorLutFrom == "B.htex");
	CHECK(controller.GetState().colorLutBlend == Approx(0.1f).margin(1e-3));
}
```

Adapt constructor, `SetTarget`, `Update` and `GetState` to the real `EnvironmentController` API and `LoadEnvironmentProfile` to the real loader name, as the existing tests use them. If the controller requires a starting profile in its constructor, follow the existing tests.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, no member `colorLut`.

- [ ] **Step 3: Implement**

Both protos, after `light_scattering = 25` (match each file's indentation):

```proto
	// Colour grading, applied after tone mapping; see docs/color-grading.md.
	// Texture path of a strip LUT (256x16 or 1024x32, imported uncompressed). Empty = none.
	optional string color_lut = 26;
	// 0 = grey, 1 = unchanged.
	optional float saturation = 27 [default = 1];
	// Contrast around mid-grey, 1 = unchanged.
	optional float contrast = 28 [default = 1];
	// Multiplies the graded colour, 1 = unchanged.
	optional float color_filter_r = 29 [default = 1];
	optional float color_filter_g = 30 [default = 1];
	optional float color_filter_b = 31 [default = 1];
```

`environment_profile.h` (Doxygen each):

```cpp
		String colorLut;
		float saturation = 1.0f;
		float contrast = 1.0f;
		Vector3 colorFilter { 1.0f, 1.0f, 1.0f };
```

Loader (`environment_profile_proto.h`):

```cpp
		result.colorLut = profile.color_lut();
		result.saturation = std::clamp(profile.saturation(), 0.0f, 2.0f);
		result.contrast = std::clamp(profile.contrast(), 0.0f, 2.0f);
		result.colorFilter = Vector3(
			std::clamp(profile.color_filter_r(), 0.0f, 2.0f),
			std::clamp(profile.color_filter_g(), 0.0f, 2.0f),
			std::clamp(profile.color_filter_b(), 0.0f, 2.0f));
```

`environment_state.h` (Doxygen each; explain the LUT pair):

```cpp
		String colorLut;              // target profile's LUT
		String colorLutFrom;          // LUT being faded out (strongest other blending profile)
		float colorLutBlend = 1.0f;   // weight of colorLut; colorLutFrom gets 1 - colorLutBlend
		float saturation = 1.0f;
		float contrast = 1.0f;
		Vector3 colorFilter { 1.0f, 1.0f, 1.0f };
```

`environment_state.cpp`:
- `EvaluateEnvironment`: copy `colorLut`, `saturation`, `contrast`, `colorFilter`; `colorLutFrom` empty; `colorLutBlend = 1`.
- `LerpEnvironment`: lerp `saturation`, `contrast` and each `colorFilter` channel; for the LUT fields set `state.colorLut = b.colorLut; state.colorLutFrom = a.colorLut; state.colorLutBlend = t;` with a comment that `EnvironmentController::Evaluate` replaces them with the target/strongest-other pair after folding.

`environment_controller.cpp` `Evaluate`, after the fold (both the normal path and the all-zero-weights fallback):

```cpp
		// LUTs cannot be averaged: show the target's LUT faded in over the strongest other profile's.
		const Entry& target = m_entries.back();
		const Entry* strongestOther = nullptr;
		for (size_t i = 0; i + 1 < m_entries.size(); ++i)
		{
			if (m_entries[i].weight > 0.0f && (strongestOther == nullptr || m_entries[i].weight > strongestOther->weight))
			{
				strongestOther = &m_entries[i];
			}
		}

		m_state.colorLut = target.profile->colorLut;
		m_state.colorLutFrom = strongestOther != nullptr ? strongestOther->profile->colorLut : String();
		m_state.colorLutBlend = strongestOther != nullptr ? std::clamp(target.weight, 0.0f, 1.0f) : 1.0f;
```

Editor window: add a section after "Wind & Noise", following the existing `ScopedEditorSection` pattern and the same `changed` / preview-revision handling the other sections use:

```cpp
		if (const auto section = ScopedEditorSection("Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;

			String lut = entry.color_lut();
			if (AssetPickerWidget::Draw("Color LUT", lut, asset_extensions::Textures))
			{
				if (lut.empty())
				{
					entry.clear_color_lut();
				}
				else
				{
					entry.set_color_lut(lut);
				}
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Strip LUT (256x16 or 1024x32), imported with compression off. See docs/color-grading.md.");
			}

			float saturation = entry.saturation();
			if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.0f, "%.2f"))
			{
				entry.set_saturation(std::clamp(saturation, 0.0f, 2.0f));
				changed = true;
			}

			float contrast = entry.contrast();
			if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 2.0f, "%.2f"))
			{
				entry.set_contrast(std::clamp(contrast, 0.0f, 2.0f));
				changed = true;
			}

			float filter[3] = { entry.color_filter_r(), entry.color_filter_g(), entry.color_filter_b() };
			if (ImGui::ColorEdit3("Color Filter", filter, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
			{
				entry.set_color_filter_r(std::clamp(filter[0], 0.0f, 2.0f));
				entry.set_color_filter_g(std::clamp(filter[1], 0.0f, 2.0f));
				entry.set_color_filter_b(std::clamp(filter[2], 0.0f, 2.0f));
				changed = true;
			}

			// ...same post-change handling as the other sections (bump the preview revision).
		}
```

Include `asset_picker_widget.h` (and whatever defines `asset_extensions::Textures`, see `water_profile_editor_window.cpp`). Replace the trailing comment with the exact code the other sections use.

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build build --config Debug -t scene_graph_tests client_data_tests mmo_client mmo_edit`
Expected: builds; `bin/Debug/scene_graph_tests.exe` and `bin/Debug/client_data_tests.exe` pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/proto_data/environment_profiles.proto src/shared/client_data/environment_profiles.proto src/shared/scene_graph/environment_profile.h src/shared/scene_graph/environment_profile_proto.h src/shared/scene_graph/environment_state.h src/shared/scene_graph/environment_state.cpp src/shared/scene_graph/environment_controller.cpp src/mmo_edit/editor_windows/environment_profile_editor_window.cpp src/tests/scene_graph_tests/test_environment_color_grading.cpp
git commit -m "feat(render): environment profiles carry a colour grade"
```

---

### Task 3: Grading in the tonemap pass

**Files:**
- Modify: `src/shared/deferred_shading/tonemap_settings.h`
- Modify: `src/shared/deferred_shading/tonemap_pass.h`, `tonemap_pass.cpp`
- Modify: `src/shared/deferred_shading/shaders/PS_Tonemap.hlsl`
- Modify: `src/shared/deferred_shading/deferred_renderer.h` (API next to `SetExposure`)
- Modify: `src/mmo_client/game_states/world_state.cpp` (next to `SetFogLightScattering`), `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` (same)
- Test: `src/tests/deferred_shading_tests/test_color_grading.cpp` (settings live in `TonemapSettings`)

**Interfaces:**
- Consumes: `ColorGradingSettings`, `color_grading::IsStripLut`, strip formula (Task 1); `EnvironmentState::colorLut / colorLutFrom / colorLutBlend / saturation / contrast / colorFilter` (Task 2).
- Produces: `TonemapSettings::grading` (`ColorGradingSettings`); `TonemapPass::SetLuts(const String& lut, const String& lutFrom, float blend)`; `DeferredRenderer::SetColorGrading(const ColorGradingSettings& settings, const String& lut, const String& lutFrom, float lutBlend)`.

- [ ] **Step 1: Write the failing test**

Append to `test_color_grading.cpp` (include `deferred_shading/tonemap_settings.h`):

```cpp
TEST_CASE("Tonemap settings start with a neutral grade", "[color_grading]")
{
	const TonemapSettings settings;
	CHECK(settings.grading.saturation == Approx(1.0f));
	CHECK(settings.grading.contrast == Approx(1.0f));
	CHECK(settings.grading.colorFilter.y == Approx(1.0f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --config Debug -t deferred_shading_tests`
Expected: compile error, no member `grading`.

- [ ] **Step 3: Settings and constant buffer**

`tonemap_settings.h`: include `color_grading.h`; add

```cpp
		/// @brief Parametric grade applied after gamma (saturation, contrast, colour filter).
		ColorGradingSettings grading;
```

`tonemap_pass.cpp` `TonemapConstants`:

```cpp
		struct alignas(16) TonemapConstants
		{
			float exposure;
			float bloomScale;
			float ditherStrength;
			float saturation;

			float colorFilter[3];
			float contrast;

			float lutBlend;
			float lutSize;
			float lutFromSize;
			float gradingPadding;
		};

		static_assert(sizeof(TonemapConstants) == 48, "TonemapConstants must match the HLSL layout");
```

- [ ] **Step 4: LUT loading in `TonemapPass`**

`tonemap_pass.h`: add

```cpp
		/// @brief Sets the zone LUTs for the next frames. Empty paths mean no LUT.
		/// @param lut Target LUT texture path.
		/// @param lutFrom LUT being faded out.
		/// @param blend Weight of lut (lutFrom gets 1 - blend).
		void SetLuts(const String& lut, const String& lutFrom, float blend);
```

private:

```cpp
		/// @brief Loads a strip LUT once; empty, missing or wrongly sized textures resolve to nullptr (warned once).
		TexturePtr ResolveLut(const String& path);

		std::map<String, TexturePtr> m_lutCache;
		TexturePtr m_lut;
		TexturePtr m_lutFrom;
		float m_lutBlend = 1.0f;
		SamplerStatePtr m_lutSampler;
```

`tonemap_pass.cpp` (include `graphics/texture_mgr.h`, `graphics/sampler_state.h`, `color_grading.h`, `<map>`; create the sampler in the constructor like `volumetric_fog_pass.cpp` creates its linear-clamp sampler, `ASSERT(m_lutSampler)`):

```cpp
	void TonemapPass::SetLuts(const String& lut, const String& lutFrom, const float blend)
	{
		m_lut = ResolveLut(lut);
		m_lutFrom = ResolveLut(lutFrom);
		m_lutBlend = std::clamp(blend, 0.0f, 1.0f);
	}

	TexturePtr TonemapPass::ResolveLut(const String& path)
	{
		if (path.empty())
		{
			return nullptr;
		}

		if (const auto it = m_lutCache.find(path); it != m_lutCache.end())
		{
			return it->second;
		}

		TexturePtr texture = TextureManager::Get().CreateOrRetrieve(path);
		if (!texture)
		{
			WLOG("Colour grading LUT '" << path << "' could not be loaded; grading without it.");
		}
		else if (!color_grading::IsStripLut(texture->GetWidth(), texture->GetHeight()))
		{
			WLOG("Colour grading LUT '" << path << "' is " << texture->GetWidth() << "x" << texture->GetHeight()
				<< ", expected a strip of N*N x N (256x16 or 1024x32); grading without it.");
			texture = nullptr;
		}

		// Failures are cached too, so each bad path warns once.
		m_lutCache[path] = texture;
		return texture;
	}
```

In `Render`, fill the new constants:

```cpp
		const ColorGradingSettings& grading = m_settings.grading;
		constants.saturation = grading.saturation;
		constants.colorFilter[0] = grading.colorFilter.x;
		constants.colorFilter[1] = grading.colorFilter.y;
		constants.colorFilter[2] = grading.colorFilter.z;
		constants.contrast = grading.contrast;
		constants.lutBlend = m_lutBlend;
		constants.lutSize = m_lut ? static_cast<float>(m_lut->GetHeight()) : 0.0f;
		constants.lutFromSize = m_lutFrom ? static_cast<float>(m_lutFrom->GetHeight()) : 0.0f;
		constants.gradingPadding = 0.0f;
```

After the bloom bind: bind `m_lut ? m_lut : m_blackTexture` at PS t2 and `m_lutFrom ? m_lutFrom : m_blackTexture` at PS t3 with `m_device.BindTexture`, then `m_lutSampler->Bind(ShaderType::PixelShader, 4);` (legacy `BindTexture` writes its own sampler into the slot index of each texture, s0-s3, so the LUT sampler uses s4). After `Draw`, unbind t2 and t3 like t0/t1.

- [ ] **Step 5: Shader**

`PS_Tonemap.hlsl`: replace the cbuffer with the spec's 48-byte layout:

```hlsl
cbuffer TonemapBuffer : register(b2)
{
    float Exposure;
    float BloomScale;       // bloom intensity already divided by the level count; 0 without bloom
    float DitherStrength;   // in 8-bit steps
    float Saturation;       // 0 = grey, 1 = unchanged
    float3 ColorFilter;     // multiplies the graded colour
    float Contrast;         // around mid-grey, 1 = unchanged
    float LutBlend;         // weight of LutTexture; LutFromTexture gets 1 - LutBlend
    float LutSize;          // edge length of LutTexture, 0 = none
    float LutFromSize;      // edge length of LutFromTexture, 0 = none
    float _GradingPadding;
};
```

Resources and the lookup (mirrors `color_grading::StripLutCoordinates`):

```hlsl
Texture2D LutTexture : register(t2);
Texture2D LutFromTexture : register(t3);
SamplerState LutSampler : register(s4);

// Strip LUT lookup (N*N x N; blue = slice, red = column, green = row). Mirrors
// deferred_shading/color_grading.h - change both together. Size 0 passes the colour through.
float3 SampleStripLut(Texture2D lut, float size, float3 color)
{
    if (size < 2.0f)
    {
        return color;
    }

    float3 c = saturate(color);
    float scale = size - 1.0f;
    float slice = c.b * scale;
    float slice0 = floor(slice);
    float slice1 = min(slice0 + 1.0f, scale);
    float column = c.r * scale + 0.5f;
    float width = size * size;
    float v = (c.g * scale + 0.5f) / size;

    float3 lower = lut.SampleLevel(LutSampler, float2((slice0 * size + column) / width, v), 0.0f).rgb;
    float3 upper = lut.SampleLevel(LutSampler, float2((slice1 * size + column) / width, v), 0.0f).rgb;
    return lerp(lower, upper, slice - slice0);
}
```

In `main`, after `float3 color = pow(ACESFilm(hdr * Exposure), 1.0f / 2.2f);` and before the dither:

```hlsl
    // Colour grading in display space: filter, saturation, contrast, then the zone LUTs.
    color *= ColorFilter;
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luma.xxx, color, Saturation);
    color = saturate((color - 0.5f) * Contrast + 0.5f);
    color = lerp(SampleStripLut(LutFromTexture, LutFromSize, color), SampleStripLut(LutTexture, LutSize, color), LutBlend);
```

Update the file header comment to mention grading.

- [ ] **Step 6: Renderer API and hosts**

`deferred_renderer.h`, next to `SetExposure`:

```cpp
        /// @brief Sets the colour grade applied after tone mapping.
        /// @param settings Saturation, contrast and colour filter.
        /// @param lut Target LUT texture path (empty = none).
        /// @param lutFrom LUT being faded out (empty = none).
        /// @param lutBlend Weight of lut; lutFrom gets 1 - lutBlend.
        void SetColorGrading(const ColorGradingSettings& settings, const String& lut, const String& lutFrom, float lutBlend)
        {
            m_tonemapPass->GetSettings().grading = settings;
            m_tonemapPass->SetLuts(lut, lutFrom, lutBlend);
        }
```

Client `world_state.cpp` and editor `world_editor_instance.cpp`, next to `SetFogLightScattering(state.lightScattering)`:

```cpp
		ColorGradingSettings grading;
		grading.SetSaturation(state.saturation);
		grading.SetContrast(state.contrast);
		grading.SetColorFilter(state.colorFilter);
		renderer->SetColorGrading(grading, state.colorLut, state.colorLutFrom, state.colorLutBlend);
```

(add `#include "deferred_shading/color_grading.h"` where needed).

- [ ] **Step 7: Build and run the tests**

Run: `cmake --build build --config Debug -t deferred_shading_tests scene_graph_tests mmo_client mmo_edit`
Expected: builds (the shader compiles at build time); `bin/Debug/deferred_shading_tests.exe` and `bin/Debug/scene_graph_tests.exe` pass.

- [ ] **Step 8: Commit**

```bash
git add src/shared/deferred_shading/tonemap_settings.h src/shared/deferred_shading/tonemap_pass.h src/shared/deferred_shading/tonemap_pass.cpp src/shared/deferred_shading/shaders/PS_Tonemap.hlsl src/shared/deferred_shading/deferred_renderer.h src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_editor_instance.cpp src/tests/deferred_shading_tests/test_color_grading.cpp
git commit -m "feat(render): tonemap pass applies the zone colour grade and LUTs"
```

---

### Task 4: Neutral LUT, test grade and docs

**Files:**
- Create: `tools/color_grading/make_luts.py`
- Create (content, commit inside the `data/client` submodule only): `data/client/Textures/ColorGrading/NeutralLUT32.png`, `data/client/Textures/ColorGrading/NeutralLUT32.htex`
- Create: `docs/color-grading.md`
- Modify: `docs/rendering-atmosphere.md` (pipeline list: grading in the TonemapPass; profile field list)

**Interfaces:**
- Consumes: strip layout (Global Constraints).
- Produces: `Textures/ColorGrading/NeutralLUT32.htex` (asset path as the game references it).

- [ ] **Step 1: Write the tool**

`tools/color_grading/make_luts.py` (numpy + PIL):
- `neutral(size)` returns a `size*size` x `size` RGB uint8 image: texel `(x, y)` = `(column / (N-1), y / (N-1), slice / (N-1)) * 255` rounded, with `slice = x // N`, `column = x % N`.
- `write_htex(path, rgba)`: writes an **uncompressed RGBA8** `.htex` with a full mip chain, following the header layout of `write_htex` in `tools/derive_height_map.py` (magic `HTEX`, version `0x0100`, 142-byte header) but with the RGBA pixel format; look up the RGBA8 enum value in the tex v1_0 header under `src/shared/tex/`.
- CLI:
  - `python tools/color_grading/make_luts.py neutral --size 32 --out-dir data/client/Textures/ColorGrading` writes `NeutralLUT32.png` and `NeutralLUT32.htex`.
  - `python tools/color_grading/make_luts.py test-grade --size 32 --out <path.htex>` writes a strong warm, high-contrast grade (for example `r' = r^0.85 * 1.1`, `b' = b^1.2 * 0.85`, clamped) as `.htex` for verification only (never shipped).
- Header comment explains the Unreal-style strip layout and points to `docs/color-grading.md`.

- [ ] **Step 2: Generate and verify**

Run the neutral command. Verify with the existing inspector:
`python .claude/skills/mmo-material-editor/scripts/htex_tool.py inspect data/client/Textures/ColorGrading/NeutralLUT32.htex` — expect 1024x32, uncompressed RGBA.
`python .claude/skills/mmo-material-editor/scripts/htex_tool.py preview data/client/Textures/ColorGrading/NeutralLUT32.htex --output <scratch>.png` — the preview's mip 0 must equal `NeutralLUT32.png` pixel for pixel (compare with PIL/numpy in a one-off check; state the result in the report).

- [ ] **Step 3: Docs**

`docs/color-grading.md`: what grading does and where it runs (after ACES + gamma, before dither; sliders then LUT; zone cross-fade of two LUTs), the profile fields and their ranges, the authoring workflow from the spec (screenshot, paste `NeutralLUT32.png`, grade, crop exactly 1024x32 or 256x16, import with compression off, pick in the profile), rules (width = height squared, uncompressed, bad LUTs are ignored with one warning), and the tool commands.
`docs/rendering-atmosphere.md`: mention grading in the TonemapPass pipeline line and add the grading fields to the profile field list, linking `color-grading.md`.

- [ ] **Step 4: Commit**

Main repo:

```bash
git add tools/color_grading/make_luts.py docs/color-grading.md docs/rendering-atmosphere.md
git commit -m "feat(tools): neutral and test colour grading LUTs, grading docs"
```

Inside the `data/client` submodule (do not bump the pointer in the main repo):

```bash
git -C data/client add Textures/ColorGrading/NeutralLUT32.png Textures/ColorGrading/NeutralLUT32.htex
git -C data/client commit -m "data: neutral 32^3 colour grading LUT"
```

---

## Controller steps (after all tasks)

1. Final whole-branch review over the grading commits.
2. Falwyn Forest profile (data, both projects): `saturation = 1.15` (regenerate the scratchpad descriptor sets first; the fields are new).
3. Full gate; visual check (neutral LUT identical, saturation 1.15, test grade, zone-border fade).
