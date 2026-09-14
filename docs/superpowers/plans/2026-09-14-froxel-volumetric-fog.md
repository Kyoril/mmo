# Froxel Volumetric Fog with Zone Wind Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the per-pixel light-shaft ray march with froxel volumetric fog: a camera-aligned 3D grid filled by compute shaders with height fog × wind-driven noise and shadowed sun light, smoothed over time, and composited onto the opaque scene. Wind and noise are authored per zone.

**Architecture:**
- **GPU compute foundation:** `GraphicsDevice` gains volume textures, compute shaders and sampler states, implemented for D3D11 with safe defaults elsewhere.
- **Fog pass:** a new `VolumetricFogPass` runs inject → temporal → integrate compute steps and a full-screen composite, replacing `AtmospherePass` at the same spot in `DeferredRenderer::Render`.
- **Wind:** a pure `WindSimulation` turns the environment profile's wind values into a scrolling noise offset that the client and editor publish through `Scene::SetWind`.

**Tech Stack:** C++17, HLSL shader model 5.0 (vertex/pixel/compute, compiled by MSBuild through CMake `VS_SHADER_*` properties), D3D11, Catch2, protobuf 2, ImGui.

Spec: [docs/superpowers/specs/2026-09-14-froxel-volumetric-fog-design.md](../specs/2026-09-14-froxel-volumetric-fog-design.md)

## Global Constraints

- **Branch and git:** stay on `feature/volumetric-atmosphere`. Never push. Never run `/code-review ultra`. Stage files with explicit paths (never `git add -A`). Leave the `data/client` and `data/editor` submodules untouched.
- **Error handling:** no exceptions. Use `ASSERT`/`VERIFY` (`base/macros.h`) and `DLOG`/`WLOG`/`ELOG` (`log/default_log_levels.h`).
- **C++ style:**
  - Allman braces, braces on every `if`, tabs.
  - `m_camelCase` members, `PascalCase` methods, `camelCase` locals and anonymous-namespace free functions.
  - Every new file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` Headers use `#pragma once` and Doxygen `///` on public members.
- **Commit messages** end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- **New `GraphicsDevice` virtuals** have safe default bodies (return `nullptr` or no-op). `GraphicsDeviceNull` and the Metal stub stay unchanged.
- **Matrix convention:**
  - C++ builds view-projection as `projection * view`, uploads `Matrix4` unchanged, and HLSL declares `column_major matrix` and uses `mul(float4(p, 1), M)`. This mirrors `ShadowBuffer::cascadeViewProjections`.
  - Inverses use `Matrix4::Inverse()`.
- **Camera cbuffer (b1)** stays byte-identical. No material rebuild.
- **Formula copies:** the height-fog formulas in `AtmosphereCommon.hlsli`, `MaterialCompilerD3D11` and `deferred_shading/atmosphere_math.h` stay identical.
- **Proto field numbers** in `src/shared/proto_data/environment_profiles.proto` and `src/shared/client_data/environment_profiles.proto` are identical and never renumbered.
- **Profile wind defaults:** wind_direction 45 (degrees clockwise from +Z, direction the wind blows toward), wind_speed 3 m/s, wind_gustiness 0.3, fog_noise_amount 0.5, fog_noise_size 60 m.
- **Profile wind clamps:** direction wrapped to [0, 360), speed [0, 30], gustiness [0, 1], noise amount [0, 1], noise size [5, 500].
- **Quality presets** (tile px / depth slices): 0 Off (volume disabled, analytic fog only), 1 Low 24/32, 2 Medium 16/48, 3 High 12/64 (default), 4 Ultra 8/96.
- **Fog grid:** range default 200 m, clamped to [50, 300]. Near distance 0.5 m. Sky distance 2000 m. Temporal blend 0.9. History reset when the camera moves more than 50 m in one frame. Noise volume 64³.
- **Build targets:** the user's login/realm/world servers may be running and lock their executables. Build only named targets and never kill processes. If `mmo_client.exe` or `mmo_edit.exe` is locked, report it instead of closing anything.
- **CMake:** after adding source or shader files run `cmake -S . -B build`, because libraries, tests and shaders glob their directories.

## Build and test commands

```powershell
cmake -S . -B build
cmake --build build --config Debug -t deferred_shading_tests scene_graph_tests client_data_tests
bin/Debug/deferred_shading_tests.exe "[volumetric_fog]"
bin/Debug/scene_graph_tests.exe "[wind]"
cmake --build build --config Debug -t mmo_client mmo_edit
```

## File map

| File | Responsibility |
|---|---|
| `src/shared/graphics/volume_texture.h` (new) | `VolumeFormat`, abstract `VolumeTexture` |
| `src/shared/graphics/sampler_state.h` (new) | `SamplerDesc`, abstract `SamplerState` |
| `src/shared/graphics/compute_shader.h` (new) | `ComputeShader` shader base type |
| `src/shared/graphics/graphics_device.h` | `CreateVolumeTexture`, `CreateSamplerState`, `Dispatch`, `ClearComputeBindings` |
| `src/shared/graphics_d3d11/volume_texture_d3d11.h/.cpp` (new) | `ID3D11Texture3D` + SRV/UAV |
| `src/shared/graphics_d3d11/sampler_state_d3d11.h/.cpp` (new) | `ID3D11SamplerState` |
| `src/shared/graphics_d3d11/compute_shader_d3d11.h/.cpp` (new) | `ID3D11ComputeShader` |
| `src/shared/graphics_d3d11/graphics_device_d3d11.h/.cpp` | Overrides; compute case in `CreateShader` |
| `src/shared/graphics_d3d11/texture_d3d11.cpp`, `render_texture_d3d11.cpp`, `constant_buffer_d3d11.cpp` | Compute-stage SRV/cbuffer binding |
| `src/tests/scene_graph_tests/test_graphics_compute_api.cpp` (new) | Null-device safety |
| `src/shared/deferred_shading/volumetric_fog_settings.h` (new) | Presets, slice math, Halton jitter, noise weighting |
| `src/shared/deferred_shading/fog_noise.h/.cpp` (new) | Tiling FBM noise volume generator |
| `src/tests/deferred_shading_tests/test_volumetric_fog_settings.cpp`, `test_fog_noise.cpp` (new) | Pure math tests |
| `src/shared/{proto_data,client_data}/environment_profiles.proto` | Fields 20–24 |
| `src/shared/scene_graph/environment_profile.h`, `environment_profile_proto.h`, `environment_state.h/.cpp` | Wind fields, load, evaluate, blend |
| `src/mmo_edit/editor_windows/environment_profile_editor_window.h/.cpp` | Wind & Noise section |
| `src/shared/scene_graph/wind_state.h` (new) | `WindState` POD |
| `src/shared/scene_graph/wind_simulation.h/.cpp` (new) | Gusts, offset integration, shader publish |
| `src/shared/scene_graph/scene.h` | `SetWind` / `GetWind` |
| `src/shared/graphics/global_shader_parameters.cpp` | `WindDirection` engine default |
| `src/mmo_client/game_states/world_state.h/.cpp`, `src/mmo_edit/editors/world_editor/world_editor_instance.h/.cpp` | Own and update `WindSimulation` |
| `src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli` (new) | Fog cbuffer, slice math, shadow lookup |
| `src/shared/deferred_shading/shaders/CS_FogInject.hlsl`, `CS_FogTemporal.hlsl`, `CS_FogIntegrate.hlsl`, `PS_FogComposite.hlsl` (new) | Pipeline shaders |
| `src/shared/deferred_shading/volumetric_fog_pass.h/.cpp` (new) | Pass orchestration |
| `src/shared/deferred_shading/CMakeLists.txt`, `.gitignore` | Compute shader build, generated headers |
| `src/shared/deferred_shading/deferred_renderer.h/.cpp` | Use the new pass and `SamplerState` |
| Removed: `atmosphere_pass.h/.cpp`, `atmosphere_pass_settings.h`, `PS_AtmosphereMarch/Blur/Composite.hlsl`, `test_atmosphere_pass_settings.cpp` | Old march |
| `docs/rendering-atmosphere.md`, `docs/console_commands.md` | Docs |

---

### Task 1: GPU compute foundation (volume textures, compute shaders, sampler states)

**Files:**
- Create: `src/shared/graphics/volume_texture.h`, `src/shared/graphics/sampler_state.h`, `src/shared/graphics/compute_shader.h`
- Modify: `src/shared/graphics/graphics_device.h` (after `CreateOcclusionQuery`, line ~232)
- Create: `src/shared/graphics_d3d11/volume_texture_d3d11.h/.cpp`, `sampler_state_d3d11.h/.cpp`, `compute_shader_d3d11.h/.cpp`
- Modify: `src/shared/graphics_d3d11/graphics_device_d3d11.h` (friend list ~31, overrides after `CreateOcclusionQuery`, members ~337), `graphics_device_d3d11.cpp` (`CreateShader` ~800)
- Modify: `src/shared/graphics_d3d11/texture_d3d11.cpp:433-450`, `src/shared/graphics_d3d11/render_texture_d3d11.cpp` (both `Bind` switches, ~63 and ~268)
- Test: `src/tests/scene_graph_tests/test_graphics_compute_api.cpp`

**Interfaces:**
- Produces:
  - `enum class VolumeFormat { R8, RGBA16F };`
  - `class VolumeTexture` with `Bind(ShaderType, uint32)`, `BindWritable(uint32)`, `Upload(const uint8*, size_t)`, `GetWidth()`, `GetHeight()`, `GetDepth()`, `GetFormat()`, `IsWritable()`.
  - `using VolumeTexturePtr = std::shared_ptr<VolumeTexture>;`
  - `enum class SamplerFilter { Point, Linear, Anisotropic, ComparisonLinear, ComparisonAnisotropic };`
  - `enum class SamplerAddress { Clamp, Wrap, Border };`
  - `enum class SamplerComparison { LessEqual, Less, Always };`
  - `struct SamplerDesc { SamplerFilter filter; SamplerAddress address; SamplerComparison comparison; float borderColor[4]; uint32 maxAnisotropy; };`
  - `class SamplerState` with `Bind(ShaderType, uint32)`. `using SamplerStatePtr = std::shared_ptr<SamplerState>;`
  - `class ComputeShader : public ShaderBase` (type `ComputeShader`).
  - `GraphicsDevice` virtuals, all with default bodies:
    - `VolumeTexturePtr CreateVolumeTexture(uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable)`
    - `SamplerStatePtr CreateSamplerState(const SamplerDesc& desc)`
    - `void Dispatch(uint32 groupsX, uint32 groupsY, uint32 groupsZ)`
    - `void ClearComputeBindings()`
  - `CreateShader(ShaderType::ComputeShader, …)` works on D3D11.
  - `Texture::Bind`, `RenderTexture::Bind` and `ConstantBuffer::BindToStage` accept `ShaderType::ComputeShader` on D3D11.

- [ ] **Step 1: Write the failing null-device test**

Create `src/tests/scene_graph_tests/test_graphics_compute_api.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/graphics_device.h"
#include "graphics/sampler_state.h"
#include "graphics/volume_texture.h"
#include "null_device.h"

using namespace mmo;

TEST_CASE("Compute API defaults are safe no-ops on the null device", "[graphics_compute]")
{
	GraphicsDevice& device = test::EnsureNullDevice();

	// Backends without compute support return no resources; callers must treat nullptr as
	// "feature unavailable" rather than crash.
	CHECK(device.CreateVolumeTexture(4, 4, 4, VolumeFormat::R8, false) == nullptr);
	CHECK(device.CreateVolumeTexture(8, 8, 8, VolumeFormat::RGBA16F, true) == nullptr);

	SamplerDesc desc;
	desc.filter = SamplerFilter::ComparisonLinear;
	desc.address = SamplerAddress::Border;
	CHECK(device.CreateSamplerState(desc) == nullptr);

	device.Dispatch(1, 1, 1);
	device.ClearComputeBindings();
	SUCCEED("Dispatch and ClearComputeBindings returned without side effects");
}

TEST_CASE("SamplerDesc defaults describe a linear clamp sampler", "[graphics_compute]")
{
	const SamplerDesc desc;
	CHECK(desc.filter == SamplerFilter::Linear);
	CHECK(desc.address == SamplerAddress::Clamp);
	CHECK(desc.comparison == SamplerComparison::LessEqual);
	CHECK(desc.maxAnisotropy == 1u);
	CHECK(desc.borderColor[0] == Approx(0.0f));
	CHECK(desc.borderColor[3] == Approx(0.0f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `graphics/sampler_state.h` not found.

- [ ] **Step 3: Add the backend-neutral headers**

Create `src/shared/graphics/volume_texture.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/shader_base.h"

#include <memory>

namespace mmo
{
	/// @brief Texel formats a volume texture can hold.
	enum class VolumeFormat
	{
		/// @brief One unsigned normalized 8-bit channel (noise).
		R8,

		/// @brief Four 16-bit float channels (fog scattering and extinction).
		RGBA16F
	};

	/// @brief A 3D texture. Readable from any shader stage; optionally writable from compute shaders.
	class VolumeTexture : public NonCopyable
	{
	public:
		/// @brief Creates the description of a volume texture.
		VolumeTexture(const uint16 width, const uint16 height, const uint16 depth, const VolumeFormat format, const bool writable)
			: m_width(width)
			, m_height(height)
			, m_depth(depth)
			, m_format(format)
			, m_writable(writable)
		{
		}

		~VolumeTexture() override = default;

	public:
		/// @brief Binds the texture for reading at a register of a shader stage.
		virtual void Bind(ShaderType stage, uint32 slot) = 0;

		/// @brief Binds the texture for writing at a compute shader UAV register. Only valid when writable.
		virtual void BindWritable(uint32 slot) = 0;

		/// @brief Uploads texel data. The size must equal width * height * depth * bytes per texel.
		virtual void Upload(const uint8* data, size_t size) = 0;

		/// @brief Width in texels.
		[[nodiscard]] uint16 GetWidth() const { return m_width; }

		/// @brief Height in texels.
		[[nodiscard]] uint16 GetHeight() const { return m_height; }

		/// @brief Depth in texels.
		[[nodiscard]] uint16 GetDepth() const { return m_depth; }

		/// @brief Texel format.
		[[nodiscard]] VolumeFormat GetFormat() const { return m_format; }

		/// @brief Whether compute shaders may write the texture.
		[[nodiscard]] bool IsWritable() const { return m_writable; }

	protected:
		uint16 m_width;
		uint16 m_height;
		uint16 m_depth;
		VolumeFormat m_format;
		bool m_writable;
	};

	using VolumeTexturePtr = std::shared_ptr<VolumeTexture>;
}
```

Create `src/shared/graphics/sampler_state.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/shader_base.h"

#include <memory>

namespace mmo
{
	/// @brief Texture filtering of a sampler state.
	enum class SamplerFilter
	{
		Point,
		Linear,
		Anisotropic,
		ComparisonLinear,
		ComparisonAnisotropic
	};

	/// @brief Texture coordinate addressing of a sampler state (applied to all three axes).
	enum class SamplerAddress
	{
		Clamp,
		Wrap,
		Border
	};

	/// @brief Comparison function of comparison samplers.
	enum class SamplerComparison
	{
		LessEqual,
		Less,
		Always
	};

	/// @brief Describes a sampler state.
	struct SamplerDesc
	{
		SamplerFilter filter = SamplerFilter::Linear;
		SamplerAddress address = SamplerAddress::Clamp;
		SamplerComparison comparison = SamplerComparison::LessEqual;
		float borderColor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		uint32 maxAnisotropy = 1;
	};

	/// @brief An immutable sampler state object.
	class SamplerState : public NonCopyable
	{
	public:
		~SamplerState() override = default;

	public:
		/// @brief Binds the sampler at a sampler register of a shader stage.
		virtual void Bind(ShaderType stage, uint32 slot) = 0;
	};

	using SamplerStatePtr = std::shared_ptr<SamplerState>;
}
```

Create `src/shared/graphics/compute_shader.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shader_base.h"

namespace mmo
{
	/// @brief Base class of a compute shader. Set() makes it the active compute program.
	class ComputeShader : public ShaderBase
	{
	public:
		ComputeShader() = default;
		~ComputeShader() override = default;

	public:
		[[nodiscard]] ShaderType GetType() const override { return ShaderType::ComputeShader; }
	};
}
```

In `src/shared/graphics/graphics_device.h`:
- add `#include "shared/graphics/volume_texture.h"` and `#include "shared/graphics/sampler_state.h"` after the `occlusion_query.h` include;
- directly after `virtual OcclusionQueryPtr CreateOcclusionQuery() { return nullptr; }` add:

```cpp

		/// @brief Creates a 3D texture. Backends without volume texture support return nullptr.
		/// @param width Width in texels.
		/// @param height Height in texels.
		/// @param depth Depth in texels.
		/// @param format Texel format.
		/// @param writable Whether compute shaders may write it (creates an unordered access view).
		virtual VolumeTexturePtr CreateVolumeTexture(uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable) { return nullptr; }

		/// @brief Creates a sampler state. Backends without explicit sampler objects return nullptr.
		virtual SamplerStatePtr CreateSamplerState(const SamplerDesc& desc) { return nullptr; }

		/// @brief Runs the active compute shader over the given number of thread groups.
		virtual void Dispatch(uint32 groupsX, uint32 groupsY, uint32 groupsZ) {}

		/// @brief Unbinds every compute shader resource, unordered access view and the compute shader
		///        itself. Call after each compute step whose output is read next, because a resource
		///        cannot be bound for reading while it is still bound for writing.
		virtual void ClearComputeBindings() {}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests; bin/Debug/scene_graph_tests.exe "[graphics_compute]"`
