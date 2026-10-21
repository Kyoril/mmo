# Atmosphere: Volumetric Fog, Light Shafts, Bloom

Designs: [froxel volumetric fog](superpowers/specs/2026-09-14-froxel-volumetric-fog-design.md),
[zone environment profiles](superpowers/specs/2026-09-14-zone-environment-profiles-design.md),
[original atmosphere](superpowers/specs/2026-09-13-volumetric-atmosphere-design.md)

## Frame order (DeferredRenderer::Render)

1. Cascaded shadow maps → G-Buffer → SSAO → contact shadows
2. Lighting (`PS_DeferredLighting`) — **linear HDR**, no fog, no tonemap
3. `VolumetricFogPass`
   - **Compute steps:**
     1. `CS_FogInject`: height fog × wind noise per froxel, into two volumes: fog ambient + point/spot lights, and the shadowed sun *without* its phase function.
     2. `CS_FogTemporal`: blend the ambient/light volume with last frame's grid, reprojected through the previous camera (Catmull-Rom across the screen axes). The sun volume skips this step.
     3. `CS_FogIntegrate`: applies the sun phase for this frame's view rays, then accumulates front to back.
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

**Sun shafts (no history):**
- The sun's shadow is box-filtered over the whole cell every frame on a fixed tap pattern (`sunTapsXY`² × `sunTapsZ`: 2×2×4 Low, 2×2×6 Medium/High/Ultra), with one cascade per cell and the taps stepped affinely in light space. The zone fog density (height falloff × wind noise) is evaluated at the same taps, so the cell holds mean(visibility × density): one noise sample per cell slid through noise features smaller than the cell as the camera moved, and the shafts showed it. Local fog volumes use the cell centre × mean visibility.
- The sun volume gets no history and no jitter, so a shaft depends only on where the cell is this frame: it cannot lag behind the camera, and it changes only as fast as the cell actually slides over the shadow.
- Why: a single shadow tap per cell resolves canopy gaps far finer than the grid, so which cells read "lit" depends on exactly where the sample lands, and every camera move moves the grid (flicker). Jitter plus history hid that at rest but smeared and lagged in motion. Measured on the canopy scene (2026-10-19): frame-to-frame change not explained by the true image change drops ~3× against one tap per cell (strafe 0.0028 → 0.0009, forward 0.0023 → 0.0007, orbit 0.0009 → 0.00045); beyond 2×2×6 taps returns are small. A tent-shaped ±1 cell footprint was tried and is blurrier without being steadier. What remains is the grid resolution itself (Ultra halves it).
- Cost control: CS_FogDepthTiles first stores the farthest depth seen through each grid column; cells starting behind the farthest depth of their 3×3 column neighbourhood (everything the composite's bilinear lookup can reach) skip the shadow taps, losslessly. Doing that gather inside CS_FogInject instead cost more than it saved (it repeats per 8-slice block and contends on group-shared atomics).
- Release, 1080p, canopy scene (2026-10-19): fog pass 0.25 ms Low, 0.34 ms Medium, 0.52 ms High (0.36 ms with one tap per cell), ~1.2 ms Ultra.

**Fog shadow map (band-limited on purpose):**
- The taps sample the fog's own shadow map (`DeferredRenderer::FogShadowMapSize`, 256²), placed and texel-snapped like a cascade over the whole fog range: texels of roughly 1-2 m. Every tap is a tent filter over 4×4 of those texels (four bilinear comparisons one texel apart), and a cell takes enough taps along its depth that consecutive taps land about one texel apart (`volumetric_fog::FogShadowDepthTaps`, 6 to 16).
- Why: the shaft flicker that remained while walking toward a low sun was aliasing along depth. A far froxel is a needle several metres long, and its few depth taps skipped across shadow edges as the cells slid through the world, so a shaft's brightness went back and forth from frame to frame. Measured in the client (2026-10-19, fog-only view, correlation of consecutive frame differences; negative = back and forth): 16 depth taps removed it on a 256 map, while 4×4 taps across the cell changed nothing; on a 1024 map even 16 depth taps did not, because canopy detail that fine cannot be sampled by any affordable tap count. The fog shadow map's texel snaps were ruled out (shaft jumps did not coincide with them), as were camera judder and cubic depth slicing (worse).
- The result is the soft, wide-beam look: individual leaf gaps no longer reach the fog.

**Sun shafts, temporal alternative (`gxFogSunMode` 1 / 2, off by default):**
- One shadow tap per cell instead of the box filter, at a per-froxel offset read from a 64² blue noise rank map (`GenerateBlueNoise`, void-and-cluster) and advanced by the golden ratio every frame, so neighbouring froxels sample different parts of their cells in the same frame instead of the whole grid shifting in step. Both volumes then keep 0.95 of a history that is first clamped to the min/max of this frame's 3×3×3 neighbourhood, which pulls back history the scene has moved away from. Mode 1 shadows from the coarse fog shadow map, mode 2 from the surface cascades.
- Measured against the box filter with `gxVolumetricFogHistory 0` (2026-10-19, `graphics_test --fog-repro`, 1080p High, canopy and forest scenes): the converged image of mode 1 matches the box filter (mean difference 0.0003); sub-centimetre steps change the image slightly more (canopy strafe 0.000116 vs 0.000096, forward 0.000070 vs 0.000033); a still camera keeps a faint residual noise (0.00006, the box filter has none); at walking speed (0.1 m/frame) the frame-to-frame change equals the box filter's, and the image trails its converged value by 0.0003–0.0009 (the box filter cannot trail). Mode 2 gives visibly sharper shafts (it resolves the cascades' detail) with about twice mode 1's noise. Fog pass 2.42 ms (mode 1) vs 2.61 ms (box) in the harness: the 27-tap clamp costs most of what the single shadow tap saves.
- In the harness the box filter is therefore at least as stable. In the client both modes also reduced the walking flicker of the old 1024 fog shadow map, but the band-limited box filter above fixed it; the modes stay as a switch. Mode 2 dithers each froxel's cascade choice over a band of ±10 % of its depth around every split (and fades to lit across the end of the last cascade), so a split no longer flips the shafts behind it from one cascade to the next in one frame while walking.

