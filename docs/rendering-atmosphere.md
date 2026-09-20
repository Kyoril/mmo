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
   - **Skipped** while submerged, with `Scene::IsFogEnabled()` false, when the combined fog density is zero, or without a shadow sampler object. The history is then discarded.
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
- `WindSimulation` adds gusts (speed ±60% · gustiness, direction ±25° · gustiness) and integrates the noise offset in double precision, in noise tiles rather than metres, so a profile blend that changes the noise size never jumps the pattern. The noise size itself steps at the midpoint of a blend instead of interpolating.
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
- Debug view 3 (density) reads black at quality 0: there is no grid volume to sample, so `DensityVolume` is never bound.
