# Froxel Volumetric Fog with Zone Wind (Project A)

Date: 2026-09-14
Status: approved design, awaiting spec review
Branch: `feature/volumetric-atmosphere`

## Goal

Replace today's per-pixel light-shaft ray march with real volumetric fog stored in a camera-aligned 3D grid ("froxels"):
- Fog density is height fog modulated by drifting 3D noise, moved by a per-zone wind.
- Sun shafts come from the cascaded shadow maps.
- Temporal smoothing keeps the result stable.

Wind and noise are authored per zone in environment profiles. The editor previews them live.

This is project A of three. Project B (point/spot light scattering) and project C (local fog volumes placed in the world editor) build on the grid defined here and are separate specs.

## Decisions

| Topic | Decision |
|---|---|
| Fog technique | Froxel pipeline: inject → temporal → integrate (compute shaders), then composite onto opaque geometry. |
| Low-end hardware | Froxels at every quality level; presets change grid size only. Quality 0 disables the volume, leaving analytic fog. |
| Forward surfaces | Water, particles and glass keep today's analytic fog (no noise, no shafts). |
| Wind scope | Fog only. Wind is published as global shader parameters for later foliage and particle use. |
| Wind controls | Compact set per profile: direction, speed, gustiness, noise amount, noise size. |
| Default fog | Density lowered to 0.0015 before this project (commit 1e0cea64). |

## 1. GPU compute foundation

New `GraphicsDevice` API. Every new virtual has a safe default body (return `nullptr` or no-op), so `GraphicsDeviceNull`, the Metal stub and tests build unchanged. Only `GraphicsDeviceD3D11` implements them.

### VolumeTexture

- **Location:** new class `VolumeTexture` in `graphics/volume_texture.h`; `Texture` stays 2D.
- **Creation:** `VolumeTexturePtr GraphicsDevice::CreateVolumeTexture(uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable)`.
  - `VolumeFormat::RGBA16F` holds scattering and extinction.
  - `VolumeFormat::R8` holds noise.
- **Methods:**
  - `void Bind(ShaderType stage, uint32 slot)` binds the SRV.
  - `void BindWritable(uint32 slot)` binds the UAV to the compute stage. Only valid for `writable` textures.
  - `void Upload(const uint8* data, size_t size)` is a CPU upload for R8 volumes. The size must equal `width*height*depth`.
  - `uint16 GetWidth() / GetHeight() / GetDepth() const`.
- **D3D11:** `VolumeTextureD3D11` wraps `ID3D11Texture3D` with an SRV and, when writable, a UAV.

### Compute shaders

- `GraphicsDevice::CreateShader(ShaderType::ComputeShader, …)` returns a new `ComputeShaderD3D11`. `Shader::Set()` binds it with `CSSetShader`.
- `void GraphicsDevice::Dispatch(uint32 x, uint32 y, uint32 z)`.
- `void GraphicsDevice::ClearComputeBindings()` nulls every compute SRV slot 0–15 and UAV slot 0–7, and unbinds the compute shader.
  - Every compute step calls it before a texture it wrote is read as input.
  - The fog pass calls it at the end of the pass.
- `ConstantBuffer::BindToStage(ShaderType::ComputeShader, slot)` already works. `Texture::Bind` and `GraphicsDevice::BindTexture` are extended to the compute stage if the D3D11 switch lacks it.

### Sampler states

- `SamplerStatePtr GraphicsDevice::CreateSamplerState(const SamplerDesc& desc)`.
  - `SamplerDesc`: `filter` (Point, Linear, LinearComparison), `address` (Clamp, Wrap, Border), `comparison` (LessEqual), `borderColor`.
- `void SamplerState::Bind(ShaderType stage, uint32 slot)`.
- `DeferredRenderer` switches its hand-built shadow comparison sampler to this API. The `bindShadowSampler` callback idiom is removed.

### Build

`src/shared/deferred_shading/CMakeLists.txt` globs `shaders/CS_*.hlsl` with these properties:
- `VS_SHADER_TYPE Compute`
- `VS_SHADER_MODEL 5.0`
- `VS_SHADER_ENTRYPOINT main`
- `VS_SHADER_OUTPUT_HEADER_FILE shaders/%(Filename).h`
- `VS_SHADER_VARIABLE_NAME g_%(Filename)`

The generated headers are added to `.gitignore` next to the existing ones.

### GPU timer

- A new mark "after volumetric fog" replaces "after atmosphere".
- The point count grows if needed, so frame timings list the fog cost separately.

## 2. Froxel fog pass

**Location:** `src/shared/deferred_shading/volumetric_fog_pass.h/.cpp` replaces `AtmospherePass`. It runs at the same point in `DeferredRenderer::Render`: after lighting, before the forward pass.