**Jitter and history (ambient fog and point/spot lights):**
- Each frame samples every cell at a Halton-jittered position inside it: depth (base 2) and across the cell on screen (bases 3 and 5).
- The temporal step keeps `gxVolumetricFogHistory` (default 0, i.e. off) of the reprojected history, which removes the jitter noise. The history is read with a Catmull-Rom filter; a bilinear read blurs it a little every frame the camera moves, and at 0.9 that compounds into soft shafts.
- **Nothing view-dependent may go into the history.** The sun's phase function depends on the view direction, so it is applied in `CS_FogIntegrate` after the blend. When it was baked into the volume, the history carried the sun glow and shafts as seen from where the camera had been ~10 frames earlier, and they visibly trailed every camera move (fixed 2026-09-26). Point and spot lights still carry their phase in the history (they would need per-light storage); for them it only matters close to the camera.
- With `gxVolumetricFogHistory 0` the jitter is off too, so every frame is a pure function of the camera pose.
- History resets on resize, quality or range change, a camera jump over 50 m, or a frame without fog.

**Reproducing motion artefacts:** `graphics_test --fog-repro` (run from `data/client`) renders a low sun through a row of pillars along a scripted camera path and writes every frame as raw RGBA16F to `--out=<dir>`. `--settle=N` holds each pose N frames (converged reference); `--tile=4 --slices=128` renders a fine-grid ground truth; `--motion=strafe|forward|yaw|orbit`, `--scene=pillars|canopy` (canopy: a tree crown with many small gaps), `--suntaps=N --suntapsz=N`, `--nofog --shadowq=0..2 --shadowsize=N` (surface shadows alone), `--step`, `--history`, `--aniso`, `--debug`, `--nojitter` vary the rest; `--perf` (Release) averages the GPU pass timings into `perf.txt`. Comparing a motion run with a ground truth rendered at the same poses measures what the camera motion costs; that comparison penalises any filter softer than the ground truth whenever edges move, so judge stability with tiny steps instead (`--step=0.004`: the true image barely changes, and the frame-to-frame change is the instability). Each frame is 16 MB - delete the dumps after scoring.

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