Expected: `All tests passed (2 test cases)`.

- [ ] **Step 5: Implement the D3D11 volume texture**

Create `src/shared/graphics_d3d11/volume_texture_d3d11.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/volume_texture.h"

namespace mmo
{
	/// @brief Direct3D 11 volume texture: an ID3D11Texture3D with a shader resource view and, when
	///        writable, an unordered access view.
	class VolumeTextureD3D11 final : public VolumeTexture
	{
	public:
		VolumeTextureD3D11(GraphicsDeviceD3D11& device, uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable);
		~VolumeTextureD3D11() override = default;

	public:
		void Bind(ShaderType stage, uint32 slot) override;

		void BindWritable(uint32 slot) override;

		void Upload(const uint8* data, size_t size) override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11Texture3D> m_texture;
		ComPtr<ID3D11ShaderResourceView> m_shaderView;
		ComPtr<ID3D11UnorderedAccessView> m_unorderedView;
	};
}
```

Create `src/shared/graphics_d3d11/volume_texture_d3d11.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "volume_texture_d3d11.h"

#include "base/macros.h"

namespace mmo
{
	namespace
	{
		DXGI_FORMAT ToDxgiFormat(const VolumeFormat format)
		{
			switch (format)
			{
			case VolumeFormat::R8:
				return DXGI_FORMAT_R8_UNORM;
			case VolumeFormat::RGBA16F:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			}

			return DXGI_FORMAT_UNKNOWN;
		}

		size_t BytesPerTexel(const VolumeFormat format)
		{
			return format == VolumeFormat::R8 ? 1u : 8u;
		}
	}

	VolumeTextureD3D11::VolumeTextureD3D11(GraphicsDeviceD3D11& device, const uint16 width, const uint16 height, const uint16 depth,
		const VolumeFormat format, const bool writable)
		: VolumeTexture(width, height, depth, format, writable)
		, m_device(device)
	{
		ID3D11Device& d3dDevice = m_device;

		D3D11_TEXTURE3D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.Depth = depth;
		desc.MipLevels = 1;
		desc.Format = ToDxgiFormat(format);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (writable ? D3D11_BIND_UNORDERED_ACCESS : 0u);
		VERIFY(SUCCEEDED(d3dDevice.CreateTexture3D(&desc, nullptr, &m_texture)));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D.MipLevels = 1;
		VERIFY(SUCCEEDED(d3dDevice.CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_shaderView)));

		if (writable)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			uavDesc.Texture3D.MipSlice = 0;
			uavDesc.Texture3D.FirstWSlice = 0;
			uavDesc.Texture3D.WSize = depth;
			VERIFY(SUCCEEDED(d3dDevice.CreateUnorderedAccessView(m_texture.Get(), &uavDesc, &m_unorderedView)));
		}
	}

	void VolumeTextureD3D11::Bind(const ShaderType stage, const uint32 slot)
	{
		ID3D11DeviceContext& context = m_device;
		ID3D11ShaderResourceView* const views = m_shaderView.Get();

		switch (stage)
		{
		case ShaderType::VertexShader:
			context.VSSetShaderResources(slot, 1, &views);
			break;
		case ShaderType::PixelShader:
			context.PSSetShaderResources(slot, 1, &views);
			break;
		case ShaderType::ComputeShader:
			context.CSSetShaderResources(slot, 1, &views);
			break;
		default:
			ASSERT(!"Unsupported shader stage for volume texture binding");
			break;
		}
	}

	void VolumeTextureD3D11::BindWritable(const uint32 slot)
	{
		ASSERT(m_writable);
		if (!m_unorderedView)
		{
			return;
		}

		ID3D11DeviceContext& context = m_device;
		ID3D11UnorderedAccessView* const views = m_unorderedView.Get();
		context.CSSetUnorderedAccessViews(slot, 1, &views, nullptr);
	}

	void VolumeTextureD3D11::Upload(const uint8* data, const size_t size)
	{
		ASSERT(data);
		ASSERT(size == static_cast<size_t>(m_width) * m_height * m_depth * BytesPerTexel(m_format));

		ID3D11DeviceContext& context = m_device;
		const UINT rowPitch = static_cast<UINT>(m_width * BytesPerTexel(m_format));
		const UINT slicePitch = rowPitch * m_height;
		context.UpdateSubresource(m_texture.Get(), 0, nullptr, data, rowPitch, slicePitch);
	}
}
```

- [ ] **Step 6: Implement the D3D11 sampler state and compute shader**

Create `src/shared/graphics_d3d11/sampler_state_d3d11.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_d3d11.h"
#include "graphics/sampler_state.h"

namespace mmo
{
	/// @brief Direct3D 11 sampler state.
	class SamplerStateD3D11 final : public SamplerState
	{
	public:
		SamplerStateD3D11(GraphicsDeviceD3D11& device, const SamplerDesc& desc);
		~SamplerStateD3D11() override = default;

	public:
		void Bind(ShaderType stage, uint32 slot) override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11SamplerState> m_state;
	};
}
```

Create `src/shared/graphics_d3d11/sampler_state_d3d11.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "sampler_state_d3d11.h"

#include "base/macros.h"

namespace mmo
{
	namespace
	{
		D3D11_FILTER toD3dFilter(const SamplerFilter filter)
		{
			switch (filter)
			{
			case SamplerFilter::Point:
				return D3D11_FILTER_MIN_MAG_MIP_POINT;
			case SamplerFilter::Linear:
				return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			case SamplerFilter::Anisotropic:
				return D3D11_FILTER_ANISOTROPIC;
			case SamplerFilter::ComparisonLinear:
				return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			case SamplerFilter::ComparisonAnisotropic:
				return D3D11_FILTER_COMPARISON_ANISOTROPIC;
			}

			return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		}

		D3D11_TEXTURE_ADDRESS_MODE toD3dAddress(const SamplerAddress address)
		{
			switch (address)
			{
			case SamplerAddress::Clamp:
				return D3D11_TEXTURE_ADDRESS_CLAMP;
			case SamplerAddress::Wrap:
				return D3D11_TEXTURE_ADDRESS_WRAP;
			case SamplerAddress::Border:
				return D3D11_TEXTURE_ADDRESS_BORDER;
			}

			return D3D11_TEXTURE_ADDRESS_CLAMP;
		}

		D3D11_COMPARISON_FUNC toD3dComparison(const SamplerComparison comparison)
		{
			switch (comparison)
			{
			case SamplerComparison::LessEqual:
				return D3D11_COMPARISON_LESS_EQUAL;
			case SamplerComparison::Less:
				return D3D11_COMPARISON_LESS;
			case SamplerComparison::Always:
				return D3D11_COMPARISON_ALWAYS;
			}

			return D3D11_COMPARISON_LESS_EQUAL;
		}
	}

	SamplerStateD3D11::SamplerStateD3D11(GraphicsDeviceD3D11& device, const SamplerDesc& desc)
		: m_device(device)
	{
		D3D11_SAMPLER_DESC d3dDesc{};
		d3dDesc.Filter = toD3dFilter(desc.filter);
		d3dDesc.AddressU = toD3dAddress(desc.address);
		d3dDesc.AddressV = d3dDesc.AddressU;
		d3dDesc.AddressW = d3dDesc.AddressU;
		d3dDesc.ComparisonFunc = toD3dComparison(desc.comparison);
		for (int i = 0; i < 4; ++i)
		{
			d3dDesc.BorderColor[i] = desc.borderColor[i];
		}
		d3dDesc.MinLOD = 0.0f;
		d3dDesc.MaxLOD = D3D11_FLOAT32_MAX;
		d3dDesc.MaxAnisotropy = desc.maxAnisotropy < 1u ? 1u : desc.maxAnisotropy;
		d3dDesc.MipLODBias = 0.0f;

		ID3D11Device& d3dDevice = m_device;
		VERIFY(SUCCEEDED(d3dDevice.CreateSamplerState(&d3dDesc, &m_state)));
	}

	void SamplerStateD3D11::Bind(const ShaderType stage, const uint32 slot)
	{
		ID3D11DeviceContext& context = m_device;
		ID3D11SamplerState* const states = m_state.Get();

		switch (stage)
		{
		case ShaderType::VertexShader:
			context.VSSetSamplers(slot, 1, &states);
			break;
		case ShaderType::PixelShader:
			context.PSSetSamplers(slot, 1, &states);
			break;
		case ShaderType::ComputeShader:
			context.CSSetSamplers(slot, 1, &states);
			break;
		default:
			ASSERT(!"Unsupported shader stage for sampler binding");
			break;
		}
	}
}
```

Create `src/shared/graphics_d3d11/compute_shader_d3d11.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/compute_shader.h"
#include "graphics_device_d3d11.h"

namespace mmo
{
	/// @brief Direct3D 11 implementation of a compute shader.
	class ComputeShaderD3D11 final : public ComputeShader
	{
	public:
		ComputeShaderD3D11(GraphicsDeviceD3D11& device, const void* shaderCode, size_t shaderCodeSize);

	public:
		void Set() override;

	private:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11ComputeShader> m_shader;
	};
}
```

Create `src/shared/graphics_d3d11/compute_shader_d3d11.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "compute_shader_d3d11.h"

#include "base/macros.h"

#include <algorithm>
#include <iterator>

namespace mmo
{
	ComputeShaderD3D11::ComputeShaderD3D11(GraphicsDeviceD3D11& device, const void* shaderCode, const size_t shaderCodeSize)
		: m_device(device)
	{
		std::copy(static_cast<const uint8*>(shaderCode), static_cast<const uint8*>(shaderCode) + shaderCodeSize, std::back_inserter(m_byteCode));

		ID3D11Device& d3dDevice = m_device;
		VERIFY(SUCCEEDED(d3dDevice.CreateComputeShader(shaderCode, shaderCodeSize, nullptr, &m_shader)));
	}

	void ComputeShaderD3D11::Set()
	{
		if (m_device.m_currentComputeShader == this)
		{
			return;
		}

		ID3D11DeviceContext& context = m_device;
		context.CSSetShader(m_shader.Get(), nullptr, 0);
		m_device.m_currentComputeShader = this;
	}
}
```

- [ ] **Step 7: Wire the D3D11 device**

In `graphics_device_d3d11.h`:
- Next to the other forward declarations add `class ComputeShaderD3D11;`. Add `friend class ComputeShaderD3D11;` after `friend class PixelShaderD3D11;`.
- After the `CreateOcclusionQuery` override declaration, add:

```cpp
		VolumeTexturePtr CreateVolumeTexture(uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable) override;

		SamplerStatePtr CreateSamplerState(const SamplerDesc& desc) override;

		void Dispatch(uint32 groupsX, uint32 groupsY, uint32 groupsZ) override;

		void ClearComputeBindings() override;
```

- After `ShaderBase* m_currentPixelShader { nullptr };` add:

```cpp

		/// Currently bound compute shader (for caching to avoid redundant CSSetShader calls).
		ShaderBase* m_currentComputeShader { nullptr };
```

In `graphics_device_d3d11.cpp`:
- Add the includes `#include "compute_shader_d3d11.h"`, `#include "sampler_state_d3d11.h"`, `#include "volume_texture_d3d11.h"`.
- In `CreateShader`, add before `default:`:

```cpp
		case ShaderType::ComputeShader:
			return std::make_unique<ComputeShaderD3D11>(*this, shaderCode, shaderCodeSize);
```

- After `CreateOcclusionQuery` add:

```cpp
	VolumeTexturePtr GraphicsDeviceD3D11::CreateVolumeTexture(const uint16 width, const uint16 height, const uint16 depth, const VolumeFormat format, const bool writable)
	{
		return std::make_shared<VolumeTextureD3D11>(*this, width, height, depth, format, writable);
	}

	SamplerStatePtr GraphicsDeviceD3D11::CreateSamplerState(const SamplerDesc& desc)
	{
		return std::make_shared<SamplerStateD3D11>(*this, desc);
	}

	void GraphicsDeviceD3D11::Dispatch(const uint32 groupsX, const uint32 groupsY, const uint32 groupsZ)
	{
		ASSERT_MAIN_THREAD();
		m_immContext->Dispatch(groupsX, groupsY, groupsZ);
	}

	void GraphicsDeviceD3D11::ClearComputeBindings()
	{
		ID3D11ShaderResourceView* nullViews[16] = {};
		ID3D11UnorderedAccessView* nullUavs[8] = {};
		m_immContext->CSSetShaderResources(0, static_cast<UINT>(std::size(nullViews)), nullViews);
		m_immContext->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(nullUavs)), nullUavs, nullptr);
		m_immContext->CSSetShader(nullptr, nullptr, 0);
		m_currentComputeShader = nullptr;
	}
```

If `ASSERT_MAIN_THREAD` is not already available in this file, include `base/thread_checks.h` (the header `structured_buffer_d3d11.cpp` uses).

- [ ] **Step 8: Allow compute-stage binding of 2D textures**

In `texture_d3d11.cpp`, `TextureD3D11::Bind`, add after the `PixelShader` case:

```cpp
		case ShaderType::ComputeShader:
			context.CSSetShaderResources(slot, 1, &views);
			break;
```

In `render_texture_d3d11.cpp`, add the same case before `default:` in **both** `Bind` switches (around lines 63 and 268).

`ConstantBufferD3D11::BindToStage` already handles `ComputeShader`.

- [ ] **Step 9: Build everything that links the graphics libraries**

Run: `cmake -S . -B build; cmake --build build --config Debug -t graphics_d3d11 scene_graph_tests mmo_client mmo_edit`
Expected: build succeeds.

Run: `bin/Debug/scene_graph_tests.exe`
Expected: all tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/shared/graphics/volume_texture.h src/shared/graphics/sampler_state.h src/shared/graphics/compute_shader.h src/shared/graphics/graphics_device.h src/shared/graphics_d3d11/volume_texture_d3d11.h src/shared/graphics_d3d11/volume_texture_d3d11.cpp src/shared/graphics_d3d11/sampler_state_d3d11.h src/shared/graphics_d3d11/sampler_state_d3d11.cpp src/shared/graphics_d3d11/compute_shader_d3d11.h src/shared/graphics_d3d11/compute_shader_d3d11.cpp src/shared/graphics_d3d11/graphics_device_d3d11.h src/shared/graphics_d3d11/graphics_device_d3d11.cpp src/shared/graphics_d3d11/texture_d3d11.cpp src/shared/graphics_d3d11/render_texture_d3d11.cpp src/tests/scene_graph_tests/test_graphics_compute_api.cpp
git commit -m "feat(graphics): volume textures, compute shaders and sampler states

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Fog grid math and the tiling noise volume

**Files:**
- Create: `src/shared/deferred_shading/volumetric_fog_settings.h`
- Create: `src/shared/deferred_shading/fog_noise.h`, `src/shared/deferred_shading/fog_noise.cpp`
- Modify: `src/tests/deferred_shading_tests/CMakeLists.txt`
- Test: `src/tests/deferred_shading_tests/test_volumetric_fog_settings.cpp`, `src/tests/deferred_shading_tests/test_fog_noise.cpp`

**Interfaces:**
- Produces:
  - `struct VolumetricFogSettings`:
    - static constants `MinRange`, `MaxRange`, `NearDistance`, `SkyDistance`, `TemporalBlend`, `HistoryResetDistance`, `NoiseResolution`, `JitterSequenceLength`
    - fields `int32 qualityLevel`, `uint32 tileSize`, `uint32 sliceCount`, `float range`, `uint32 debugMode`
    - methods `ApplyQualityLevel(int)`, `SetRange(float)`, `SetDebugMode(int)`, `IsVolumeEnabled()`, `GetGridWidth(uint32)`, `GetGridHeight(uint32)`
  - `namespace volumetric_fog`:
    - `float SliceToDepth(float slice01, float nearDistance, float farDistance)`
    - `float DepthToSlice(float depth, float nearDistance, float farDistance)`
    - `float HaltonBase2(uint32 index)`
    - `float JitterForFrame(uint64 frame)`
    - `float NoiseDensityFactor(float noise, float amount)`
  - `std::vector<uint8> GenerateFogNoise(uint32 size, uint32 seed)`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/deferred_shading_tests/test_volumetric_fog_settings.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/volumetric_fog_settings.h"

#include <set>

using namespace mmo;