**Skip conditions:** it is skipped while submerged, with `Scene::IsFogEnabled()` false, or when the combined fog density is 0. The composite result is copied back exactly as today.

### Settings (`volumetric_fog_settings.h`, dependency-free, unit-tested)

| Quality | Tile size (px) | Depth slices | 1080p grid |
|---|---|---|---|
| 0 Off | – | – | volume disabled, analytic fog only |
| 1 Low | 24 | 32 | 80×45×32 |
| 2 Medium | 16 | 48 | 120×68×48 |
| 3 High (default) | 12 | 64 | 160×90×64 |
| 4 Ultra | 8 | 96 | 240×135×96 |

- **Grid width and height:** `ceil(renderWidth / tile)` and `ceil(renderHeight / tile)`.
- **Slice spacing:** exponential. Slice `s` of `N` sits at view depth `near · (far/near)^(s/N)`, with `near = 0.5 m` and `far = fogRange`.
  - `SliceToDepth` and `DepthToSlice` are exact inverses and tested.
- **Fog range:** default 200 m, clamped to [50, 300] (the CSM shadow range).
- **Jitter:** a Halton(2) sequence of 16 values in [0, 1), advanced every frame, offsets each cell's sample position along its depth slice.
- **Noise weighting:** `NoiseDensityFactor(n, amount) = max(0, 1 + amount · (2n − 1))`. It is shared by the CPU tests and mirrored in HLSL.

### Per-frame steps

Compute thread groups are 8×8×8.

**1. Inject — `CS_FogInject`**
- **Writes:** the current frame's `RGBA16F` volume: rgb = in-scattered radiance × σ, a = σ.
- **World position:** each cell centre (jittered in depth) is reconstructed from the inverse view-projection.
- **Density:**
  - σ = `FogDensityAt(y)` (the existing height fog, with its exponent clamp) × `NoiseDensityFactor(noise, amount)`.
  - `noise` = the R8 noise volume sampled with a wrap sampler at `worldPos / noiseSize + windOffset` (xz scrolled; y uses `worldPos.y / noiseSize`).
- **Light:** `FogTint + SunScatterColor · SunColor · SunIntensity · ShaftStrength · Phase(cosθ) · visibility`.
  - `visibility` is one comparison sample of the correct CSM cascade.
  - `Phase` is the existing lerp(1, Henyey-Greenstein, 0.8).

**2. Temporal — `CS_FogTemporal`**
- **Reprojection:** each cell's world position is transformed by the previous frame's view-projection into the previous grid's normalized uvw.
- **Blend:** result = lerp(current, history, 0.9) when uvw is inside [0, 1]³. Otherwise it is current.
- **History:** alternates between two volume textures.
- **History reset** (use current only):
  - first frame, resize, quality change or fog-range change
  - camera moved more than 50 m since the last frame
  - the pass was skipped last frame

**3. Integrate — `CS_FogIntegrate`**
- Front to back per column. For slice `s` with thickness `Δ = SliceToDepth(s+1) − SliceToDepth(s)`:
  - `T_slice = exp(−σ·Δ)`
  - `radiance = rgb / max(σ, 1e-6)` (inject stored radiance × σ)
  - `scattered += T_accumulated · radiance · (1 − T_slice)`
  - `T_accumulated *= T_slice`
- **Writes:** rgb = accumulated scattered light, a = accumulated transmittance.

**4. Composite — `PS_FogComposite`**
- **Inputs:** scene colour and G-buffer normal RT (a = radial depth, 0 = sky).
- **Lookup:** radial depth becomes view depth, then a slice coordinate via `DepthToSlice`. The integrated volume is sampled trilinearly with a clamp sampler.
- **Sky pixels** use depth 2000 m.
- **Beyond the fog range:** the existing closed-form `FogOpticalDepth` / `FogSource` integrate from `fogRange` to the pixel distance with the sun unshadowed. That result is attenuated by the volume's transmittance at `fogRange` and added.
  - Because the noise factor averages ≈ 1, brightness matches at the boundary.
- **Output:** `sceneColor · T + scattered`.
- **Debug:** `gxAtmosphereDebug` 1 = scattered light, 2 = transmittance, 3 = density at the pixel's slice.

### Constants

A new fog cbuffer at b2 in the fog shaders holds:
- grid dimensions, near, far, slice count
- jitter
- current inverse view-projection and previous view-projection
- wind offset (xz, wrapped), noise size, noise amount
- temporal blend and history-valid flag

The camera cbuffer b1 is **unchanged**, so no material rebuild is needed. CSM data comes from the existing `ShadowBuffer` (b3) and the cascade textures.

### Noise volume

