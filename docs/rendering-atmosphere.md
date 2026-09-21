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
6. `TonemapPass` — scene + bloom, exposure, ACES, gamma, colour grading (sliders then zone LUT cross-fade, see [docs/color-grading.md](color-grading.md)), dither
7. `PostProcessPass` (underwater only)

## The linear-HDR contract

Nothing before the TonemapPass may tone map or gamma-encode. Forward materials rendered outside
DeferredRenderer (editor previews, model frames, the minimap baker) keep tone mapping in the material
because they never set `forwardOutputLinear`. Material graphs sampling Scene Color / SSR receive a
display-referred sample through the generated `LoadSceneColor` helper.

Both conversions - `InverseTonemap` for unlit forward materials and `LoadSceneColor` for scene-colour
samples - are only the inverse of the TonemapPass when they use the same exposure, so the scene
publishes it as `forwardExposure` in the camera constants (`Scene::SetForwardExposure`, set by
DeferredRenderer around its forward pass; 1 everywhere else). Changing the generated shader code
requires **Tools > Rebuild All Materials** in the editor.

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
- Density `σ(y) = density · densityMultiplier · exp(min(−fog_height_falloff · (y − base), 3))`, with `base = fog_base_height`.
- `fog_base_height` is an absolute world Y, so a fog bank stays where it was authored: climb above it and it is below you. A zone whose terrain sits far from Y = 0 must author its own base, or it gets the saturated density (about 20×) that applies below the base.
- Inside the grid, σ is multiplied by `max(0, 1 + fog_noise_amount · (2n − 1))`, where n samples a 64³ tiling noise volume (`fog_noise.cpp`) at `worldPos / fog_noise_size − windOffset`.
- That factor averages 1, so the smooth fog beyond the grid matches the grid's brightness.

**Scattered light:** per metre, the fog scatters `σ · (FogTint + SunScatterColor · SunColor · SunIntensity · shaft_strength · Phase(cosθ) · shadow)` toward the camera. Phase is Henyey-Greenstein (g = `fog_anisotropy`) blended 80/20 with isotropic.

### Point and spot lights

- Every point and spot light the deferred renderer gathers for the frame also scatters into the froxel fog; directional lights do not (the sun has its own term above).
- Per metre a light adds `σ · Color · Intensity · attenuation · cone · fogScattering · light_scattering · Phase(cosθ)`, with θ between the view ray and the light's travel direction.
- `fogScattering` is per light: world model lights author it in the editor, spells and projectiles carry `fog_scattering`. 0 keeps a light out of the fog entirely.
- `light_scattering` is the zone multiplier from the environment profile (`DeferredRenderer::SetFogLightScattering`, clamped to 0–8).
- Culling: `CS_FogInject` runs 8×8×8 thread groups. Each group culls the frame's lights once against its block's bounding sphere into a 64-entry group-shared list (`light_math::MaxLightsPerFogBlock`); a block reached by more lights drops the rest and shows red in debug view 4.
- Attenuation and the spot cone are shared with the lighting pass through `shaders/LightCommon.hlsli`, which mirrors the unit-tested `scene_graph/light_math.h` (including `SpotConeIntersectsSphere`).
- Lights are unshadowed in the fog: a light inside a building bleeds through its walls, so interior lights should use `fogScattering` 0.
- The temporal blend smears fast-moving lights (projectiles) into short trails.

### Local fog volumes

- Authored, client-only fog placed in the world editor: valley mist, swamp haze, a crypt's floor fog. Purely visual; the server never sees them.
- **Shapes:** box or ellipsoid (inscribed into the box), with a centre, half-size per axis and a yaw around +Y.
- **Fields:** density (peak, 0-1), colour (tint, 0-2 per channel), edge fade (fraction of the shape over which density fades to 0 with a smoothstep), height falloff (`exp(-falloff * height above the volume floor)`), active hours `from`/`to` plus fade hours (`from == to` is always on), noise amount and noise detail (1, 2 or 4 times the zone noise frequency; the pattern scrolls with the zone wind).
- **Storage:** one `Worlds/<dir>/<dir>.hfog` chunked binary file per map (`game_common/world_fog_volumes.h`), sanitized on load.
- **Per frame:** the renderer selects up to 64 volumes (`fog_volume::MaxVolumesPerFrame`) and scales their density by the time-of-day factor, then hands them to `DeferredRenderer::SetFogVolumes`. `VolumetricFogPass` uploads them as 80-byte `GpuFogVolume` records into a structured buffer bound at CS `t10`; `FogVolumeCount` in the fog cbuffer (256 bytes) says how many are valid.
- **Culling:** each 8x8x8 inject group culls the frame's volumes against its block's bounding sphere (volume reach = length of the half-size) into a 16-entry group-shared list (`fog_volume::MaxVolumesPerBlock`), in the same parallel pass as the light culling. A block reached by more volumes drops the rest.
- **Density and colour:** a froxel adds each volume's `density * edgeFade * heightFactor * noiseFactor` to the zone fog's sigma for extinction, and the same value times the volume colour for scattering: output rgb = `radiance * (sigma + sum(sigma_v * color_v))`, a = `sigma + sum(sigma_v)`.
- **Lighting:** shared with the zone fog (fog ambient, shadowed sun, point and spot lights); the colour only tints what the volume scatters.
- **Range:** volumes exist only inside the froxel grid (`gxVolumetricFogRange`). Beyond it and with the volume off (quality 0) only the closed-form zone fog remains, so a volume far away is invisible.
- Debug view 3 (density) includes the volumes.
- The math lives in `deferred_shading/fog_volume_math.h` (unit-tested) and is mirrored in `shaders/FogVolumeCommon.hlsli`; `GpuFogVolume` mirrors `struct FogVolume` there.

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
- fog: density, height falloff, base height, anisotropy, light scattering (zone multiplier on point/spot light scattering in the fog, `DeferredRenderer::SetFogLightScattering`, 0–8)
- light: shaft strength, exposure, bloom intensity, bloom threshold
- blending: transition seconds
- wind and noise: wind direction (degrees clockwise from +Z, direction the wind blows toward), wind speed (m/s), gustiness, fog noise amount, fog noise size (metres)
- colour grading (see [docs/color-grading.md](color-grading.md)): `color_lut` (strip LUT texture path, empty = none), `saturation`, `contrast`, `color_filter_r/g/b` (all [0, 2], default 1); applied in the TonemapPass after ACES and gamma. `saturation`, `contrast` and `color_filter_r/g/b` blend like the other fixed values; `color_lut` instead cross-fades between the new zone's LUT and the one being faded out (see color-grading.md)

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
| `gxAtmosphereDebug` | 0 | 1 scattered light, 2 transmittance, 3 fog density, 4 lights per fog block |
| `gxBloomQuality` | 2 | 0 Off, 1 Low, 2 High |
| `gxExposure` | 1.0 | player brightness, multiplies the profile exposure |

## Known limitations

- Water, particles and glass get closed-form fog only: no noise, no shafts.
- Shafts end at the 300 m shadow range; mountains farther away cannot block the sun.
- Point and spot lights scatter without shadows (see above). Local fog volumes are planned as project C.
- Bloom strength is the environment profile's bloom intensity divided by the number of bloom levels.
- Debug views are composited before bloom and tone mapping, so they appear tone-mapped.
- Debug view 3 (density) reads black at quality 0: there is no grid volume to sample, so `DensityVolume` is never bound.
- Debug view 4 (lights per fog block) shows only the analytic fog at quality 0: there is no froxel volume to cull lights into.