TEST_CASE("Volumetric fog quality presets set tile size and slice count", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.qualityLevel == 3);
	CHECK(settings.tileSize == 12u);
	CHECK(settings.sliceCount == 64u);
	CHECK(settings.IsVolumeEnabled());

	settings.ApplyQualityLevel(0);
	CHECK_FALSE(settings.IsVolumeEnabled());

	settings.ApplyQualityLevel(1);
	CHECK(settings.tileSize == 24u);
	CHECK(settings.sliceCount == 32u);
	CHECK(settings.GetGridWidth(1920) == 80u);
	CHECK(settings.GetGridHeight(1080) == 45u);

	settings.ApplyQualityLevel(2);
	CHECK(settings.tileSize == 16u);
	CHECK(settings.sliceCount == 48u);
	CHECK(settings.GetGridWidth(1920) == 120u);
	CHECK(settings.GetGridHeight(1080) == 68u);

	settings.ApplyQualityLevel(3);
	CHECK(settings.GetGridWidth(1920) == 160u);
	CHECK(settings.GetGridHeight(1080) == 90u);

	settings.ApplyQualityLevel(4);
	CHECK(settings.tileSize == 8u);
	CHECK(settings.sliceCount == 96u);
	CHECK(settings.GetGridWidth(1920) == 240u);
	CHECK(settings.GetGridHeight(1080) == 135u);

	settings.ApplyQualityLevel(-5);
	CHECK(settings.qualityLevel == 0);
	settings.ApplyQualityLevel(42);
	CHECK(settings.qualityLevel == 4);
}

TEST_CASE("Volumetric fog grid never collapses to zero cells", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.GetGridWidth(1) == 1u);
	CHECK(settings.GetGridHeight(0) == 1u);
}

TEST_CASE("Volumetric fog range and debug mode clamp", "[volumetric_fog]")
{
	VolumetricFogSettings settings;
	CHECK(settings.range == Approx(200.0f));

	settings.SetRange(10.0f);
	CHECK(settings.range == Approx(VolumetricFogSettings::MinRange));
	settings.SetRange(1000.0f);
	CHECK(settings.range == Approx(VolumetricFogSettings::MaxRange));

	settings.SetDebugMode(-1);
	CHECK(settings.debugMode == 0u);
	settings.SetDebugMode(7);
	CHECK(settings.debugMode == 3u);
}

TEST_CASE("Slice and depth conversions are exact inverses and monotonic", "[volumetric_fog]")
{
	constexpr float nearDistance = VolumetricFogSettings::NearDistance;
	constexpr float farDistance = 200.0f;

	CHECK(volumetric_fog::SliceToDepth(0.0f, nearDistance, farDistance) == Approx(nearDistance));
	CHECK(volumetric_fog::SliceToDepth(1.0f, nearDistance, farDistance) == Approx(farDistance));
	CHECK(volumetric_fog::DepthToSlice(nearDistance * 0.5f, nearDistance, farDistance) == Approx(0.0f));

	float previous = 0.0f;
	for (float depth : { 0.75f, 1.0f, 5.0f, 17.0f, 64.0f, 150.0f, 199.0f })
	{
		const float slice = volumetric_fog::DepthToSlice(depth, nearDistance, farDistance);
		CHECK(slice > previous);
		CHECK(volumetric_fog::SliceToDepth(slice, nearDistance, farDistance) == Approx(depth).epsilon(1e-4));
		previous = slice;
	}

	// Beyond the range the slice coordinate keeps growing; the composite uses that to switch to the
	// closed-form tail.
	CHECK(volumetric_fog::DepthToSlice(400.0f, nearDistance, farDistance) > 1.0f);
}

TEST_CASE("Halton jitter starts at the radical inverse and stays in [0, 1)", "[volumetric_fog]")
{
	CHECK(volumetric_fog::HaltonBase2(1) == Approx(0.5f));
	CHECK(volumetric_fog::HaltonBase2(2) == Approx(0.25f));
	CHECK(volumetric_fog::HaltonBase2(3) == Approx(0.75f));

	std::set<float> values;
	for (uint64 frame = 0; frame < VolumetricFogSettings::JitterSequenceLength; ++frame)
	{
		const float jitter = volumetric_fog::JitterForFrame(frame);
		CHECK(jitter >= 0.0f);
		CHECK(jitter < 1.0f);
		values.insert(jitter);
	}

	CHECK(values.size() == VolumetricFogSettings::JitterSequenceLength);
	CHECK(volumetric_fog::JitterForFrame(VolumetricFogSettings::JitterSequenceLength) == Approx(volumetric_fog::JitterForFrame(0)));
}

TEST_CASE("Noise density factor keeps the average density", "[volumetric_fog]")
{
	CHECK(volumetric_fog::NoiseDensityFactor(0.3f, 0.0f) == Approx(1.0f));
	CHECK(volumetric_fog::NoiseDensityFactor(0.0f, 1.0f) == Approx(0.0f));
	CHECK(volumetric_fog::NoiseDensityFactor(1.0f, 1.0f) == Approx(2.0f));

	for (float amount : { 0.0f, 0.25f, 0.5f, 1.0f })
	{
		constexpr int samples = 1000;
		double sum = 0.0;
		for (int i = 0; i < samples; ++i)
		{
			const float noise = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
			sum += volumetric_fog::NoiseDensityFactor(noise, amount);
		}

		CHECK(sum / samples == Approx(1.0).epsilon(1e-3));
	}
}
```

Create `src/tests/deferred_shading_tests/test_fog_noise.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/fog_noise.h"

#include <cstdlib>

using namespace mmo;

namespace
{
	int voxel(const std::vector<uint8>& noise, const uint32 size, const uint32 x, const uint32 y, const uint32 z)
	{
		return noise[static_cast<size_t>(z) * size * size + static_cast<size_t>(y) * size + x];
	}
}

TEST_CASE("Fog noise has the requested size and is deterministic per seed", "[volumetric_fog]")
{
	const std::vector<uint8> a = GenerateFogNoise(32, 7);
	const std::vector<uint8> b = GenerateFogNoise(32, 7);
	const std::vector<uint8> c = GenerateFogNoise(32, 8);

	REQUIRE(a.size() == 32u * 32u * 32u);
	CHECK(a == b);
	CHECK(a != c);
}

TEST_CASE("Fog noise spans the value range with a centred mean", "[volumetric_fog]")
{
	const std::vector<uint8> noise = GenerateFogNoise(64, 1);

	int minimum = 255;
	int maximum = 0;
	double sum = 0.0;
	for (const uint8 value : noise)
	{
		minimum = std::min<int>(minimum, value);
		maximum = std::max<int>(maximum, value);
		sum += value;
	}

	CHECK(minimum == 0);
	CHECK(maximum == 255);

	const double mean = sum / static_cast<double>(noise.size());
	CHECK(mean >= 110.0);
	CHECK(mean <= 145.0);
}