- **Code:** `src/shared/deferred_shading/fog_noise.h/.cpp` has `std::vector<uint8> GenerateFogNoise(uint32 size, uint32 seed)`.
  - 64³, tiling (periodic lattice), 4-octave value/gradient FBM, normalized to [0, 255].
  - Deterministic for a given seed.
- **Upload:** generated once when the pass is created, uploaded to an R8 `VolumeTexture`.

### Cvars (client)

- `gxAtmosphereQuality` 0–4 (unchanged name). The Options menu entry is unchanged.
- `gxVolumetricFogRange` (new, default 200) replaces `gxAtmosphereMarchDistance`.
- `gxAtmosphereDebug` 0–3.

### Budget

- ≤ 2 ms at High and ≤ 0.7 ms at Low, 1080p, RTX 3070-class GPU, measured with the GPU timer overlay.

## 3. Wind and profile settings

### Profile fields

Added to both `environment_profiles.proto` copies with identical numbers:

| # | Field | Default | Clamp |
|---|---|---|---|
| 20 | `wind_direction` (degrees, clockwise from +Z; direction the wind blows toward) | 45 | wrapped to [0, 360) |
| 21 | `wind_speed` (m/s) | 3 | [0, 30] |
| 22 | `wind_gustiness` | 0.3 | [0, 1] |
| 23 | `fog_noise_amount` | 0.5 | [0, 1] |
| 24 | `fog_noise_size` (metres per noise tile) | 60 | [5, 500] |

**Runtime structs:**
- `EnvironmentProfile` gains `windDirectionDegrees`, `windSpeed`, `windGustiness`, `fogNoiseAmount`, `fogNoiseSize`.
- `EnvironmentState` stores the wind as a unit xz vector plus the other four values.

**Evaluate and blend:**
- `EvaluateEnvironment` converts degrees to the vector.
- `LerpEnvironment` lerps the vector and re-normalises it. If the blended length is below 1e-3 (opposite winds), it uses `b`'s direction. The four scalars lerp linearly.

**Tests:** the proto-default parity test covers fields 20–24.

### WindSimulation

**Location:** `src/shared/scene_graph/wind_simulation.h/.cpp` — plain logic, no graphics.

```cpp
struct WindState
{
	Vector3 direction;       // unit, y = 0
	float speed = 0.0f;      // m/s including gust
	float noiseOffsetX = 0;  // accumulated offset / noiseSize, wrapped to [0,1)
	float noiseOffsetZ = 0;
	float noiseSize = 60.0f;
	float noiseAmount = 0.5f;
};

class WindSimulation
{
public:
	void Update(float deltaSeconds, const EnvironmentState& environment);
	const WindState& GetState() const;
	void Reset();
};
```

- **Gust:** `g(t)` is a smooth 1D value noise over accumulated time `t` (a `double`, advanced by `deltaSeconds`; never an absolute clock), mapped to [-0.6, 0.6].
- **Speed:** `speed · (1 + gustiness · g(t))`.
- **Direction:** rotated by `±25° · gustiness · g(t + 17.3)`.
- **Offset:**
  - It accumulates in `double` noise tiles as `∫ direction·speed / noiseSize dt`, wrapped to [0, 1) every frame (`x − floor(x)`, also correct for negative offsets). It is continuous when direction, speed or noise size changes, and seamless because the noise tiles at 1.0.
  - Accumulating metres and dividing by the current noise size instead would move the pattern by `distance/sizeA − distance/sizeB` tiles whenever a blend changed the size — dozens of tiles after an hour of play.
  - `fog_noise_size` therefore steps once, at the midpoint of a profile blend, instead of being interpolated: the shader samples at `worldPos / noiseSize`, so a continuously changing size rescales the lookup and sweeps the pattern far from the world origin.
- **Scene:** the host (client `WorldState`, editor `WorldEditorInstance`) owns a `WindSimulation` next to its `EnvironmentController`. It updates it every frame after the environment and calls the new `Scene::SetWind(const WindState&)`. `Scene::GetWind()` is read by the fog pass.
- **Global shader parameters:** `WindDirection` (xyz direction, w = speed) is published for later foliage and particle use.
- **Editor with the sky paused:** the world editor advances wind with real frame time even when the sky clock is paused.

### Editor

- The Environment Profile Editor gets a **Wind & Noise** section: direction (0–360), speed (0–30), gustiness (0–1), noise amount (0–1), noise size (5–500).
- Each edit calls `EnvironmentPreview::NotifyChanged()`, so world editors update live.

## 4. Removal, documentation, testing

### Removed

- `AtmospherePass`, `atmosphere_pass_settings.h`, `PS_AtmosphereMarch.hlsl`, `PS_AtmosphereBlur.hlsl`, `PS_AtmosphereComposite.hlsl`, and their generated headers.
- The `gxAtmosphereMarchDistance` cvar.
- `DeferredRenderer::SetAtmosphereMarchDistance`, replaced by `SetVolumetricFogRange`.