**Distance fog:** after the height fog, the colour is blended toward the `distance_fog` curve colour by `opacity · saturate((d − distance_fog_start) / (distance_fog_end − distance_fog_start))`, where d is the distance from the camera and opacity the curve's alpha. It makes far geometry vanish regardless of height (the height fog thins above its base, so distant mountains never disappear in it alone). Sky pixels get `opacity · (1 − smoothstep(0, distance_fog_sky_band, ray.y))`, so fogged terrain meets a horizon of the same colour and the sky above stays visible. Off while end ≤ start (the default). It is applied in `PS_FogComposite` and the forward materials, not inside the froxel volume, and the atmosphere pass now also runs when only the distance fog is active.

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
- **Shapes:** box or ellipsoid (inscribed into the box), with a centre, full size (edge length / diameter) per axis and a yaw around +Y.
- **Fields:** density (peak, 0-1), colour (tint, 0-2 per channel), edge fade (fraction of the shape over which density fades to 0 with a smoothstep), height falloff (`exp(-falloff * height above the volume floor)`), active hours `from`/`to` plus fade hours (`from == to` is always on), noise amount and noise detail (1, 2 or 4 times the zone noise frequency; the pattern scrolls with the zone wind).
- **Storage:** one `Worlds/<dir>/<dir>.hfog` chunked binary file per map (`game_common/world_fog_volumes.h`), sanitized on load.
- **Per frame:** the client and editor select up to 64 volumes (`fog_volume::MaxVolumesPerFrame`) via `SelectFogVolumes`, which scales their density by the time-of-day factor, then hand the list to `DeferredRenderer::SetFogVolumes`. `VolumetricFogPass` uploads them as 80-byte `GpuFogVolume` records into a structured buffer bound at CS `t10`; `FogVolumeCount` in the fog cbuffer (256 bytes) says how many are valid.
- **Culling:** each 8x8x8 inject group culls the frame's volumes against its block's bounding sphere (volume reach = length of the half-size) into a 16-entry group-shared list (`fog_volume::MaxVolumesPerBlock`), in the same parallel pass as the light culling. A block reached by more volumes drops the rest.
- **Density and colour:** a froxel adds each volume's `density * edgeFade * heightFactor * noiseFactor` to the zone fog's sigma for extinction, and the same value times the volume colour for scattering: output rgb = `radiance * (sigma + sum(sigma_v * color_v))`, a = `sigma + sum(sigma_v)`.
- **Lighting:** shared with the zone fog (fog ambient, shadowed sun, point and spot lights); the colour only tints what the volume scatters.
- **Range:** volumes exist only inside the froxel grid (`gxVolumetricFogRange`). Beyond it and with the volume off (quality 0) only the closed-form zone fog remains, so a volume far away is invisible.
- Debug view 3 (density) includes the volumes.
- The math lives in `deferred_shading/fog_volume_math.h` (unit-tested) and is mirrored in `shaders/FogVolumeCommon.hlsli`; `GpuFogVolume` mirrors `struct FogVolume` there.

**Formula copies:** the closed-form formulas exist three times and must change together: `shaders/AtmosphereCommon.hlsli`, the forward fog emitted by `MaterialCompilerD3D11`, and `deferred_shading/atmosphere_math.h`. The camera cbuffer (b1, 208 bytes) is declared three times as well; changing it requires **Tools → Rebuild All Materials**.

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
| `distance_fog` | colour far geometry fades into (unauthored = follows `sky_horizon`) | opacity at the end distance |