TEST_CASE("Fog noise tiles seamlessly on every axis", "[volumetric_fog]")
{
	constexpr uint32 size = 64;
	const std::vector<uint8> noise = GenerateFogNoise(size, 3);

	int interiorX = 0;
	int interiorY = 0;
	int interiorZ = 0;
	int wrapX = 0;
	int wrapY = 0;
	int wrapZ = 0;

	for (uint32 a = 0; a < size; ++a)
	{
		for (uint32 b = 0; b < size; ++b)
		{
			for (uint32 i = 0; i + 1 < size; ++i)
			{
				interiorX = std::max(interiorX, std::abs(voxel(noise, size, i + 1, a, b) - voxel(noise, size, i, a, b)));
				interiorY = std::max(interiorY, std::abs(voxel(noise, size, a, i + 1, b) - voxel(noise, size, a, i, b)));
				interiorZ = std::max(interiorZ, std::abs(voxel(noise, size, a, b, i + 1) - voxel(noise, size, a, b, i)));
			}

			wrapX = std::max(wrapX, std::abs(voxel(noise, size, 0, a, b) - voxel(noise, size, size - 1, a, b)));
			wrapY = std::max(wrapY, std::abs(voxel(noise, size, a, 0, b) - voxel(noise, size, a, size - 1, b)));
			wrapZ = std::max(wrapZ, std::abs(voxel(noise, size, a, b, 0) - voxel(noise, size, a, b, size - 1)));
		}
	}

	CHECK(wrapX <= interiorX * 3 / 2);
	CHECK(wrapY <= interiorY * 3 / 2);
	CHECK(wrapZ <= interiorZ * 3 / 2);
}
```

In `src/tests/deferred_shading_tests/CMakeLists.txt`, add the noise source next to `water_volume_system.cpp` inside `target_sources`:

```cmake
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/deferred_shading/fog_noise.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build; cmake --build build --config Debug -t deferred_shading_tests`
Expected: compile error, `deferred_shading/volumetric_fog_settings.h` not found.

- [ ] **Step 3: Write `volumetric_fog_settings.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Quality and debug settings of the froxel volumetric fog pass.
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	struct VolumetricFogSettings
	{
		/// @brief Smallest allowed fog grid range in metres.
		static constexpr float MinRange = 50.0f;

		/// @brief Largest allowed fog grid range: the cascaded shadow map's maximum distance.
		static constexpr float MaxRange = 300.0f;

		/// @brief View depth of the grid's near plane in metres.
		static constexpr float NearDistance = 0.5f;

		/// @brief Distance at which sky pixels (no geometry) are fogged.
		static constexpr float SkyDistance = 2000.0f;

		/// @brief Weight of the reprojected history in the temporal blend.
		static constexpr float TemporalBlend = 0.9f;

		/// @brief A camera jump longer than this within one frame discards the history.
		static constexpr float HistoryResetDistance = 50.0f;

		/// @brief Edge length of the tiling noise volume in texels.
		static constexpr uint32 NoiseResolution = 64;

		/// @brief Number of frames in the depth jitter sequence.
		static constexpr uint32 JitterSequenceLength = 16;

		/// @brief Current preset: 0 Off (analytic fog only), 1 Low, 2 Medium, 3 High, 4 Ultra.
		int32 qualityLevel = 3;

		/// @brief Screen pixels per grid cell along each screen axis. 0 disables the volume.
		uint32 tileSize = 12;

		/// @brief Number of exponential depth slices. 0 disables the volume.
		uint32 sliceCount = 64;

		/// @brief View depth covered by the grid in metres.
		float range = 200.0f;

		/// @brief 0 off, 1 scattered light, 2 transmittance, 3 density.
		uint32 debugMode = 0;

		/// @brief Applies a quality preset. Out-of-range values clamp.
		void ApplyQualityLevel(int level)
		{
			level = std::clamp(level, 0, 4);
			qualityLevel = level;

			switch (level)
			{
			case 0:
				tileSize = 0;
				sliceCount = 0;
				break;
			case 1:
				tileSize = 24;
				sliceCount = 32;
				break;
			case 2:
				tileSize = 16;
				sliceCount = 48;
				break;
			case 3:
				tileSize = 12;
				sliceCount = 64;
				break;
			default:
				tileSize = 8;
				sliceCount = 96;
				break;
			}
		}

		/// @brief Sets the grid range, clamped to [MinRange, MaxRange].
		void SetRange(const float value)
		{
			range = std::clamp(value, MinRange, MaxRange);
		}

		/// @brief Sets the debug view, clamped to [0, 3].
		void SetDebugMode(const int mode)
		{
			debugMode = static_cast<uint32>(std::clamp(mode, 0, 3));
		}

		/// @brief Whether the froxel volume runs at all.
		[[nodiscard]] bool IsVolumeEnabled() const { return tileSize > 0 && sliceCount > 0; }

		/// @brief Grid cells across the screen width. Never 0.
		[[nodiscard]] uint32 GetGridWidth(const uint32 renderWidth) const
		{
			return tileSize == 0 ? 1u : std::max(1u, (renderWidth + tileSize - 1) / tileSize);
		}

		/// @brief Grid cells across the screen height. Never 0.
		[[nodiscard]] uint32 GetGridHeight(const uint32 renderHeight) const
		{
			return tileSize == 0 ? 1u : std::max(1u, (renderHeight + tileSize - 1) / tileSize);
		}
	};

	namespace volumetric_fog
	{
		/// @brief View depth of a normalized slice coordinate (0 = near plane, 1 = far plane), exponential.
		[[nodiscard]] inline float SliceToDepth(const float slice01, const float nearDistance, const float farDistance)
		{
			return nearDistance * std::pow(farDistance / nearDistance, slice01);
		}

		/// @brief Normalized slice coordinate of a view depth. 0 at or before the near plane; grows past 1
		///        beyond the far plane.
		[[nodiscard]] inline float DepthToSlice(const float depth, const float nearDistance, const float farDistance)
		{
			if (depth <= nearDistance)
			{
				return 0.0f;
			}

			return std::log(depth / nearDistance) / std::log(farDistance / nearDistance);
		}

		/// @brief Radical inverse of index in base 2 (Halton sequence). index 1 -> 0.5, 2 -> 0.25, 3 -> 0.75.
		[[nodiscard]] inline float HaltonBase2(uint32 index)
		{
			float result = 0.0f;
			float fraction = 0.5f;
			while (index > 0)
			{
				if ((index & 1u) != 0)
				{
					result += fraction;
				}

				index >>= 1;
				fraction *= 0.5f;
			}

			return result;
		}

		/// @brief Depth jitter in [0, 1) for a frame number, repeating every JitterSequenceLength frames.
		[[nodiscard]] inline float JitterForFrame(const uint64 frame)
		{
			return HaltonBase2(static_cast<uint32>(frame % VolumetricFogSettings::JitterSequenceLength) + 1u);
		}

		/// @brief Multiplier noise applies to the height-fog density. Averages 1 over uniformly distributed
		///        noise, so the fog beyond the grid (which has no noise) matches the grid's average.
		/// @param noise Noise sample in [0, 1].
		/// @param amount 0 smooth fog, 1 fully patchy.
		/// @remark Mirrored by NoiseDensityFactor in VolumetricFogCommon.hlsli.
		[[nodiscard]] inline float NoiseDensityFactor(const float noise, const float amount)
		{
			return std::max(0.0f, 1.0f + amount * (2.0f * noise - 1.0f));
		}
	}
}
```

- [ ] **Step 4: Write the noise generator**

Create `src/shared/deferred_shading/fog_noise.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	/// @brief Generates a tiling 3D fractal value noise volume for the volumetric fog.
	/// @param size Edge length in texels. The volume repeats seamlessly every size texels on each axis.
	/// @param seed Selects the pattern; the same seed always yields the same volume.
	/// @return size^3 texels, x fastest, then y, then z, stretched to span [0, 255].
	/// @remark Dependency-free so the headless deferred_shading_tests target can compile it.
	[[nodiscard]] std::vector<uint8> GenerateFogNoise(uint32 size, uint32 seed);
}
```

Create `src/shared/deferred_shading/fog_noise.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "fog_noise.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr uint32 OctaveCount = 4;
		constexpr uint32 BasePeriod = 4;

		uint32 hashLattice(const uint32 x, const uint32 y, const uint32 z, const uint32 octave, const uint32 seed)
		{
			uint32 h = seed * 0x9E3779B1u + octave * 0x632BE5ABu;
			h ^= x * 0x85EBCA6Bu;
			h = (h << 13) | (h >> 19);
			h ^= y * 0xC2B2AE35u;
			h = (h << 13) | (h >> 19);
			h ^= z * 0x27D4EB2Fu;
			h ^= h >> 16;
			h *= 0x7FEB352Du;
			h ^= h >> 15;
			h *= 0x846CA68Bu;
			h ^= h >> 16;
			return h;
		}

		float latticeValue(const uint32 x, const uint32 y, const uint32 z, const uint32 octave, const uint32 seed)
		{
			return static_cast<float>(hashLattice(x, y, z, octave, seed) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
		}

		float fade(const float t)
		{
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		/// Value noise on a lattice that wraps every `period` cells, sampled at lattice coordinates.
		float periodicValueNoise(const float u, const float v, const float w, const uint32 period, const uint32 octave, const uint32 seed)
		{
			const float fu = std::floor(u);
			const float fv = std::floor(v);
			const float fw = std::floor(w);

			const uint32 x0 = static_cast<uint32>(fu) % period;
			const uint32 y0 = static_cast<uint32>(fv) % period;
			const uint32 z0 = static_cast<uint32>(fw) % period;
			const uint32 x1 = (x0 + 1) % period;
			const uint32 y1 = (y0 + 1) % period;
			const uint32 z1 = (z0 + 1) % period;

			const float tx = fade(u - fu);
			const float ty = fade(v - fv);
			const float tz = fade(w - fw);

			const auto lerp = [](const float a, const float b, const float t) { return a + (b - a) * t; };

			const float c00 = lerp(latticeValue(x0, y0, z0, octave, seed), latticeValue(x1, y0, z0, octave, seed), tx);
			const float c10 = lerp(latticeValue(x0, y1, z0, octave, seed), latticeValue(x1, y1, z0, octave, seed), tx);
			const float c01 = lerp(latticeValue(x0, y0, z1, octave, seed), latticeValue(x1, y0, z1, octave, seed), tx);
			const float c11 = lerp(latticeValue(x0, y1, z1, octave, seed), latticeValue(x1, y1, z1, octave, seed), tx);

			return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
		}
	}

	std::vector<uint8> GenerateFogNoise(const uint32 size, const uint32 seed)
	{
		const size_t texelCount = static_cast<size_t>(size) * size * size;
		std::vector<float> values(texelCount, 0.0f);

		float minimum = 1.0f;
		float maximum = 0.0f;

		for (uint32 z = 0; z < size; ++z)
		{
			for (uint32 y = 0; y < size; ++y)
			{
				for (uint32 x = 0; x < size; ++x)
				{
					float sum = 0.0f;
					float amplitude = 1.0f;
					float amplitudeSum = 0.0f;
					uint32 period = BasePeriod;

					for (uint32 octave = 0; octave < OctaveCount; ++octave)
					{
						// Texel coordinate scaled so texel `size` lands exactly on lattice cell `period`,
						// which wraps to 0: the volume tiles.
						const float scale = static_cast<float>(period) / static_cast<float>(size);
						sum += amplitude * periodicValueNoise(static_cast<float>(x) * scale, static_cast<float>(y) * scale, static_cast<float>(z) * scale, period, octave, seed);
						amplitudeSum += amplitude;
						amplitude *= 0.5f;
						period *= 2;
					}

					const float value = sum / amplitudeSum;
					values[static_cast<size_t>(z) * size * size + static_cast<size_t>(y) * size + x] = value;
					minimum = std::min(minimum, value);
					maximum = std::max(maximum, value);
				}
			}
		}

		// Fractal sums crowd around 0.5; stretching to the full range gives the fog visible gaps.
		const float span = std::max(maximum - minimum, 1e-6f);

		std::vector<uint8> texels(texelCount, 0);
		for (size_t i = 0; i < texelCount; ++i)
		{
			const float normalized = (values[i] - minimum) / span;
			texels[i] = static_cast<uint8>(std::lround(std::clamp(normalized, 0.0f, 1.0f) * 255.0f));
		}

		return texels;
	}
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake -S . -B build; cmake --build build --config Debug -t deferred_shading_tests; bin/Debug/deferred_shading_tests.exe "[volumetric_fog]"`
Expected: `All tests passed (9 test cases)`.

If the mean check fails (outside [110, 145]), report it rather than widening the bounds. The spec fixes that range.

Run: `bin/Debug/deferred_shading_tests.exe`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/shared/deferred_shading/volumetric_fog_settings.h src/shared/deferred_shading/fog_noise.h src/shared/deferred_shading/fog_noise.cpp src/tests/deferred_shading_tests/CMakeLists.txt src/tests/deferred_shading_tests/test_volumetric_fog_settings.cpp src/tests/deferred_shading_tests/test_fog_noise.cpp
git commit -m "feat(render): volumetric fog grid math and tiling fog noise

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Wind and noise fields in environment profiles

**Files:**
- Modify: `src/shared/proto_data/environment_profiles.proto`, `src/shared/client_data/environment_profiles.proto` (after `transition_seconds = 19`)
- Modify: `src/shared/scene_graph/environment_profile.h`
- Modify: `src/shared/scene_graph/environment_state.h`, `src/shared/scene_graph/environment_state.cpp`
- Modify: `src/shared/scene_graph/environment_profile_proto.h` (`LoadEnvironmentProfile`, before `return result;`)
- Modify: `src/mmo_edit/editor_windows/environment_profile_editor_window.h` (declaration next to `DrawFixedValues`), `environment_profile_editor_window.cpp` (call after `DrawFixedValues(currentEntry);`, new method)
- Test: `src/tests/scene_graph_tests/test_environment_wind_fields.cpp`

**Interfaces:**
- Consumes: `EnvironmentProfile`, `EnvironmentState`, `EvaluateEnvironment`, `LerpEnvironment`, `LoadEnvironmentProfile`, `EnvironmentPreview::NotifyChanged` (existing).
- Produces:
  - Proto fields `wind_direction = 20`, `wind_speed = 21`, `wind_gustiness = 22`, `fog_noise_amount = 23`, `fog_noise_size = 24` (float).
  - `EnvironmentProfile::windDirectionDegrees`, `windSpeed`, `windGustiness`, `fogNoiseAmount`, `fogNoiseSize`.
  - `EnvironmentState::windDirection` (`Vector3`, unit, y = 0), `windSpeed`, `windGustiness`, `fogNoiseAmount`, `fogNoiseSize`.
  - `float WrapDegrees360(float degrees)` (in `environment_profile.h`).
  - `Vector3 WindDirectionFromDegrees(float degrees)` (in `environment_state.h`).

- [ ] **Step 1: Write the failing tests**

Create `src/tests/scene_graph_tests/test_environment_wind_fields.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Wind proto defaults equal the runtime profile defaults", "[wind]")
{
	const proto_client::EnvironmentProfile record;
	const EnvironmentProfile profile;

	CHECK(record.wind_direction() == Approx(profile.windDirectionDegrees));
	CHECK(record.wind_speed() == Approx(profile.windSpeed));
	CHECK(record.wind_gustiness() == Approx(profile.windGustiness));
	CHECK(record.fog_noise_amount() == Approx(profile.fogNoiseAmount));
	CHECK(record.fog_noise_size() == Approx(profile.fogNoiseSize));

	CHECK(profile.windDirectionDegrees == Approx(45.0f));
	CHECK(profile.windSpeed == Approx(3.0f));
	CHECK(profile.windGustiness == Approx(0.3f));
	CHECK(profile.fogNoiseAmount == Approx(0.5f));
	CHECK(profile.fogNoiseSize == Approx(60.0f));
}

TEST_CASE("Loading wind fields wraps the direction and clamps the rest", "[wind]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(1);
	record.set_name("Storm");
	record.set_wind_direction(-90.0f);
	record.set_wind_speed(99.0f);
	record.set_wind_gustiness(-1.0f);
	record.set_fog_noise_amount(3.0f);
	record.set_fog_noise_size(1.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.windDirectionDegrees == Approx(270.0f));
	CHECK(profile.windSpeed == Approx(30.0f));
	CHECK(profile.windGustiness == Approx(0.0f));
	CHECK(profile.fogNoiseAmount == Approx(1.0f));
	CHECK(profile.fogNoiseSize == Approx(5.0f));

	record.set_wind_direction(725.0f);
	record.set_fog_noise_size(9000.0f);
	const EnvironmentProfile wrapped = LoadEnvironmentProfile(record);
	CHECK(wrapped.windDirectionDegrees == Approx(5.0f));
	CHECK(wrapped.fogNoiseSize == Approx(500.0f));
}

TEST_CASE("Wind direction degrees map clockwise from +Z", "[wind]")
{
	const Vector3 north = WindDirectionFromDegrees(0.0f);
	CHECK(north.x == Approx(0.0f).margin(1e-5));
	CHECK(north.z == Approx(1.0f));

	const Vector3 east = WindDirectionFromDegrees(90.0f);
	CHECK(east.x == Approx(1.0f));
	CHECK(east.z == Approx(0.0f).margin(1e-5));
	CHECK(east.y == Approx(0.0f));
}

TEST_CASE("Evaluating a profile copies the wind values", "[wind]")
{
	EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
	profile.windDirectionDegrees = 180.0f;
	profile.windSpeed = 7.0f;
	profile.windGustiness = 0.8f;
	profile.fogNoiseAmount = 0.2f;
	profile.fogNoiseSize = 120.0f;

	const EnvironmentState state = EvaluateEnvironment(profile, 0.5f);
	CHECK(state.windDirection.z == Approx(-1.0f));
	CHECK(state.windSpeed == Approx(7.0f));
	CHECK(state.windGustiness == Approx(0.8f));
	CHECK(state.fogNoiseAmount == Approx(0.2f));
	CHECK(state.fogNoiseSize == Approx(120.0f));
}

TEST_CASE("Wind direction blends along the shortest arc", "[wind]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.windDirection = WindDirectionFromDegrees(350.0f);
	b.windDirection = WindDirectionFromDegrees(10.0f);
	a.windSpeed = 2.0f;
	b.windSpeed = 6.0f;

	const EnvironmentState half = LerpEnvironment(a, b, 0.5f);
	CHECK(half.windDirection.z == Approx(1.0f));
	CHECK(half.windDirection.x == Approx(0.0f).margin(1e-4));
	CHECK(half.windSpeed == Approx(4.0f));

	const float length = std::sqrt(half.windDirection.x * half.windDirection.x + half.windDirection.z * half.windDirection.z);
	CHECK(length == Approx(1.0f));
}

TEST_CASE("Opposite winds fall back to the incoming direction", "[wind]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.windDirection = WindDirectionFromDegrees(0.0f);
	b.windDirection = WindDirectionFromDegrees(180.0f);

	const EnvironmentState half = LerpEnvironment(a, b, 0.5f);
	CHECK(half.windDirection.z == Approx(-1.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests`
Expected: compile errors (`wind_direction` is not a member of `proto_client::EnvironmentProfile`).

- [ ] **Step 3: Add the proto fields**

In **both** `environment_profiles.proto` files, replace the line `// Fields 20 and up are reserved for wind-driven volumetric fog.` with:

```proto
	// Wind and fog noise (volumetric fog).
	// Direction the wind blows toward, in degrees clockwise from north (+Z).
	optional float wind_direction = 20 [default = 45];
	// Wind speed in metres per second.
	optional float wind_speed = 21 [default = 3];
	// How strongly gusts vary speed and direction, 0..1.
	optional float wind_gustiness = 22 [default = 0.3];
	// 0 = smooth fog, 1 = very patchy fog.
	optional float fog_noise_amount = 23 [default = 0.5];
	// Metres per repeat of the noise pattern.
	optional float fog_noise_size = 24 [default = 60];
```

- [ ] **Step 4: Runtime profile and state fields**

In `src/shared/scene_graph/environment_profile.h`, add `#include <cmath>` to the includes. After `MakeEnvironmentCurve`'s declaration add:

```cpp

	/// @brief Wraps an angle in degrees into [0, 360).
	[[nodiscard]] inline float WrapDegrees360(const float degrees)
	{
		float wrapped = std::fmod(degrees, 360.0f);
		if (wrapped < 0.0f)
		{
			wrapped += 360.0f;
		}

		return wrapped >= 360.0f ? 0.0f : wrapped;
	}
```

Add to `struct EnvironmentProfile`, after `transitionSeconds`:

```cpp

		/// @brief Direction the wind blows toward, degrees clockwise from +Z, [0, 360).
		float windDirectionDegrees = 45.0f;

		/// @brief Wind speed in m/s, [0, 30].
		float windSpeed = 3.0f;

		/// @brief Gust strength, [0, 1].
		float windGustiness = 0.3f;

		/// @brief Fog patchiness: 0 smooth, 1 very patchy.
		float fogNoiseAmount = 0.5f;

		/// @brief Metres per repeat of the fog noise, [5, 500].
		float fogNoiseSize = 60.0f;
```

In `src/shared/scene_graph/environment_state.h`, add to `struct EnvironmentState` after `bloomThreshold`:

```cpp

		/// @brief Unit xz direction the wind blows toward (y = 0).
		Vector3 windDirection{ 0.70710678f, 0.0f, 0.70710678f };

		/// @brief Wind speed in m/s before gusts.
		float windSpeed = 3.0f;

		/// @brief Gust strength, [0, 1].
		float windGustiness = 0.3f;

		/// @brief Fog patchiness: 0 smooth, 1 very patchy.
		float fogNoiseAmount = 0.5f;

		/// @brief Metres per repeat of the fog noise.
		float fogNoiseSize = 60.0f;
```

and after the `LerpEnvironment` declaration:

```cpp

	/// @brief Unit xz vector for a wind direction in degrees clockwise from +Z (0 = +Z, 90 = +X).
	[[nodiscard]] Vector3 WindDirectionFromDegrees(float degrees);
```

In `src/shared/scene_graph/environment_state.cpp`, add `#include <cmath>` and, after the anonymous namespace, add:

```cpp
	Vector3 WindDirectionFromDegrees(const float degrees)
	{
		constexpr float degreesToRadians = 3.14159265358979f / 180.0f;
		const float radians = degrees * degreesToRadians;
		return Vector3(std::sin(radians), 0.0f, std::cos(radians));
	}
```

In `EvaluateEnvironment`, before `return state;`:

```cpp

		state.windDirection = WindDirectionFromDegrees(profile.windDirectionDegrees);
		state.windSpeed = profile.windSpeed;
		state.windGustiness = profile.windGustiness;
		state.fogNoiseAmount = profile.fogNoiseAmount;
		state.fogNoiseSize = profile.fogNoiseSize;
```

In `LerpEnvironment`, before `return state;`:

```cpp

		// Blend the direction as a vector so 350 -> 10 degrees passes through 0, not 180. Opposite
		// winds cancel out; the incoming direction wins then.
		const Vector3 blendedWind = a.windDirection * (1.0f - t) + b.windDirection * t;
		const float windLength = blendedWind.GetLength();
		state.windDirection = windLength < 1e-3f ? b.windDirection : blendedWind * (1.0f / windLength);
		state.windSpeed = lerpFloat(a.windSpeed, b.windSpeed, t);
		state.windGustiness = lerpFloat(a.windGustiness, b.windGustiness, t);
		state.fogNoiseAmount = lerpFloat(a.fogNoiseAmount, b.fogNoiseAmount, t);
		state.fogNoiseSize = lerpFloat(a.fogNoiseSize, b.fogNoiseSize, t);
```

In `src/shared/scene_graph/environment_profile_proto.h`, `LoadEnvironmentProfile`, before `return result;`:

```cpp

		result.windDirectionDegrees = WrapDegrees360(profile.wind_direction());
		result.windSpeed = std::clamp(profile.wind_speed(), 0.0f, 30.0f);
		result.windGustiness = std::clamp(profile.wind_gustiness(), 0.0f, 1.0f);
		result.fogNoiseAmount = std::clamp(profile.fog_noise_amount(), 0.0f, 1.0f);
		result.fogNoiseSize = std::clamp(profile.fog_noise_size(), 5.0f, 500.0f);
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug -t scene_graph_tests client_data_tests; bin/Debug/scene_graph_tests.exe "[wind]"`
Expected: `All tests passed (6 test cases)`.

Run: `bin/Debug/scene_graph_tests.exe; bin/Debug/client_data_tests.exe`
Expected: all tests pass.

- [ ] **Step 6: Editor Wind & Noise section**

In `environment_profile_editor_window.h`, after `void DrawFixedValues(proto::EnvironmentProfile& entry);` add:

```cpp

		void DrawWind(proto::EnvironmentProfile& entry);
```

In `environment_profile_editor_window.cpp`, add the call directly after `DrawFixedValues(currentEntry);`:

```cpp
		DrawWind(currentEntry);
```

and add the method after `DrawFixedValues`:

```cpp
	void EnvironmentProfileEditorWindow::DrawWind(proto::EnvironmentProfile& entry)
	{
		if (const auto section = ScopedEditorSection("Wind & Noise", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;

			float direction = entry.wind_direction();
			if (ImGui::SliderFloat("Wind Direction", &direction, 0.0f, 360.0f, "%.0f deg"))
			{
				entry.set_wind_direction(std::clamp(direction, 0.0f, 360.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Direction the wind blows toward, clockwise from north (+Z). 90 = toward +X.");
			}

			float speed = entry.wind_speed();
			if (ImGui::DragFloat("Wind Speed", &speed, 0.05f, 0.0f, 30.0f, "%.1f m/s"))
			{
				entry.set_wind_speed(std::clamp(speed, 0.0f, 30.0f));
				changed = true;
			}

			float gustiness = entry.wind_gustiness();
			if (ImGui::SliderFloat("Gustiness", &gustiness, 0.0f, 1.0f, "%.2f"))
			{
				entry.set_wind_gustiness(gustiness);
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How much gusts vary the wind speed (up to +/-60%%) and direction (up to +/-25 degrees).");
			}

			float noiseAmount = entry.fog_noise_amount();
			if (ImGui::SliderFloat("Fog Noise Amount", &noiseAmount, 0.0f, 1.0f, "%.2f"))
			{
				entry.set_fog_noise_amount(noiseAmount);
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("0 = smooth fog, 1 = drifting banks with clear gaps. Average fog amount stays the same.");
			}

			float noiseSize = entry.fog_noise_size();
			if (ImGui::DragFloat("Fog Noise Size", &noiseSize, 0.5f, 5.0f, 500.0f, "%.0f m"))
			{
				entry.set_fog_noise_size(std::clamp(noiseSize, 5.0f, 500.0f));
				changed = true;
			}

			if (changed)
			{
				GetEnvironmentPreview().NotifyChanged();
			}
		}
	}
```

- [ ] **Step 7: Build the client and editor**

Run: `cmake --build build --config Debug -t mmo_client mmo_edit`
Expected: build succeeds.

- [ ] **Step 8: Commit**

```bash
git add src/shared/proto_data/environment_profiles.proto src/shared/client_data/environment_profiles.proto src/shared/scene_graph/environment_profile.h src/shared/scene_graph/environment_state.h src/shared/scene_graph/environment_state.cpp src/shared/scene_graph/environment_profile_proto.h src/mmo_edit/editor_windows/environment_profile_editor_window.h src/mmo_edit/editor_windows/environment_profile_editor_window.cpp src/tests/scene_graph_tests/test_environment_wind_fields.cpp
git commit -m "feat(render): wind and fog noise settings in environment profiles

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Wind simulation, scene wind and host wiring

**Files:**
- Create: `src/shared/scene_graph/wind_state.h`
- Create: `src/shared/scene_graph/wind_simulation.h`, `src/shared/scene_graph/wind_simulation.cpp`
- Modify: `src/shared/scene_graph/scene.h` (include; accessors after `GetAtmosphereReferenceHeight` ~383; member after `m_atmosphereTimeOfDay` ~761)
- Modify: `src/shared/graphics/global_shader_parameters.cpp` (`EnsureEngineDefaults`, after `SunColor`)
- Modify: `src/mmo_client/game_states/world_state.h` (include; member after `EnvironmentController m_environment;`), `world_state.cpp` (`OnIdle`, after `ApplyEnvironmentToRenderer();` ~1014)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h` (member after `EnvironmentController m_environment;`), `world_editor_instance.cpp` (`UpdateEnvironment`, after `m_skyComponent->ApplyEnvironment(m_environment.GetState());` ~1387)
- Test: `src/tests/scene_graph_tests/test_wind_simulation.cpp`

**Interfaces:**
- Consumes: `EnvironmentState::windDirection/windSpeed/windGustiness/fogNoiseAmount/fogNoiseSize` (Task 3).
- Produces:
  - `struct WindState { Vector3 direction; float speed; float noiseOffsetX; float noiseOffsetZ; float noiseSize; float noiseAmount; };`
  - `float WindGustNoise(double time)`, returning a smooth value in [-1, 1].
  - `class WindSimulation` with `Update(float deltaSeconds, const EnvironmentState&)`, `const WindState& GetState() const`, `Reset()`, and constants `MaxGustSpeedFactor = 0.6f`, `MaxGustAngleDegrees = 25.0f`.
  - `void PublishWindShaderParameters(const WindState&)`.
  - `void Scene::SetWind(const WindState&)`, `const WindState& Scene::GetWind() const`.
  - Global shader parameter `WindDirection`: xyz direction, w speed.

- [ ] **Step 1: Write the failing tests**

Create `src/tests/scene_graph_tests/test_wind_simulation.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_state.h"
#include "scene_graph/wind_simulation.h"

#include <cmath>

using namespace mmo;

namespace
{
	EnvironmentState makeWind(const float degrees, const float speed, const float gustiness)
	{
		EnvironmentState state;
		state.windDirection = WindDirectionFromDegrees(degrees);
		state.windSpeed = speed;
		state.windGustiness = gustiness;
		state.fogNoiseSize = 40.0f;
		state.fogNoiseAmount = 0.7f;
		return state;
	}

	/// Shortest signed difference of two offsets wrapped to [0, 1).
	float wrappedDelta(const float from, const float to)
	{
		float delta = to - from;
		delta -= std::round(delta);
		return delta;
	}
}

TEST_CASE("Gust noise is smooth and bounded", "[wind]")
{
	float previous = WindGustNoise(0.0);
	for (int i = 1; i < 2000; ++i)
	{
		const double time = static_cast<double>(i) * 0.01;
		const float value = WindGustNoise(time);
		CHECK(value >= -1.0f);
		CHECK(value <= 1.0f);
		CHECK(std::abs(value - previous) < 0.1f);
		previous = value;
	}
}

TEST_CASE("Zero wind speed leaves the noise offset still", "[wind]")
{
	WindSimulation simulation;
	const EnvironmentState calm = makeWind(90.0f, 0.0f, 1.0f);

	simulation.Update(0.1f, calm);
	const float x = simulation.GetState().noiseOffsetX;
	const float z = simulation.GetState().noiseOffsetZ;

	for (int i = 0; i < 100; ++i)
	{
		simulation.Update(0.1f, calm);
	}

	CHECK(simulation.GetState().noiseOffsetX == Approx(x));
	CHECK(simulation.GetState().noiseOffsetZ == Approx(z));
	CHECK(simulation.GetState().speed == Approx(0.0f));
}

TEST_CASE("Gusts keep speed and direction within their bounds", "[wind]")
{
	WindSimulation simulation;
	const EnvironmentState gusty = makeWind(0.0f, 10.0f, 0.5f);

	for (int i = 0; i < 3000; ++i)
	{
		simulation.Update(0.05f, gusty);
		const WindState& state = simulation.GetState();

		CHECK(state.speed >= 10.0f * (1.0f - WindSimulation::MaxGustSpeedFactor * 0.5f) - 1e-3f);
		CHECK(state.speed <= 10.0f * (1.0f + WindSimulation::MaxGustSpeedFactor * 0.5f) + 1e-3f);

		const float angleDegrees = std::acos(std::clamp(state.direction.z, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
		CHECK(angleDegrees <= WindSimulation::MaxGustAngleDegrees * 0.5f + 0.01f);
		CHECK(state.direction.y == Approx(0.0f));
	}
}

TEST_CASE("Noise offset moves continuously across direction and speed changes", "[wind]")
{
	WindSimulation simulation;
	constexpr float dt = 1.0f / 60.0f;

	float previousX = simulation.GetState().noiseOffsetX;
	float previousZ = simulation.GetState().noiseOffsetZ;

	for (int i = 0; i < 1200; ++i)
	{
		// Switch direction and speed abruptly every 200 frames.
		const int phase = i / 200;
		const EnvironmentState wind = makeWind(static_cast<float>(phase) * 73.0f, 2.0f + static_cast<float>(phase) * 3.0f, 0.3f);
		simulation.Update(dt, wind);

		const WindState& state = simulation.GetState();
		const float maxStepMetres = wind.windSpeed * (1.0f + WindSimulation::MaxGustSpeedFactor) * dt + 1e-3f;
		CHECK(std::abs(wrappedDelta(previousX, state.noiseOffsetX)) * state.noiseSize <= maxStepMetres);
		CHECK(std::abs(wrappedDelta(previousZ, state.noiseOffsetZ)) * state.noiseSize <= maxStepMetres);

		CHECK(state.noiseOffsetX >= 0.0f);
		CHECK(state.noiseOffsetX < 1.0f);
		CHECK(state.noiseOffsetZ >= 0.0f);
		CHECK(state.noiseOffsetZ < 1.0f);

		previousX = state.noiseOffsetX;
		previousZ = state.noiseOffsetZ;
	}
}

TEST_CASE("Wind moves the noise offset toward the wind direction", "[wind]")
{
	WindSimulation simulation;
	const EnvironmentState eastward = makeWind(90.0f, 4.0f, 0.0f);

	float previousX = simulation.GetState().noiseOffsetX;
	simulation.Update(1.0f, eastward);

	// 4 m/s for one second with a 40 m noise size advances x by 0.1.
	CHECK(wrappedDelta(previousX, simulation.GetState().noiseOffsetX) == Approx(0.1f).margin(1e-4));
	CHECK(simulation.GetState().noiseSize == Approx(40.0f));
	CHECK(simulation.GetState().noiseAmount == Approx(0.7f));
}

TEST_CASE("Reset clears the accumulated offset", "[wind]")
{
	WindSimulation simulation;
	simulation.Update(3.0f, makeWind(45.0f, 5.0f, 0.0f));
	simulation.Reset();

	CHECK(simulation.GetState().noiseOffsetX == Approx(0.0f));
	CHECK(simulation.GetState().noiseOffsetZ == Approx(0.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `scene_graph/wind_simulation.h` not found.

- [ ] **Step 3: Write the wind state and simulation**

Create `src/shared/scene_graph/wind_state.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"

namespace mmo
{
	/// @brief The wind of the current frame, as consumed by the renderer.
	struct WindState
	{
		/// @brief Unit direction the wind blows toward, including gust wobble (y = 0).
		Vector3 direction{ 0.70710678f, 0.0f, 0.70710678f };

		/// @brief Current speed in m/s, including gusts.
		float speed = 0.0f;

		/// @brief Accumulated noise scroll along x, in noise tiles, wrapped to [0, 1).
		float noiseOffsetX = 0.0f;

		/// @brief Accumulated noise scroll along z, in noise tiles, wrapped to [0, 1).
		float noiseOffsetZ = 0.0f;

		/// @brief Metres per repeat of the fog noise.
		float noiseSize = 60.0f;

		/// @brief Fog patchiness: 0 smooth, 1 very patchy.
		float noiseAmount = 0.5f;
	};
}
```

Create `src/shared/scene_graph/wind_simulation.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "scene_graph/environment_state.h"
#include "scene_graph/wind_state.h"

namespace mmo
{
	/// @brief Smooth 1D value noise over time, in [-1, 1]. Varies on a time scale of a few seconds.
	/// @param time Accumulated seconds. Never pass an absolute clock value.
	[[nodiscard]] float WindGustNoise(double time);

	/// @brief Turns a zone's wind settings into gusting wind and a scrolling fog noise offset.
	/// @remark Pure logic: no graphics. Main thread only. Time and offsets accumulate in doubles and only
	///         wrapped values reach float math, so long sessions keep full precision.
	class WindSimulation final
	{
	public:
		/// @brief Largest relative speed change from gusts at gustiness 1.
		static constexpr float MaxGustSpeedFactor = 0.6f;

		/// @brief Largest direction wobble from gusts at gustiness 1, in degrees.
		static constexpr float MaxGustAngleDegrees = 25.0f;

	public:
		/// @brief Advances the wind by one frame.
		/// @param deltaSeconds Real time since the last update.
		/// @param environment The blended environment state of this frame.
		void Update(float deltaSeconds, const EnvironmentState& environment);

		/// @brief The wind of the last update.
		[[nodiscard]] const WindState& GetState() const { return m_state; }

		/// @brief Clears accumulated time and offsets.
		void Reset();

	private:
		double m_time = 0.0;
		double m_offsetX = 0.0;
		double m_offsetZ = 0.0;
		WindState m_state;
	};

	/// @brief Publishes the WindDirection global shader parameter (xyz direction, w speed).
	void PublishWindShaderParameters(const WindState& state);
}
```

Create `src/shared/scene_graph/wind_simulation.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "wind_simulation.h"

#include "graphics/global_shader_parameters.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr double gustFrequency = 0.35;
		constexpr double directionGustTimeOffset = 17.3;
		constexpr float degreesToRadians = 3.14159265358979f / 180.0f;

		float hashToSigned(const int64 cell)
		{
			uint32 h = static_cast<uint32>(cell) * 0x9E3779B1u ^ static_cast<uint32>(static_cast<uint64>(cell) >> 32) * 0x85EBCA6Bu;
			h ^= h >> 16;
			h *= 0x7FEB352Du;
			h ^= h >> 15;
			h *= 0x846CA68Bu;
			h ^= h >> 16;
			return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu) * 2.0f - 1.0f;
		}

		float wrap01(const double value)
		{
			const float wrapped = static_cast<float>(value - std::floor(value));
			return wrapped >= 1.0f ? 0.0f : wrapped;
		}
	}

	float WindGustNoise(const double time)
	{
		const double scaled = time * gustFrequency;
		const double cell = std::floor(scaled);
		const float t = static_cast<float>(scaled - cell);
		const float fade = t * t * (3.0f - 2.0f * t);

		const int64 index = static_cast<int64>(cell);
		const float a = hashToSigned(index);
		const float b = hashToSigned(index + 1);
		return a + (b - a) * fade;
	}

	void WindSimulation::Update(const float deltaSeconds, const EnvironmentState& environment)
	{
		m_time += static_cast<double>(deltaSeconds);

		const float gustiness = std::clamp(environment.windGustiness, 0.0f, 1.0f);
		const float speed = std::max(0.0f, environment.windSpeed * (1.0f + gustiness * MaxGustSpeedFactor * WindGustNoise(m_time)));

		// Positive angles turn from +Z toward +X, matching the clockwise-from-north convention.
		const float angle = MaxGustAngleDegrees * gustiness * WindGustNoise(m_time + directionGustTimeOffset) * degreesToRadians;
		const float cosAngle = std::cos(angle);
		const float sinAngle = std::sin(angle);
		const Vector3& base = environment.windDirection;
		const Vector3 direction(base.x * cosAngle + base.z * sinAngle, 0.0f, base.z * cosAngle - base.x * sinAngle);

		m_offsetX += static_cast<double>(direction.x) * speed * deltaSeconds;
		m_offsetZ += static_cast<double>(direction.z) * speed * deltaSeconds;

		const float noiseSize = std::max(environment.fogNoiseSize, 5.0f);

		m_state.direction = direction;
		m_state.speed = speed;
		m_state.noiseSize = noiseSize;
		m_state.noiseAmount = std::clamp(environment.fogNoiseAmount, 0.0f, 1.0f);
		m_state.noiseOffsetX = wrap01(m_offsetX / noiseSize);
		m_state.noiseOffsetZ = wrap01(m_offsetZ / noiseSize);
	}

	void WindSimulation::Reset()
	{
		m_time = 0.0;
		m_offsetX = 0.0;
		m_offsetZ = 0.0;
		m_state.noiseOffsetX = 0.0f;
		m_state.noiseOffsetZ = 0.0f;
	}

	void PublishWindShaderParameters(const WindState& state)
	{
		GlobalShaderParameters::Get().SetVector("WindDirection", Vector4(state.direction.x, state.direction.y, state.direction.z, state.speed));
	}
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests; bin/Debug/scene_graph_tests.exe "[wind]"`
Expected: `All tests passed (12 test cases)` (6 from Task 3, 6 here).

- [ ] **Step 5: Scene accessors and the engine default parameter**

In `src/shared/scene_graph/scene.h`, add `#include "scene_graph/wind_state.h"` next to the atmosphere settings include. After `GetAtmosphereReferenceHeight()` add:

```cpp

		/// @brief Sets the wind of this frame. Written by the host's WindSimulation every update.
		void SetWind(const WindState& wind) { m_wind = wind; }

		/// @brief Gets the wind of this frame (read by the volumetric fog pass).
		[[nodiscard]] const WindState& GetWind() const { return m_wind; }
```

After `AtmosphereTimeOfDay m_atmosphereTimeOfDay;` add:

```cpp

		/// @brief Wind of the current frame; see SetWind.
		WindState m_wind;
```

In `src/shared/graphics/global_shader_parameters.cpp`, `EnsureEngineDefaults`, after `DefineVector("SunColor", …);` add:

```cpp

		// Published by the host's WindSimulation every frame: xyz is the unit direction the wind blows
		// toward (y = 0), w the current speed in m/s including gusts.
		DefineVector("WindDirection", Vector4(0.70710678f, 0.0f, 0.70710678f, 0.0f));
```

- [ ] **Step 6: Wire the client and editor hosts**

In `src/mmo_client/game_states/world_state.h`, add `#include "scene_graph/wind_simulation.h"` after the environment controller include, and after `EnvironmentController m_environment;`:

```cpp

		/// Gusting wind and fog noise scroll driven by the environment's wind settings.
		WindSimulation m_wind;
```

In `world_state.cpp`, `OnIdle`, directly after `ApplyEnvironmentToRenderer();`:

```cpp

		m_wind.Update(deltaSeconds, m_environment.GetState());
		if (m_scene)
		{
			m_scene->SetWind(m_wind.GetState());
		}
		PublishWindShaderParameters(m_wind.GetState());
```

In `src/mmo_edit/editors/world_editor/world_editor_instance.h`, add `#include "scene_graph/wind_simulation.h"`, and after `EnvironmentController m_environment;`:

```cpp

		/// Gusting wind and fog noise scroll for the viewport. Advances with real frame time even while
		/// the sky clock is paused, so drifting fog stays visible while authoring.
		WindSimulation m_wind;
```

In `world_editor_instance.cpp`, `UpdateEnvironment`, directly after `m_skyComponent->ApplyEnvironment(m_environment.GetState());`:

```cpp

		m_wind.Update(deltaSeconds, m_environment.GetState());
		m_scene.SetWind(m_wind.GetState());
		PublishWindShaderParameters(m_wind.GetState());
```

- [ ] **Step 7: Build and run everything affected**

Run: `cmake --build build --config Debug -t scene_graph_tests mmo_client mmo_edit; bin/Debug/scene_graph_tests.exe`
Expected: builds succeed and all tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/shared/scene_graph/wind_state.h src/shared/scene_graph/wind_simulation.h src/shared/scene_graph/wind_simulation.cpp src/shared/scene_graph/scene.h src/shared/graphics/global_shader_parameters.cpp src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_editor_instance.h src/mmo_edit/editors/world_editor/world_editor_instance.cpp src/tests/scene_graph_tests/test_wind_simulation.cpp
git commit -m "feat(render): gusting wind simulation published to the scene and shaders

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Volumetric fog shaders and pass

**Files:**
- Create: `src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli`
- Create: `src/shared/deferred_shading/shaders/CS_FogInject.hlsl`, `CS_FogTemporal.hlsl`, `CS_FogIntegrate.hlsl`, `PS_FogComposite.hlsl`
- Create: `src/shared/deferred_shading/volumetric_fog_pass.h`, `src/shared/deferred_shading/volumetric_fog_pass.cpp`
- Modify: `src/shared/deferred_shading/CMakeLists.txt`
- Modify: `.gitignore` (after line 71)

**Interfaces:**
- Consumes:
  - Task 1: `VolumeTexture`, `VolumeFormat`, `SamplerState`, `SamplerDesc`, `GraphicsDevice::CreateVolumeTexture`, `CreateSamplerState`, `Dispatch`, `ClearComputeBindings`, `CreateShader(ShaderType::ComputeShader, …)`, and compute-stage `Bind`/`BindToStage`.
  - Task 2: `VolumetricFogSettings`, `volumetric_fog::JitterForFrame`, `GenerateFogNoise`.
  - Task 4: `WindState`.
  - Existing: `AtmosphereCommon.hlsli` (`CameraBuffer` b1, `FogDensityAt`, `FogOpticalDepth`, `FogSource`), `ShadowBuffer` b3 layout, `NUM_SHADOW_CASCADES`, `Camera::GetViewMatrix/GetProjectionMatrix/GetDerivedPosition/GetDerivedDirection`.
- Produces: `class VolumetricFogPass` with:
  - `VolumetricFogPass(GraphicsDevice&, uint32 width, uint32 height)`
  - `void Resize(uint32, uint32)`
  - `void Render(Camera&, const WindState&, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output, const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>&, ConstantBuffer& shadowBuffer, ConstantBuffer& cameraBuffer, SamplerState& shadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs)`
  - `void InvalidateHistory()`
  - `VolumetricFogSettings& GetSettings()`

This task compiles the pass and its shaders. `DeferredRenderer` starts using it in Task 6. GPU behaviour is checked in game in Task 7, and the pure math is already unit-tested in Task 2.

- [ ] **Step 1: Build rules for compute shaders**

In `src/shared/deferred_shading/CMakeLists.txt`:
- After `file(GLOB d3d11_ps_shaders "shaders/PS_*.hlsl")` add `file(GLOB d3d11_cs_shaders "shaders/CS_*.hlsl")`.
- Add `${d3d11_cs_shaders}` to the `target_sources` line.
- Add `source_group(src\\shaders FILES ${d3d11_cs_shaders})`.
- After the pixel shader `set_source_files_properties` add:

```cmake
	set_source_files_properties( ${d3d11_cs_shaders} PROPERTIES VS_SHADER_TYPE Compute VS_SHADER_MODEL 5.0 VS_SHADER_ENTRYPOINT main
		VS_SHADER_OUTPUT_HEADER_FILE "${CMAKE_CURRENT_LIST_DIR}/shaders/%(Filename).h" VS_SHADER_OBJECT_FILE_NAME "$(IntDir)%(Filename).cso"
		VS_SHADER_VARIABLE_NAME "g_%(Filename)")
```

In `.gitignore`, after `/src/shared/deferred_shading/shaders/PS_BloomUpsample.h` add:

```gitignore
/src/shared/deferred_shading/shaders/CS_FogInject.h
/src/shared/deferred_shading/shaders/CS_FogTemporal.h
/src/shared/deferred_shading/shaders/CS_FogIntegrate.h
/src/shared/deferred_shading/shaders/PS_FogComposite.h
```

- [ ] **Step 2: Shared shader include**

Create `src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Froxel volumetric fog: the pass constant buffer and the grid math shared by the inject, temporal,
// integrate and composite shaders. The slice math and noise weighting mirror
// deferred_shading/volumetric_fog_settings.h (unit-tested) - change both together.

#ifndef VOLUMETRIC_FOG_COMMON_HLSLI
#define VOLUMETRIC_FOG_COMMON_HLSLI

#include "AtmosphereCommon.hlsli"

// MUST stay field-for-field in sync with VolumetricFogConstants in volumetric_fog_pass.cpp.
cbuffer VolumetricFogBuffer : register(b2)
{
    column_major matrix InverseViewProj;
    column_major matrix PrevViewProj;
    float3 CameraForward;
    float Jitter;               // [0, 1) depth offset of this frame's samples, in slices
    float3 PrevCameraPosition;
    float TemporalBlend;        // history weight
    float3 PrevCameraForward;
    float HistoryValid;         // 1 when the history volume may be reprojected
    uint GridWidth;
    uint GridHeight;
    uint GridDepth;
    uint DebugMode;             // 0 off, 1 scattered light, 2 transmittance, 3 density
    float NearDistance;
    float FarDistance;
    float NoiseSize;            // metres per noise tile
    float NoiseAmount;          // 0 smooth .. 1 patchy
    float2 WindOffset;          // accumulated wind scroll in noise tiles, wrapped to [0, 1)
    float SkyDistance;
    float VolumeEnabled;        // 0 when the froxel volume is off (analytic fog only)
};

// View depth of a normalized slice coordinate, exponential between NearDistance and FarDistance.
float SliceToDepth(float slice01)
{
    return NearDistance * pow(FarDistance / NearDistance, slice01);
}

// Normalized slice coordinate of a view depth: 0 at or before the near plane, above 1 past the far plane.
float DepthToSlice(float viewDepth)
{
    if (viewDepth <= NearDistance)
    {
        return 0.0f;
    }

    return log(viewDepth / NearDistance) / log(FarDistance / NearDistance);
}

// Density multiplier of a noise sample. Averages 1 over uniform noise.
float NoiseDensityFactor(float noise, float amount)
{
    return max(0.0f, 1.0f + amount * (2.0f * noise - 1.0f));
}

// World-space unit ray through a screen UV (y down).
float3 FogWorldRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), InverseViewProj);
    return normalize(farPoint.xyz / farPoint.w - CameraPosition);
}

// World position at a view depth (distance along the camera's forward axis) on a ray.
float3 FogWorldPosition(float3 ray, float viewDepth)
{
    return CameraPosition + ray * (viewDepth / max(dot(ray, CameraForward), 1e-4f));
}

#endif
```

- [ ] **Step 3: Inject compute shader**

Create `src/shared/deferred_shading/shaders/CS_FogInject.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Fills every froxel with this frame's fog: extinction from height fog times wind-scrolled noise, and
// the light scattered toward the camera (fog ambient + shadowed sun). Output rgb = radiance * sigma,
// a = sigma.

#include "VolumetricFogCommon.hlsli"

static const uint NUM_SHADOW_CASCADES = 4;

Texture3D<float> NoiseVolume : register(t0);
Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

SamplerState NoiseSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

// MUST stay field-for-field in sync with struct ShadowBuffer in deferred_renderer.cpp. Only the leading
// fields are read here.
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

RWTexture3D<float4> InjectOutput : register(u0);

// One hardware-PCF tap in the cascade covering this distance. 1 = lit.
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

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float viewDepth = SliceToDepth((float(id.z) + Jitter) / float(GridDepth));
    float3 worldPos = FogWorldPosition(ray, viewDepth);

    // The pattern moves with the wind: the noise at p now is the noise that was at p - offset.
    float3 noiseUvw = float3(worldPos.x / NoiseSize - WindOffset.x, worldPos.y / NoiseSize, worldPos.z / NoiseSize - WindOffset.y);
    float noise = NoiseVolume.SampleLevel(NoiseSampler, noiseUvw, 0.0f);

    float sigma = FogDensityAt(worldPos.y) * NoiseDensityFactor(noise, NoiseAmount);
    float visibility = SampleSunVisibility(worldPos, length(worldPos - CameraPosition));
    float3 radiance = FogSource(dot(ray, SunDirection), visibility);

    InjectOutput[id] = float4(radiance * sigma, sigma);
}
```

- [ ] **Step 4: Temporal and integrate compute shaders**

Create `src/shared/deferred_shading/shaders/CS_FogTemporal.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Blends this frame's jittered froxels with last frame's result, reprojected through the previous
// camera. Cells that were outside the previous grid, or all cells after a history reset, use the new
// value alone.

#include "VolumetricFogCommon.hlsli"

Texture3D<float4> CurrentVolume : register(t0);
Texture3D<float4> HistoryVolume : register(t1);
SamplerState LinearClampSampler : register(s0);
RWTexture3D<float4> TemporalOutput : register(u0);

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    float4 current = CurrentVolume.Load(int4(id, 0));
    if (HistoryValid < 0.5f)
    {
        TemporalOutput[id] = current;
        return;
    }

    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float3 worldPos = FogWorldPosition(ray, SliceToDepth((float(id.z) + 0.5f) / float(GridDepth)));

    float4 prevClip = mul(float4(worldPos, 1.0f), PrevViewProj);
    if (prevClip.w <= 1e-4f)
    {
        TemporalOutput[id] = current;
        return;
    }

    float2 prevNdc = prevClip.xy / prevClip.w;
    float3 prevUvw = float3(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f, DepthToSlice(dot(worldPos - PrevCameraPosition, PrevCameraForward)));
    if (any(prevUvw < 0.0f) || any(prevUvw > 1.0f))
    {
        TemporalOutput[id] = current;
        return;
    }

    float4 history = HistoryVolume.SampleLevel(LinearClampSampler, prevUvw, 0.0f);
    TemporalOutput[id] = lerp(current, history, TemporalBlend);
}
```

Create `src/shared/deferred_shading/shaders/CS_FogIntegrate.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Accumulates the smoothed froxels front to back along each grid column. Texel z holds the light
// scattered toward the camera and the transmittance from the near plane to the END of slice z.

#include "VolumetricFogCommon.hlsli"

Texture3D<float4> ScatteringVolume : register(t0);
RWTexture3D<float4> IntegratedOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight)
    {
        return;
    }

    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float pathScale = 1.0f / max(dot(ray, CameraForward), 1e-4f);

    float3 scattered = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;

    [loop]
    for (uint z = 0; z < GridDepth; ++z)
    {
        float4 cell = ScatteringVolume.Load(int4(id.xy, z, 0));
        float sigma = max(cell.a, 0.0f);

        float thickness = (SliceToDepth(float(z + 1) / float(GridDepth)) - SliceToDepth(float(z) / float(GridDepth))) * pathScale;
        float sliceTransmittance = exp(-sigma * thickness);
        float3 radiance = cell.rgb / max(sigma, 1e-6f);

        scattered += transmittance * radiance * (1.0f - sliceTransmittance);
        transmittance *= sliceTransmittance;

        IntegratedOutput[uint3(id.xy, z)] = float4(scattered, transmittance);
    }
}
```

- [ ] **Step 5: Composite pixel shader**

Create `src/shared/deferred_shading/shaders/PS_FogComposite.hlsl`:

```hlsl
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Applies the integrated fog volume to the lit opaque scene and continues with the closed-form height
// fog (sun unshadowed) from the volume's far plane to the pixel. With the volume disabled the whole
// ray uses the closed form.

#include "VolumetricFogCommon.hlsli"

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D NormalTexture : register(t1);          // a = linear radial depth, 0 = sky
Texture3D<float4> IntegratedVolume : register(t2);
Texture3D<float4> DensityVolume : register(t3);
SamplerState LinearClampSampler : register(s0);

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 pixel = int2(input.Position.xy);
    float4 scene = SceneTexture.Load(int3(pixel, 0));
    float radialDepth = NormalTexture.Load(int3(pixel, 0)).a;
    float sceneDistance = radialDepth <= 0.0f ? SkyDistance : radialDepth;

    float3 ray = FogWorldRay(input.TexCoord);
    float cosForward = max(dot(ray, CameraForward), 1e-4f);
    float viewDepth = sceneDistance * cosForward;

    float3 volumeScattered = float3(0.0f, 0.0f, 0.0f);
    float volumeTransmittance = 1.0f;
    float volumeEndDistance = 0.0f;
    float slice = 0.0f;

    if (VolumeEnabled > 0.5f)
    {
        float volumeDepth = min(viewDepth, FarDistance);
        slice = DepthToSlice(volumeDepth);

        // Integrated texel z holds the value at the end of slice z, i.e. at slice coordinate (z + 1) / N.
        float w = saturate((slice * float(GridDepth) - 0.5f) / float(GridDepth));
        float4 integrated = IntegratedVolume.SampleLevel(LinearClampSampler, float3(input.TexCoord, w), 0.0f);
        volumeScattered = integrated.rgb;
        volumeTransmittance = integrated.a;
        volumeEndDistance = volumeDepth / cosForward;
    }

    float tailLength = max(sceneDistance - volumeEndDistance, 0.0f);
    float tailTau = FogOpticalDepth(CameraPosition.y + ray.y * volumeEndDistance, ray.y, tailLength);
    float tailTransmittance = exp(-tailTau);
    float3 tailScattered = volumeTransmittance * FogSource(dot(ray, SunDirection), 1.0f) * (1.0f - tailTransmittance);

    float totalTransmittance = volumeTransmittance * tailTransmittance;
    float3 totalScattered = volumeScattered + tailScattered;

    if (DebugMode == 1)
    {
        return float4(totalScattered, 1.0f);
    }

    if (DebugMode == 2)
    {
        return float4(totalTransmittance.xxx, 1.0f);
    }

    if (DebugMode == 3)
    {
        float sigma = VolumeEnabled > 0.5f ? DensityVolume.SampleLevel(LinearClampSampler, float3(input.TexCoord, saturate(slice)), 0.0f).a : 0.0f;
        return float4(saturate(sigma / max(FogDensity * 4.0f, 1e-6f)).xxx, 1.0f);
    }

    return float4(scene.rgb * totalTransmittance + totalScattered, scene.a);
}
```

- [ ] **Step 6: Pass header**

Create `src/shared/deferred_shading/volumetric_fog_pass.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "cascaded_shadow_camera_setup.h"
#include "volumetric_fog_settings.h"

#include "base/non_copyable.h"
#include "graphics/constant_buffer.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/sampler_state.h"
#include "graphics/shader_base.h"
#include "graphics/vertex_buffer.h"
#include "graphics/volume_texture.h"
#include "math/matrix4.h"
#include "math/vector3.h"
#include "scene_graph/wind_state.h"

#include <array>

namespace mmo
{
	class Camera;

	/// @brief Froxel volumetric fog over the lit opaque scene.
	/// @remark Three compute steps fill a camera-aligned 3D grid (inject: height fog x wind noise and
	///         shadowed sun light; temporal: reprojected history blend; integrate: front-to-back
	///         accumulation), then a full-screen composite applies it and continues with the closed-form
	///         fog beyond the grid. With the volume disabled (quality 0) only the closed form runs.
	/// @remark Backend-neutral: it only uses GraphicsDevice abstractions. On backends without volume
	///         textures or compute shaders the volume stays disabled.
	class VolumetricFogPass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass and uploads the noise volume.
		VolumetricFogPass(GraphicsDevice& device, uint32 width, uint32 height);

		~VolumetricFogPass() override = default;

	public:
		/// @brief Records a new render size. Grid volumes are rebuilt lazily.
		void Resize(uint32 width, uint32 height);

		/// @brief Composites the fog onto sceneColor, writing the result into output.
		/// @param camera Camera the frame is rendered with.
		/// @param wind The frame's wind (noise scroll, size and amount).
		/// @param sceneColor The lit opaque scene (linear HDR). Read only.
		/// @param gbufferNormalRT G-Buffer normal target (a = radial depth).
		/// @param output Full-resolution target receiving the fogged scene. Must differ from sceneColor.
		/// @param cascadeShadowMaps The cascade depth maps.
		/// @param shadowBuffer The ShadowBuffer cbuffer already filled for this frame.
		/// @param cameraBuffer The scene camera cbuffer already refreshed for this camera.
		/// @param shadowSampler The cascade comparison sampler.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		void Render(Camera& camera, const WindState& wind, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
			const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
			ConstantBuffer& cameraBuffer, SamplerState& shadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Discards the temporal history, e.g. after a frame in which the pass did not run.
		void InvalidateHistory() { m_historyValid = false; }

		/// @brief Gets the mutable settings.
		[[nodiscard]] VolumetricFogSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const VolumetricFogSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief (Re)creates the grid volumes for the current preset and render size.
		void EnsureVolumes();

		/// @brief Releases the grid volumes.
		void ReleaseVolumes();

		/// @brief Whether this backend can run the froxel volume at all.
		[[nodiscard]] bool SupportsVolume() const;

	private:
		GraphicsDevice& m_device;
		VolumetricFogSettings m_settings;

		uint32 m_width;
		uint32 m_height;

		uint32 m_gridWidth = 0;
		uint32 m_gridHeight = 0;
		uint32 m_gridDepth = 0;

		VolumeTexturePtr m_noiseVolume;
		VolumeTexturePtr m_injectVolume;
		std::array<VolumeTexturePtr, 2> m_historyVolumes;
		VolumeTexturePtr m_integratedVolume;

		/// @brief Index into m_historyVolumes written this frame; the other one holds last frame.
		uint32 m_historyIndex = 0;
		bool m_historyValid = false;
		uint64 m_frameIndex = 0;

		Matrix4 m_prevViewProj;
		Vector3 m_prevCameraPosition;
		Vector3 m_prevCameraForward;
		float m_prevRange = 0.0f;

		ConstantBufferPtr m_fogBuffer;

		ShaderPtr m_injectCs;
		ShaderPtr m_temporalCs;
		ShaderPtr m_integrateCs;
		ShaderPtr m_compositePs;

		SamplerStatePtr m_linearClampSampler;
		SamplerStatePtr m_noiseSampler;
	};
}
```

- [ ] **Step 7: Pass implementation**

Create `src/shared/deferred_shading/volumetric_fog_pass.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "volumetric_fog_pass.h"

#include "fog_noise.h"

#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/CS_FogInject.h"
#	include "shaders/CS_FogTemporal.h"
#	include "shaders/CS_FogIntegrate.h"
#	include "shaders/PS_FogComposite.h"
#	define MMO_FOG_INJECT_CS_BYTECODE g_CS_FogInject
#	define MMO_FOG_INJECT_CS_SIZE std::size(g_CS_FogInject)
#	define MMO_FOG_TEMPORAL_CS_BYTECODE g_CS_FogTemporal
#	define MMO_FOG_TEMPORAL_CS_SIZE std::size(g_CS_FogTemporal)
#	define MMO_FOG_INTEGRATE_CS_BYTECODE g_CS_FogIntegrate
#	define MMO_FOG_INTEGRATE_CS_SIZE std::size(g_CS_FogIntegrate)
#	define MMO_FOG_COMPOSITE_PS_BYTECODE g_PS_FogComposite
#	define MMO_FOG_COMPOSITE_PS_SIZE std::size(g_PS_FogComposite)
#else
#	define MMO_FOG_INJECT_CS_BYTECODE nullptr
#	define MMO_FOG_INJECT_CS_SIZE 0
#	define MMO_FOG_TEMPORAL_CS_BYTECODE nullptr
#	define MMO_FOG_TEMPORAL_CS_SIZE 0
#	define MMO_FOG_INTEGRATE_CS_BYTECODE nullptr
#	define MMO_FOG_INTEGRATE_CS_SIZE 0
#	define MMO_FOG_COMPOSITE_PS_BYTECODE nullptr
#	define MMO_FOG_COMPOSITE_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr uint32 noiseSeed = 1337;
		constexpr uint32 threadGroupSize = 8;

		/// @brief Mirrors VolumetricFogBuffer in VolumetricFogCommon.hlsli (b2).
		struct alignas(16) VolumetricFogConstants
		{
			Matrix4 inverseViewProj;
			Matrix4 prevViewProj;

			float cameraForward[3];
			float jitter;

			float prevCameraPosition[3];
			float temporalBlend;

			float prevCameraForward[3];
			float historyValid;

			uint32 gridWidth;
			uint32 gridHeight;
			uint32 gridDepth;
			uint32 debugMode;

			float nearDistance;
			float farDistance;
			float noiseSize;
			float noiseAmount;

			float windOffsetX;
			float windOffsetZ;
			float skyDistance;
			float volumeEnabled;
		};

		static_assert(sizeof(VolumetricFogConstants) == 224, "VolumetricFogConstants must match the HLSL layout");

		uint32 groupCount(const uint32 cells)
		{
			return (cells + threadGroupSize - 1) / threadGroupSize;
		}

		void copyVector(const Vector3& source, float destination[3])
		{
			destination[0] = source.x;
			destination[1] = source.y;
			destination[2] = source.z;
		}
	}

	VolumetricFogPass::VolumetricFogPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_width(width)
		, m_height(height)
	{
		m_fogBuffer = m_device.CreateConstantBuffer(sizeof(VolumetricFogConstants), nullptr);
		ASSERT(m_fogBuffer);

		m_compositePs = m_device.CreateShader(ShaderType::PixelShader, MMO_FOG_COMPOSITE_PS_BYTECODE, MMO_FOG_COMPOSITE_PS_SIZE);
		ASSERT(m_compositePs);

		if (MMO_FOG_INJECT_CS_SIZE > 0)
		{
			m_injectCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_FOG_INJECT_CS_BYTECODE, MMO_FOG_INJECT_CS_SIZE);
			m_temporalCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_FOG_TEMPORAL_CS_BYTECODE, MMO_FOG_TEMPORAL_CS_SIZE);
			m_integrateCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_FOG_INTEGRATE_CS_BYTECODE, MMO_FOG_INTEGRATE_CS_SIZE);
		}

		SamplerDesc linearClamp;
		linearClamp.filter = SamplerFilter::Linear;
		linearClamp.address = SamplerAddress::Clamp;
		m_linearClampSampler = m_device.CreateSamplerState(linearClamp);

		SamplerDesc noiseWrap;
		noiseWrap.filter = SamplerFilter::Linear;
		noiseWrap.address = SamplerAddress::Wrap;
		m_noiseSampler = m_device.CreateSamplerState(noiseWrap);

		constexpr uint32 noiseSize = VolumetricFogSettings::NoiseResolution;
		m_noiseVolume = m_device.CreateVolumeTexture(noiseSize, noiseSize, noiseSize, VolumeFormat::R8, false);
		if (m_noiseVolume)
		{
			const std::vector<uint8> noise = GenerateFogNoise(noiseSize, noiseSeed);
			m_noiseVolume->Upload(noise.data(), noise.size());
		}
	}

	void VolumetricFogPass::Resize(const uint32 width, const uint32 height)
	{
		m_width = width;
		m_height = height;
		ReleaseVolumes();
	}

	bool VolumetricFogPass::SupportsVolume() const
	{
		return m_injectCs && m_temporalCs && m_integrateCs && m_noiseVolume && m_linearClampSampler && m_noiseSampler;
	}

	void VolumetricFogPass::ReleaseVolumes()
	{
		m_injectVolume.reset();
		m_historyVolumes[0].reset();
		m_historyVolumes[1].reset();
		m_integratedVolume.reset();
		m_gridWidth = 0;
		m_gridHeight = 0;
		m_gridDepth = 0;
		m_historyValid = false;
	}

	void VolumetricFogPass::EnsureVolumes()
	{
		const uint32 gridWidth = m_settings.GetGridWidth(m_width);
		const uint32 gridHeight = m_settings.GetGridHeight(m_height);
		const uint32 gridDepth = m_settings.sliceCount;

		if (m_injectVolume && gridWidth == m_gridWidth && gridHeight == m_gridHeight && gridDepth == m_gridDepth)
		{
			return;
		}

		const auto create = [this, gridWidth, gridHeight, gridDepth]()
		{
			return m_device.CreateVolumeTexture(static_cast<uint16>(gridWidth), static_cast<uint16>(gridHeight), static_cast<uint16>(gridDepth), VolumeFormat::RGBA16F, true);
		};

		m_injectVolume = create();
		m_historyVolumes[0] = create();
		m_historyVolumes[1] = create();
		m_integratedVolume = create();

		m_gridWidth = gridWidth;
		m_gridHeight = gridHeight;
		m_gridDepth = gridDepth;
		m_historyIndex = 0;
		m_historyValid = false;
	}

	void VolumetricFogPass::Render(Camera& camera, const WindState& wind, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
		const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
		ConstantBuffer& cameraBuffer, SamplerState& shadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs)
	{
		// Blend state persists across frames and the previous forward pass may have left alpha blending
		// on; the composite must overwrite its target.
		m_device.SetBlendMode(BlendMode::Opaque);

		const bool volumeEnabled = m_settings.IsVolumeEnabled() && SupportsVolume();
		if (volumeEnabled)
		{
			EnsureVolumes();
		}
		else if (m_injectVolume)
		{
			ReleaseVolumes();
		}

		const Matrix4 viewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		const Vector3 cameraPosition = camera.GetDerivedPosition();
		const Vector3 cameraForward = camera.GetDerivedDirection().NormalizedCopy();

		const float resetDistance = VolumetricFogSettings::HistoryResetDistance;
		if ((cameraPosition - m_prevCameraPosition).GetSquaredLength() > resetDistance * resetDistance || m_settings.range != m_prevRange)
		{
			m_historyValid = false;
		}

		VolumetricFogConstants constants{};
		constants.inverseViewProj = viewProj.Inverse();
		constants.prevViewProj = m_prevViewProj;
		copyVector(cameraForward, constants.cameraForward);
		constants.jitter = volumetric_fog::JitterForFrame(m_frameIndex);
		copyVector(m_prevCameraPosition, constants.prevCameraPosition);
		constants.temporalBlend = VolumetricFogSettings::TemporalBlend;
		copyVector(m_prevCameraForward, constants.prevCameraForward);
		constants.historyValid = m_historyValid ? 1.0f : 0.0f;
		constants.gridWidth = m_gridWidth;
		constants.gridHeight = m_gridHeight;
		constants.gridDepth = m_gridDepth;
		constants.debugMode = m_settings.debugMode;
		constants.nearDistance = VolumetricFogSettings::NearDistance;
		constants.farDistance = m_settings.range;
		constants.noiseSize = wind.noiseSize;
		constants.noiseAmount = wind.noiseAmount;
		constants.windOffsetX = wind.noiseOffsetX;
		constants.windOffsetZ = wind.noiseOffsetZ;
		constants.skyDistance = VolumetricFogSettings::SkyDistance;
		constants.volumeEnabled = volumeEnabled ? 1.0f : 0.0f;
		m_fogBuffer->Update(&constants);

		if (volumeEnabled)
		{
			cameraBuffer.BindToStage(ShaderType::ComputeShader, 1);
			m_fogBuffer->BindToStage(ShaderType::ComputeShader, 2);
			shadowBuffer.BindToStage(ShaderType::ComputeShader, 3);

			// --- Inject -----------------------------------------------------------------------
			m_noiseVolume->Bind(ShaderType::ComputeShader, 0);
			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				cascadeShadowMaps[i]->Bind(ShaderType::ComputeShader, 5 + i);
			}
			m_noiseSampler->Bind(ShaderType::ComputeShader, 0);
			shadowSampler.Bind(ShaderType::ComputeShader, 1);
			m_injectVolume->BindWritable(0);
			m_injectCs->Set();
			m_device.Dispatch(groupCount(m_gridWidth), groupCount(m_gridHeight), groupCount(m_gridDepth));
			m_device.ClearComputeBindings();

			// --- Temporal ---------------------------------------------------------------------
			const VolumeTexturePtr& temporalTarget = m_historyVolumes[m_historyIndex];
			const VolumeTexturePtr& history = m_historyVolumes[1 - m_historyIndex];
			m_injectVolume->Bind(ShaderType::ComputeShader, 0);
			history->Bind(ShaderType::ComputeShader, 1);
			m_linearClampSampler->Bind(ShaderType::ComputeShader, 0);
			temporalTarget->BindWritable(0);
			m_temporalCs->Set();
			m_device.Dispatch(groupCount(m_gridWidth), groupCount(m_gridHeight), groupCount(m_gridDepth));
			m_device.ClearComputeBindings();

			// --- Integrate --------------------------------------------------------------------
			temporalTarget->Bind(ShaderType::ComputeShader, 0);
			m_integratedVolume->BindWritable(0);
			m_integrateCs->Set();
			m_device.Dispatch(groupCount(m_gridWidth), groupCount(m_gridHeight), 1);
			m_device.ClearComputeBindings();
		}

		// --- Composite ------------------------------------------------------------------------
		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		quad.Set(0);
		cameraBuffer.BindToStage(ShaderType::PixelShader, 1);
		m_fogBuffer->BindToStage(ShaderType::PixelShader, 2);

		output.Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_width), static_cast<int32>(m_height), 0.0f, 1.0f);

		sceneColor.Bind(ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		if (volumeEnabled)
		{
			m_integratedVolume->Bind(ShaderType::PixelShader, 2);
			m_historyVolumes[m_historyIndex]->Bind(ShaderType::PixelShader, 3);
		}

		m_compositePs->Set();

		// Last, so no device state call above can replace the sampler.
		if (m_linearClampSampler)
		{
			m_linearClampSampler->Bind(ShaderType::PixelShader, 0);
		}

		m_device.Draw(6, 0);

		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);

		// --- Frame bookkeeping ----------------------------------------------------------------
		if (volumeEnabled)
		{
			m_historyIndex = 1 - m_historyIndex;
			m_historyValid = true;
		}

		m_prevViewProj = viewProj;
		m_prevCameraPosition = cameraPosition;
		m_prevCameraForward = cameraForward;
		m_prevRange = m_settings.range;
		++m_frameIndex;
	}
}
```

The composite binds the **freshly written** temporal volume for debug view 3. `m_historyIndex` only flips in the bookkeeping block after the draw, so `m_historyVolumes[m_historyIndex]` still names this frame's temporal output at that point.

- [ ] **Step 8: Build the library and its dependents**

Run: `cmake -S . -B build; cmake --build build --config Debug -t deferred_shading mmo_client mmo_edit`
Expected:
- The four shader headers `CS_FogInject.h`, `CS_FogTemporal.h`, `CS_FogIntegrate.h`, `PS_FogComposite.h` appear in `src/shared/deferred_shading/shaders/`.
- The build succeeds.

If FXC reports an error, fix the HLSL and note it in the report. Keep the constant buffer layout identical to `VolumetricFogConstants`; the `static_assert` guards its size.

Run: `bin/Debug/deferred_shading_tests.exe; bin/Debug/scene_graph_tests.exe`
Expected: all tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/shared/deferred_shading/shaders/VolumetricFogCommon.hlsli src/shared/deferred_shading/shaders/CS_FogInject.hlsl src/shared/deferred_shading/shaders/CS_FogTemporal.hlsl src/shared/deferred_shading/shaders/CS_FogIntegrate.hlsl src/shared/deferred_shading/shaders/PS_FogComposite.hlsl src/shared/deferred_shading/volumetric_fog_pass.h src/shared/deferred_shading/volumetric_fog_pass.cpp src/shared/deferred_shading/CMakeLists.txt .gitignore
git commit -m "feat(render): froxel volumetric fog pass and compute shaders

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Switch the renderer to the froxel pass and remove the march

**Files:**
- Modify: `src/shared/deferred_shading/deferred_renderer.h` (include line 11; setters 245–252; `BindShadowSampler` doc ~300; member ~356; sampler member 408–411)
- Modify: `src/shared/deferred_shading/deferred_renderer.cpp` (include line 8; `BindShadowSampler` 92–100; constructor 152 and 181–205; GPU timer name 325; `Resize` 345; `Render` 453–487)
- Modify: `src/mmo_client/game_states/world_state.h` (`OnAtmosphereRenderingChanged` comment), `world_state.cpp` (statics 146–148, registration ~2140–2147, unregister ~2293–2295, handler ~6233–6244)
- Delete: `src/shared/deferred_shading/atmosphere_pass.h`, `atmosphere_pass.cpp`, `atmosphere_pass_settings.h`, `shaders/PS_AtmosphereMarch.hlsl`, `shaders/PS_AtmosphereBlur.hlsl`, `shaders/PS_AtmosphereComposite.hlsl`, `src/tests/deferred_shading_tests/test_atmosphere_pass_settings.cpp`
- Modify: `.gitignore` (remove the three `PS_Atmosphere*.h` lines)
- Modify: `docs/rendering-atmosphere.md` (rewrite), `docs/console_commands.md`

**Interfaces:**
- Consumes:
  - Task 1: `SamplerDesc`, `SamplerFilter`, `SamplerAddress`, `SamplerComparison`, `GraphicsDevice::CreateSamplerState`, `SamplerStatePtr`.
  - Task 4: `Scene::GetWind()`.
  - Task 5: `VolumetricFogPass`.
- Produces:
  - `DeferredRenderer::SetAtmosphereQuality(int)` (now froxel presets).
  - `DeferredRenderer::SetVolumetricFogRange(float)`.
  - `DeferredRenderer::SetAtmosphereDebugMode(int)`.
  - Cvar `gxVolumetricFogRange` (default `"200"`) replaces `gxAtmosphereMarchDistance`.

- [ ] **Step 1: Renderer header**

In `deferred_renderer.h`:
- Replace `#include "atmosphere_pass.h"` with `#include "volumetric_fog_pass.h"` and add `#include "graphics/sampler_state.h"`.
- Replace the three atmosphere setters (lines 245–252) with:

```cpp
        /// @brief Applies the volumetric fog quality preset: 0 Off (closed-form fog only), 1 Low ... 4 Ultra.
        void SetAtmosphereQuality(int level) { m_volumetricFogPass->GetSettings().ApplyQualityLevel(level); }

        /// @brief Sets the view depth the fog volume covers in metres (clamped to [50, 300]).
        void SetVolumetricFogRange(float range) { m_volumetricFogPass->GetSettings().SetRange(range); }

        /// @brief Sets the fog debug view: 0 off, 1 scattered light, 2 transmittance, 3 density.
        void SetAtmosphereDebugMode(int mode) { m_volumetricFogPass->GetSettings().SetDebugMode(mode); }
```

- Replace the member `std::unique_ptr<AtmospherePass> m_atmospherePass;` and its comment with:

```cpp
        /// @brief Froxel volumetric fog and light shafts. Runs after lighting, before the forward pass.
        std::unique_ptr<VolumetricFogPass> m_volumetricFogPass;
```

- Replace the block

```cpp
#ifdef _WIN32
		ComPtr<ID3D11SamplerState> m_shadowSampler{ nullptr };

#endif
```

with:

```cpp
        /// @brief Cascade comparison sampler (lighting pass s1, fog inject s1). Null on backends
        ///        without explicit sampler objects.
        SamplerStatePtr m_shadowSampler;
```

- [ ] **Step 2: Renderer implementation**

In `deferred_renderer.cpp`:
- Replace `#include "atmosphere_pass.h"` with `#include "volumetric_fog_pass.h"`.
- Replace the body of `DeferredRenderer::BindShadowSampler()` with:

```cpp
        if (m_shadowSampler)
        {
            m_shadowSampler->Bind(ShaderType::PixelShader, 1);
        }
```

- In the constructor, replace `m_atmospherePass = std::make_unique<AtmospherePass>(m_device, width, height);` with:

```cpp
        m_volumetricFogPass = std::make_unique<VolumetricFogPass>(m_device, width, height);
```

- Replace the whole `#ifdef WIN32 … #endif` sampler block at the end of the constructor (lines 181–205) with:

```cpp
        // Anisotropic comparison with a white border: shadow edges fade to lit instead of darkening.
        SamplerDesc shadowSamplerDesc;
        shadowSamplerDesc.filter = SamplerFilter::ComparisonAnisotropic;
        shadowSamplerDesc.address = SamplerAddress::Border;
        shadowSamplerDesc.comparison = SamplerComparison::LessEqual;
        for (float& channel : shadowSamplerDesc.borderColor)
        {
            channel = 1.0f;
        }
        shadowSamplerDesc.maxAnisotropy = 4;
        m_shadowSampler = m_device.CreateSamplerState(shadowSamplerDesc);
```

- In `GpuTimerEndAndCollect`, change `emit("GPU: Atmosphere", 5, 6);` to `emit("GPU: Volumetric fog", 5, 6);`.
- In `Resize`, replace `m_atmospherePass->Resize(width, height);` with `m_volumetricFogPass->Resize(width, height);`.
- In `Render`, replace the block from the comment `// Height fog and light shafts over the lit opaque scene.` through the closing brace of `if (runAtmosphere) { … }` with:

```cpp
        // Froxel volumetric fog over the lit opaque scene. The composite reads m_renderTexture and
        // writes m_sceneColorCopy, which stays the refraction source; the single CopyResource below
        // then carries the fogged scene back into m_renderTexture for the forward pass. Skipped while
        // submerged (the underwater pass has its own fog), with fog turned off, when the combined
        // density is zero, or without a shadow sampler; the copy then runs in its old direction and
        // the fog history is discarded so it does not reproject a stale frame later.
        const bool runAtmosphere = scene.IsFogEnabled() && !m_underwaterState.active
            && scene.GetCombinedFogDensity() > 0.0f && m_shadowSampler != nullptr;
        if (runAtmosphere)
        {
            m_volumetricFogPass->Render(camera, scene.GetWind(), *m_renderTexture, m_gBuffer.GetNormalRT(), *m_sceneColorCopy,
                m_cascadeShadowMaps, *m_shadowBuffer, *scene.GetCameraBuffer(), *m_shadowSampler, *m_quadBuffer, *m_deferredLightVs);
        }
        else
        {
            m_volumetricFogPass->InvalidateHistory();
        }
```

- Change the comment `// after atmosphere` to `// after volumetric fog`.

- [ ] **Step 3: Remove the old pass**

```bash
git rm src/shared/deferred_shading/atmosphere_pass.h src/shared/deferred_shading/atmosphere_pass.cpp src/shared/deferred_shading/atmosphere_pass_settings.h src/shared/deferred_shading/shaders/PS_AtmosphereMarch.hlsl src/shared/deferred_shading/shaders/PS_AtmosphereBlur.hlsl src/shared/deferred_shading/shaders/PS_AtmosphereComposite.hlsl src/tests/deferred_shading_tests/test_atmosphere_pass_settings.cpp
```

- Delete the untracked generated headers `src/shared/deferred_shading/shaders/PS_AtmosphereMarch.h`, `PS_AtmosphereBlur.h` and `PS_AtmosphereComposite.h` from disk if present.
- Remove their three lines from `.gitignore`.
- Search `src/` for `AtmospherePass`, `atmosphere_pass` and `SetAtmosphereMarchDistance`. The only remaining hits should be the `world_state` ones fixed in Step 4.

- [ ] **Step 4: Client cvars**

In `world_state.cpp`:
- Rename `static ConsoleVar *s_atmosphereMarchDistanceVar = nullptr;` to `static ConsoleVar *s_volumetricFogRangeVar = nullptr;`.
- Replace the registrations of the three atmosphere cvars with:

```cpp
		s_atmosphereQualityVar = ConsoleVarMgr::RegisterConsoleVar("gxAtmosphereQuality", "Volumetric fog quality: 0 = Off (smooth height fog only), 1 = Low (24 px cells, 32 slices), 2 = Medium (16 px, 48), 3 = High (12 px, 64), 4 = Ultra (8 px, 96).", "3");
		m_cvarChangedSignals += s_atmosphereQualityVar->Changed.connect(this, &WorldState::OnAtmosphereRenderingChanged);

		s_volumetricFogRangeVar = ConsoleVarMgr::RegisterConsoleVar("gxVolumetricFogRange", "How many metres in front of the camera the volumetric fog covers (50 to 300, the shadow range). Fog beyond uses a smooth closed-form estimate.", "200");
		m_cvarChangedSignals += s_volumetricFogRangeVar->Changed.connect(this, &WorldState::OnAtmosphereRenderingChanged);

		s_atmosphereDebugVar = ConsoleVarMgr::RegisterConsoleVar("gxAtmosphereDebug", "Fog debug view: 0 = off, 1 = scattered light, 2 = transmittance, 3 = fog density.", "0");
		m_cvarChangedSignals += s_atmosphereDebugVar->Changed.connect(this, &WorldState::OnAtmosphereRenderingChanged);
```

- In `RemoveGameplayCommands`, replace `ConsoleVarMgr::UnregisterConsoleVar("gxAtmosphereMarchDistance");` with `ConsoleVarMgr::UnregisterConsoleVar("gxVolumetricFogRange");`.
- In `OnAtmosphereRenderingChanged`, replace `renderer->SetAtmosphereMarchDistance(s_atmosphereMarchDistanceVar->GetFloatValue());` with `renderer->SetVolumetricFogRange(s_volumetricFogRangeVar->GetFloatValue());`.

In `world_state.h`, change the `OnAtmosphereRenderingChanged` comment to `/// @brief Called when gxAtmosphereQuality, gxVolumetricFogRange or gxAtmosphereDebug changed.`

Search `data/client/Interface` for `gxAtmosphereMarchDistance`. Do not edit `data/`; if anything is found, list it in the report.

- [ ] **Step 5: Docs**

Replace the entire content of `docs/rendering-atmosphere.md` with:

````markdown
# Atmosphere: Volumetric Fog, Light Shafts, Bloom

Designs: [froxel volumetric fog](superpowers/specs/2026-09-14-froxel-volumetric-fog-design.md),
[zone environment profiles](superpowers/specs/2026-09-14-zone-environment-profiles-design.md),
[original atmosphere](superpowers/specs/2026-09-13-volumetric-atmosphere-design.md)

## Frame order (DeferredRenderer::Render)

1. Cascaded shadow maps → G-Buffer → SSAO → contact shadows
2. Lighting (`PS_DeferredLighting`) — **linear HDR**, no fog, no tonemap
3. `VolumetricFogPass`
   - **Compute steps:**
     1. `CS_FogInject`: height fog × wind noise, and fog ambient + shadowed sun, per froxel.
     2. `CS_FogTemporal`: blend with last frame's grid, reprojected through the previous camera.
     3. `CS_FogIntegrate`: front-to-back accumulation.
   - **Composite:** `PS_FogComposite` applies the volume and continues with closed-form fog beyond it, then the result is copied back into the scene target.
   - **Skipped** while submerged, with `Scene::IsFogEnabled()` false, or when the combined fog density is zero. The history is then discarded.
4. Forward pass — `Scene::SetForwardOutputLinear(true)`; materials apply the analytic height fog (no noise, no shafts)
5. `BloomPass` — skipped while submerged
6. `TonemapPass` — scene + bloom, exposure, ACES, gamma, dither
7. `PostProcessPass` (underwater only)

## The linear-HDR contract

Nothing before the TonemapPass may tone map or gamma-encode. Forward materials rendered outside
DeferredRenderer (editor previews, model frames, the minimap baker) keep tone mapping in the material
because they never set `forwardOutputLinear`. Material graphs sampling Scene Color / SSR receive a
display-referred sample through the generated `LoadSceneColor` helper.

## Froxel grid

A camera-aligned 3D grid covers the screen and the first `gxVolumetricFogRange` metres of view depth.
Depth slices are spaced exponentially from 0.5 m, so cells near the camera are thin.

| Quality (`gxAtmosphereQuality`) | Cell size | Depth slices | 1080p grid |
|---|---|---|---|
| 0 Off | – | – | no volume, closed-form fog only |
| 1 Low | 24 px | 32 | 80×45×32 |
| 2 Medium | 16 px | 48 | 120×68×48 |
| 3 High (default) | 12 px | 64 | 160×90×64 |
| 4 Ultra | 8 px | 96 | 240×135×96 |

**Jitter and history:**
- Each frame samples every cell at a Halton-jittered depth inside its slice.
- The temporal step keeps 90% of the reprojected history, which removes the jitter noise and smooths the shafts.
- History resets on resize, quality or range change, a camera jump over 50 m, or a frame without fog.

**Formula sync:**
- The grid math lives in `deferred_shading/volumetric_fog_settings.h` (unit-tested) and is mirrored in `shaders/VolumetricFogCommon.hlsli`.
- `VolumetricFogConstants` in `volumetric_fog_pass.cpp` must match that include's cbuffer (size asserted).

## Fog model

**Density:**
- Density `σ(y) = density · densityMultiplier · exp(min(−fog_height_falloff · (y − base), 3))`, with `base = reference height + fog_base_height`.
- The reference height is the controlled player's height in the client and the camera pivot in the editor.
- Inside the grid, σ is multiplied by `max(0, 1 + fog_noise_amount · (2n − 1))`, where n samples a 64³ tiling noise volume (`fog_noise.cpp`) at `worldPos / fog_noise_size − windOffset`.
- That factor averages 1, so the smooth fog beyond the grid matches the grid's brightness.

**Scattered light:** per metre, the fog scatters `σ · (FogTint + SunScatterColor · SunColor · SunIntensity · shaft_strength · Phase(cosθ) · shadow)` toward the camera. Phase is Henyey-Greenstein (g = `fog_anisotropy`) blended 80/20 with isotropic.

**Formula copies:** the closed-form formulas exist three times and must change together: `shaders/AtmosphereCommon.hlsli`, the forward fog emitted by `MaterialCompilerD3D11`, and `deferred_shading/atmosphere_math.h`. The camera cbuffer (b1, 176 bytes) is declared three times as well; changing it requires **Tools → Rebuild All Materials**.

## Environment profiles and wind

The look comes from environment profiles (`environment_profiles` game-data table, edited in the
editor's Environment Profile Editor):

| Curve | rgb | alpha |
|---|---|---|
| `sky_horizon`, `sky_zenith`, `clouds` | sky material colours | unused |
| `ambient` | scene ambient | unused |
| `sun`, `moon` | light colour | intensity |
| `fog` | fog ambient radiance | density multiplier |
| `sun_scatter` | sun colour inside fog | shaft multiplier |

**Fixed values:**
- fog: density, height falloff, base height, anisotropy
- light: shaft strength, exposure, bloom intensity, bloom threshold
- blending: transition seconds
- wind and noise: wind direction (degrees clockwise from +Z, direction the wind blows toward), wind speed (m/s), gustiness, fog noise amount, fog noise size (metres)

**Profile resolution:** a zone uses its own profile, else its parent's, else the map's default profile, else the built-in Default. `EnvironmentController` fades between profiles.

**Wind:**
- `WindSimulation` adds gusts (speed ±60% · gustiness, direction ±25° · gustiness) and integrates the noise offset in double precision.
- `Scene::SetWind` hands it to the fog pass.
- The `WindDirection` global shader parameter (xyz direction, w speed) is published for future foliage and particle use.

## Console variables

| Cvar | Default | Meaning |
|---|---|---|
| `gxAtmosphereQuality` | 3 | 0 Off, 1 Low, 2 Medium, 3 High, 4 Ultra (grid size) |
| `gxVolumetricFogRange` | 200 | metres of view depth covered by the fog volume (50–300) |
| `gxAtmosphereDebug` | 0 | 1 scattered light, 2 transmittance, 3 fog density |
| `gxBloomQuality` | 2 | 0 Off, 1 Low, 2 High |
| `gxExposure` | 1.0 | player brightness, multiplies the profile exposure |

## Known limitations

- Water, particles and glass get closed-form fog only: no noise, no shafts.
- Shafts end at the 300 m shadow range; mountains farther away cannot block the sun.
- Point and spot lights do not scatter in the fog yet (planned: project B). Local fog volumes are planned as project C.
- Bloom strength is the environment profile's bloom intensity divided by the number of bloom levels.
- Debug views are composited before bloom and tone mapping, so they appear tone-mapped.
````

In `docs/console_commands.md`:
- replace any `gxAtmosphereMarchDistance` entry with ``- `gxVolumetricFogRange` - Metres of view depth covered by the volumetric fog, 50–300 (default: 200)``;
- update the `gxAtmosphereQuality` description to "Volumetric fog quality 0–4 (default: 3)";
- update `gxAtmosphereDebug` to "Fog debug view: 1 scattered light, 2 transmittance, 3 density (default: 0)".

- [ ] **Step 6: Build and test**

Run: `cmake -S . -B build; cmake --build build --config Debug -t deferred_shading_tests scene_graph_tests client_data_tests mmo_client mmo_edit`
Expected: builds succeed.

Run: `bin/Debug/deferred_shading_tests.exe; bin/Debug/scene_graph_tests.exe; bin/Debug/client_data_tests.exe`
Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/shared/deferred_shading/deferred_renderer.h src/shared/deferred_shading/deferred_renderer.cpp src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp .gitignore docs/rendering-atmosphere.md docs/console_commands.md
git commit -m "feat(render): deferred renderer uses froxel volumetric fog; remove the per-pixel march

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

(The `git rm` from Step 3 is already staged and goes into this commit.)

---

### Task 7: In-game verification, perf, gate

The controller (main session) runs this task, not a subagent. Use the scratchpad driver scripts and the `client-visual-verification` memory recipes: auto-login, Alt-tap foreground + real-cursor click, console key VK 0xC0, `set <cvar> <value>`. Only drive input while the user is away. Stop immediately if there are signs of user presence.

- [ ] **Step 1: Start the stack and enter the world**

Start `login_server`, `realm_server` and `world_server` from `bin/Debug` if they are not running. Run `run_view2.ps1 -Exe mmo_client.exe` and capture two screenshots.
Expected: the world renders with fog; there are no black or NaN frames, and no fully white screen.

- [ ] **Step 2: Look checks**

Capture screenshots with these console commands (one run each):
1. Default look (no command), for drifting banks and gaps.
2. `set gxAtmosphereDebug 1`, `set gxAtmosphereDebug 2`, `set gxAtmosphereDebug 3`.
3. `set gxAtmosphereQuality 1` and `set gxAtmosphereQuality 4`, then `set gxAtmosphereQuality 0` (closed-form fog only, no crash).

Also:
- Turn the character with the arrow-key hold between two screenshots and compare them for ghosting or smearing.
- Capture two screenshots 5 s apart to confirm the fog pattern moves with the wind.

- [ ] **Step 3: Perf numbers**

Run `set perf 1` and read `GPU: Volumetric fog` at quality 3 and at quality 1 from a screenshot.
Target: ≤ 2 ms at High and ≤ 0.7 ms at Low. Debug builds read higher; record the numbers either way.

- [ ] **Step 4: Fix loop**

For any visual defect (NaNs, banding, a seam at the fog range, a wrong wind direction, temporal smearing), dispatch one fix subagent with the screenshot findings and re-verify.

- [ ] **Step 5: Gate**

Stop the servers this session started before running the gate. Run `/gate`.
Expected: green. Address any review findings before reporting.

- [ ] **Step 6: Record**

Append an `## Implementation Notes` section to the spec (deviations, perf numbers, tuning), update the `volumetric-atmosphere` memory, and commit the spec notes.

## Spec coverage

| Spec requirement | Task |
|---|---|
| VolumeTexture, compute shaders, Dispatch, ClearComputeBindings, SamplerState, CS build rules, null-safe defaults | 1, 5 |
| DeferredRenderer shadow sampler through SamplerState; callback idiom removed | 6 |
| GPU timer mark renamed | 6 |
| Grid presets, exponential slices, range clamp, Halton jitter, noise weighting | 2 |
| Tiling 64³ FBM noise with determinism, range, mean, tiling tests | 2 |
| Inject / temporal / integrate / composite pipeline, history reset rules, far-tail closed form, debug views 1–3 | 5 |
| Fog cbuffer at b2 with camera cbuffer untouched | 5 |
| Cvars gxAtmosphereQuality 0–4, gxVolumetricFogRange, gxAtmosphereDebug | 6 |
| Profile fields 20–24, clamps, blend (shortest arc, opposite fallback), editor section | 3 |
| WindSimulation gusts, double offset, wrapped publish, Scene::SetWind, WindDirection parameter, editor real-time wind | 4 |
| Removal of AtmospherePass and march shaders | 6 |
| Docs rewrite | 6 |
| Visual verification, perf budget, gate | 7 |