`AtmosphereCommon.hlsli` stays; fog shaders include it. The three formula copies (hlsli, material compiler, `atmosphere_math.h`) remain in sync.

### Docs

- `docs/rendering-atmosphere.md` is rewritten to cover the froxel pipeline and frame order, the quality table, wind and noise fields, cvars, and known limitations:
  - no fog volume on forward surfaces
  - no occlusion beyond 300 m
  - point/spot lights and local volumes are pending (B, C)
- `docs/console_commands.md` is updated.

### Unit tests

- **`deferred_shading_tests`** (headless):
  - grid size per preset and resolution
  - slice↔depth round trip and monotonicity
  - Halton jitter in [0, 1)
  - `NoiseDensityFactor` mean ≈ 1 over uniform n, and zero at amount 1, n = 0
  - `GenerateFogNoise` is deterministic for a seed and has range [0, 255] with a mean in [110, 145]
  - it tiles: along each axis, the largest absolute difference between the voxel at index `size−1` and the voxel at index 0 is at most 1.5× the largest difference between any two adjacent interior voxels on that axis
- **`scene_graph_tests`:**
  - `WindSimulation`:
    - zero speed keeps a constant offset
    - offset is continuous (no jump > speed·dt) across direction and speed changes
    - gust speed stays within `speed · [1 − 0.6g, 1 + 0.6g]`
    - the wrapped offset stays in [0, 1)
  - wind direction blend: 350°↔10° passes through 0°, and opposite winds fall back to the incoming direction
  - wind fields load and clamp
- **`client_data_tests`:** proto defaults for fields 20–24 equal the runtime defaults.
- **Null device:** `CreateVolumeTexture` returns nullptr, and `Dispatch`, `ClearComputeBindings` and `CreateSamplerState` are safe.

### Visual verification

This is controller-driven, and only while the user is away from the PC.

**Client:**
- drifting banks and gaps at noon, dusk and night
- shafts through trees and hills
- no visible seam at the fog range
- no ghosting or swimming when turning or walking
- a teleport resets history
- each quality preset
- debug views 1–3

**Editor:** wind sliders update the viewport live.

**Perf:** High and Low numbers from the GPU timer overlay against the budget.

**Gate:** `/gate` green before `/ship`.

## Out of scope

- Point/spot light scattering (project B).
- Local fog volumes (project C).
- Fog volume or shafts on forward surfaces.
- Foliage and particle wind.
- Metal compute backend.
- Occlusion beyond the 300 m CSM range.

## Implementation Notes

Implemented 2026-09-14 on `feature/volumetric-atmosphere`, commits 52ed80a5..332a1d7d. Plan: `docs/superpowers/plans/2026-09-14-froxel-volumetric-fog.md`.

1. **Default fog density.** Lowered from 0.004 to 0.0015 before this project (commit 1e0cea64), because the user found the default "extremely foggy".
2. **Gust formula.** `WindSimulation` uses one normalized gust noise `n ∈ [-1, 1]` for speed (`speed · (1 + gustiness · 0.6 · n)`) and one for direction (`±25° · gustiness · n`). The spec text mixed the two scales; this matches Section 3's prose ("up to ±25° · gustiness").
3. **Shared-state fixes after review.**
   - `GraphicsDeviceD3D11::Reset()` now also resets the cached compute shader.
   - The fog composite sets the device's legacy address/filter state (Clamp/Bilinear) so it agrees with the explicit linear-clamp sampler. The legacy state matters because `Draw` rebinds PS s0 from it.
   - The fog range is clamped when uploaded.
   - `constant_buffer.h` includes `shader_base.h`.
   - `GraphicsDeviceD3D11::BindTexture(nullptr, …)` only clears the device cache and does not unbind on the GPU. This is pre-existing behaviour.
4. **How the look was checked.**
   - **Console:** injected console input did not take effect in these runs, so debug views and the perf overlay were enabled by temporarily editing `bin/Debug/config/Config.cfg` (backed up and restored).
   - **Time of day:** the world server's game clock is the UTC time of day, so the evening test ran at night. Daytime was checked with a temporary, uncommitted client override to 13:00 (reverted and rebuilt).
   - **Results:** no black, NaN or white frames at night or at noon; thin haze with sun shafts through the trees at noon; the density debug view varies with depth and over time.
   - **Wind drift:** subtle at noon with the default noise amount of 0.5.
5. **Performance.** Not measured: the Debug perf overlay's GPU rows read 0.0. The budget (≤ 2 ms High, ≤ 0.7 ms Low) remains unverified.
6. **VRAM.** The Ultra preset uses about 400 MB at 4K (four RGBA16F volumes of 480×270×96). High at 1080p uses about 30 MB.