**Fixed values:**
- distance fog: start and end distance (metres; off while end ≤ start), sky band (view-ray Y up to which the sky is fogged, 0 = sky untouched). Blending into or out of a profile without distance fog pushes the fog range out to 100 km instead of popping it.
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
| `gxVolumetricFogHistory` | 0 | history weight of the ambient fog and point/spot lights (0–0.95; 0 also disables the jitter). The sun shafts never use history |
| `gxFogSunMode` | 0 | sun filtering: 0 box filter (above), 1 temporal (one blue-noise jittered tap per cell, neighbourhood-clamped 0.95 history, fog shadow map), 2 temporal with the surface cascades. 1 and 2 also run the ambient fog at 0.95 history with the same clamp and jitter |
| `gxAtmosphereDebug` | 0 | 1 scattered light, 2 transmittance, 3 fog density, 4 lights per fog block |
| `gxBloomQuality` | 2 | 0 Off, 1 Low, 2 High |
| `gxExposure` | 1.0 | player brightness, multiplies the profile exposure |

## Sun shadow stability (cascaded shadow maps)

The shafts can only be as steady as the shadow maps they sample, so the same rules hold for both:

- **Every cascade snaps on its own texel grid.** Cascades past `CascadedShadowConfig::fullResolutionCascades` render at half size; the snap and the renderer both read `GetCascadeMapSize`/`GetCascadeTexelSize`. Snapping a half-size cascade on the full-size grid moved it by half a texel whenever the camera moved, re-rasterising every shadow edge (crawling edges, thin light gaps opening and closing). Low and Medium keep only cascade 0 at full size, so there cascade 1 - the ground right in front of the player - was affected (fixed 2026-10-19; High had it on cascades 2 and 3).
- **The surface shadow filter is deterministic.** `PS_DeferredLighting` uses an n×n tent PCF (3×3 Low, 5×5 Medium, 7×7 High) built from (n+1)²/4 bilinear comparison taps whose weights reproduce each texel's trapezoid weight exactly, so the filtered shadow is a fixed, continuous function of world position. The former Poisson kernel was rotated by a hash of the pixel's shadow-map UV - per-pixel noise that re-randomised whenever the cascade snapped. The shadow sampler is `ComparisonLinear` (the tent relies on exact 2×2 texel weights; anisotropic comparison under `SampleCmpLevelZero` is driver-defined). The sky profile's shadow softness widens the tent beyond one texel per filter texel.
- **No shadow boundary may move with the player.** Shadow casters ignore rendering distances (`MovableObject::ClearRenderingDistanceCull`): every placed world entity has a 256 m rendering distance, and measured from any moving camera that cut-off swept through the forest as the player walked - a low sun throws a tree's shadow 150-250 m, so distant shafts popped on every step while turning stayed calm (2026-10-19; first from the cascade camera, which held almost no trees in the far cascades, then from the view camera). The cascades instead reach toward the light as far as a 50 m caster's shadow can (`CascadedShadowConfig::GetLightwardReach`: casterHeight / tan(elevation), at most 800 m); the old `2r + 100` put the nearest cascade's near plane ~140 m toward the sun, where it sliced distant trees as it slid along. Unchanged at noon; it only changes on light-direction steps.
- **The light direction is quantised** (cvar `ShadowLightStep`, `CascadedShadowConfig::lightStepDegrees`), and all cascades refresh together on a step.

Measured with `graphics_test --fog-repro --nofog --step=0.004` (sub-centimetre camera steps, 1024 maps at Low): frame-to-frame instability −24 % strafing and −44 % walking forward (99.9th percentile halved), against the old filter and snap. The mean includes the true image change of every edge in view, so the shadow-only gain is larger.

## Known limitations

- Water, particles and glass get closed-form fog only: no noise, no shafts.
- Shafts end at the 300 m shadow range; mountains farther away cannot block the sun.
- Point and spot lights scatter without shadows (see above). Local fog volumes are planned as project C.
- Bloom strength is the environment profile's bloom intensity divided by the number of bloom levels.
- Debug views are composited before bloom and tone mapping, so they appear tone-mapped.
- Debug view 3 (density) reads black at quality 0: there is no grid volume to sample, so `DensityVolume` is never bound.
- Debug view 4 (lights per fog block) shows only the analytic fog at quality 0: there is no froxel volume to cull lights into.
