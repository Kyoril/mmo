# Volumetric Atmosphere: Height Fog, Light Shafts, Bloom — Design

Date: 2026-09-13
Branch: `feature/volumetric-atmosphere`
Status: approved design, pending implementation plan

## Goal

Give the world a painterly forest atmosphere: height fog that pools in low ground and glows
toward the sun, shadowed light shafts through canopies, and soft bloom around bright openings.
The reference is a stylized forest shot with a low sun behind trees.

Scope of this project: a linear-HDR frame with a final tonemap pass, analytic height fog,
ray-marched shadowed light shafts, and bloom. D3D11 only (the Metal backend is a stub).

Out of scope: froxel/compute volumetrics, local (point/spot) lights scattering in fog, shafts
over forward/translucent surfaces, animated fog noise, per-zone atmosphere values (a later
per-area time-of-day project will override what `SkyComponent` writes).

## Current State (as of 2026-09-13)

- `PS_DeferredLighting.hlsl:580-589` applies linear distance fog (`FogStart`/`FogEnd`/`FogColor`
  in camera cbuffer b1), then ACES, then gamma. The RGBA16F lighting target therefore holds
  display-referred colour.
- `MaterialCompilerD3D11` (`material_compiler_d3d11.cpp:1690-1732`) repeats the fog and tonemap
  for lit forward materials; unlit forward materials output `pow(baseColor, 2.2)` as
  display-referred colour, fogged toward `pow(ACES(fogColor), 1/2.2)`.
- Client fog range is hard-coded 60-500 m (`world_state.cpp:1433`); fog colour follows the
  horizon curve (`sky_component.cpp:329`).
- God rays exist only underwater (`PS_Underwater.hlsl:240-285`, radial blur, display space).
- No bloom, exposure, TAA, compute shaders or 3D textures.
- CSM: 4 cascades, 300 m, cascade matrices/splits in `ShadowBuffer` (b3), maps at t5-t8,
  comparison sampler at s1 — all private to `DeferredRenderer`. Distant cascades refresh
  on a 2-3 frame stagger.

## Section 1 — Frame Pipeline and Linear HDR

New order inside `DeferredRenderer::Render` (★ = new or changed):

1. Shadows (CSM) → G-Buffer → SSAO → contact shadows. Unchanged.
2. ★ **Lighting** outputs **linear HDR**: no fog, no ACES, no gamma.
3. ★ **AtmospherePass** (Section 2) composites fog and shafts onto the lit opaque scene.
4. Scene colour copy for refraction (t14). Unchanged; now holds fogged linear HDR.
5. ★ **Forward pass.**
   - Lit forward materials output linear HDR with the same analytic height fog as the
     atmosphere pass (shared `AtmosphereCommon.hlsli`, emitted into generated material HLSL).
     No tonemap.
   - Unlit forward materials output `InverseTonemap(color)` — the exact analytic inverse of
     gamma∘ACES, with the input clamped to 0.999 — so after the final tonemap they land on the
     same display value as today. The analytic height fog is then applied to that linear value
     exactly as for lit materials (`L · T + inscatter`), so with fog disabled the output is
     unchanged and with fog enabled unlit effects fog out to the same colour as the scene.
6. ★ **BloomPass** (Section 3).
7. ★ **TonemapPass**: `color *= Exposure` → ACES → gamma → triangular dither of ±½ an 8-bit
   step. Output is what `GetFinalRenderTarget()` returns when not submerged.
8. Underwater `PostProcessPass` runs after the TonemapPass, on display-referred input as today,
   so its thresholds and tuning remain valid. AtmospherePass and BloomPass are skipped while
   submerged.

Contract: **nothing before the TonemapPass may tonemap or gamma-encode.**

Regression guarantee: with `gxAtmosphereQuality 0`, fog disabled, `gxBloomQuality 0` and
`gxExposure 1`, the frame must match develop within dither noise. This is checkpoint 1.

Legacy linear fog (`FogStart`/`FogEnd`) is removed. `Scene::SetFogEnabled` remains and gates
the whole atmosphere (analytic and marched). `Scene::SetFogRange` is removed with its callers
(`world_state.cpp`, editor world settings).

All `DeferredRenderer` users (client, world editor, world model editor, material instance
editor, spell visualization preview, graphics_test) inherit the change. Material bytecode
changes, so all materials must be rebuilt with the editor's Rebuild All Materials tool and the
rebuilt data committed.

## Section 2 — AtmospherePass (Height Fog + Light Shafts)

New class `AtmospherePass` in `src/shared/deferred_shading/` following the `SsaoPass` /
`ContactShadowPass` pattern (settings struct with `ApplyQualityLevel`, owned targets,
full-screen quad with `VS_DeferredLighting`).

### Inputs

- G-Buffer normal RT alpha: linear radial depth.
- CSM maps, `ShadowBuffer` (b3) and the comparison sampler, rebound by `DeferredRenderer`
  (they stay private; the pass receives them via its `Render` signature).
- Lit HDR scene (`m_renderTexture`).
- New `AtmosphereBuffer` cbuffer: density, height falloff, base height, anisotropy, shaft
  strength, march distance, step count, fog tint and density multiplier, sun scatter colour and
  strength multiplier, ambient colour, sun direction (toward the sun), camera position,
  inverse view and inverse projection, target texel size, debug mode.

### Density model

Exponential height fog: `σ(y) = Density · exp(−HeightFalloff · (y − BaseHeight))`.

### Per-pixel march (reduced-resolution RGBA16F target; rgb = inscatter, a = transmittance)

1. Ray length `L = min(sceneDepth, MarchDistance)`. `MarchDistance` is clamped to the CSM max
   distance (300 m). Sky pixels (detected the same way `PS_Underwater.hlsl` computes `isSky`)
   use `MarchDistance` so canopy silhouettes against the sky still produce shafts.
2. `N` steps (from quality level), quadratically distributed (`t_i = L · ((i + j) / N)²`) with a
   per-pixel start offset `j` from interleaved gradient noise. The noise is static — no
   per-frame variation, because nothing accumulates it.
3. Per step:
   - Shadow: one hardware-PCF tap in the cascade that covers the step's distance.
   - Inscatter: `σ · (SunScatterColor · SunIntensity · shadow · Phase(cosθ) · ShaftStrength
     + Ambient · FogTint · (1/4π))`.
   - `Phase` = `lerp(isotropic, HenyeyGreenstein(g = Anisotropy), 0.8)`.
   - Transmittance per segment via the analytic height-fog optical depth between the segment
     endpoints (not `σ · Δt`), so step count does not change total fog amount.
4. Tail beyond `L` (up to `sceneDepth`, or a sky distance of 2000 m for sky pixels): the
   closed-form height-fog integral with shadow = 1, so near and far fog join without a seam
   and fog continues past shadow range.

### Blur and upsample

- Separable depth-aware bilateral blur at reduced resolution, 7 taps, 1 or 2 iterations.
- Composite at full resolution: 4-tap bilateral upsample choosing weights by depth similarity
  (nearest-depth fallback), then `scene · transmittance + inscatter`.

### Quality levels (`gxAtmosphereQuality`)

| Level | Resolution | Steps | Blur iterations |
|---|---|---|---|
| 0 Off | – | – | – (analytic fog still applied in the composite/lighting) |
| 1 Low | ¼ | 12 | 1 |
| 2 Medium | ½ | 24 | 1 |
| 3 High (default) | ½ | 48 | 2 |
| 4 Ultra | full | 64 | 2 |

At quality 0, a full-resolution analytic-only composite still runs (cheap, no shadow taps),
so height fog and sun glow remain.

Budget estimate: ~1.5-2 ms at High, 1080p, RTX 3070 class.

## Section 3 — BloomPass

New class `BloomPass` in `src/shared/deferred_shading/`.

- Input: linear HDR scene after the forward pass.
- Downsample chain starting at ½ (or ¼) resolution: 13-tap filter per level; the first level
  uses a Karis (luminance-weighted) average and applies a soft-knee threshold
  (`Threshold` default 0.8 linear, knee 0.5).
- Upsample chain: 3×3 tent filter, additive.
- Composite: `scene + bloom · Intensity` (default 0.08), before the TonemapPass.
- Targets RGBA16F (`PixelFormat` has no R11G11B10).

| `gxBloomQuality` | Start resolution | Levels |
|---|---|---|
| 0 Off | – | – |
| 1 Low | ¼ | 4 |
| 2 High (default) | ½ | 6 |

Budget estimate: 0.3-0.5 ms at High. Skipped while submerged.

## Section 4 — Parameters, Time of Day, Cvars

### Data flow

`SkyComponent::UpdateLighting` → `Scene::SetAtmosphereSettings(AtmosphereSettings)` →
`DeferredRenderer` → `AtmosphereBuffer`. The renderer reads only `Scene`; a later per-area
time-of-day system overrides what `SkyComponent` writes.

### Time-of-day curves

Two new curves in the existing `.hccv` format (editable in mmo_edit's colour curve editor),
loaded in `SkyComponent::LoadColorCurves` with built-in fallbacks like the existing four:

| File | rgb | alpha |
|---|---|---|
| `Models/FogColor.hccv` | fog scattering tint (cool blue night, warm peach dawn/dusk, pale day) | density multiplier (night 1.5, dawn 2.5, noon 1.0, dusk 2.0) |
| `Models/SunScatter.hccv` | sun colour inside fog (golden at low sun, near-white at noon) | shaft strength multiplier (strongest at low sun) |

(Curve alphas above superseded by Implementation Notes 10-11 — the fallback curves gained pre-dawn/after-dusk keys so nights are dark.)

The ambient term uses the existing ambient colour curve.

### Cvars

Final value = cvar base × curve multiplier where a curve applies. Registered and unregistered
in `WorldState` alongside the other `gx*` cvars; applied on load.

| Cvar | Default | Meaning |
|---|---|---|
| `gxAtmosphereQuality` | 3 | 0-4 (Section 2) |
| `gxFogDensity` | 0.02 (superseded by Implementation Notes 10-11: default is now 0.01) | extinction per metre at base height |
| `gxFogHeightFalloff` | 0.05 | exponential falloff per metre of height |
| `gxFogBaseHeight` | 0 (superseded by Implementation Notes 10-11: relative to the reference height, default -10) | world Y where density equals `gxFogDensity` |
| `gxFogAnisotropy` | 0.7 | Henyey-Greenstein g |
| `gxShaftStrength` | 1.0 | sun inscatter multiplier |
| `gxAtmosphereMarchDistance` | 200 | metres, clamped to ≤ 300 |
| `gxBloomQuality` | 2 | 0-2 (Section 3) |
| `gxBloomIntensity` | 0.08 | bloom composite weight |
| `gxBloomThreshold` | 0.8 | soft-knee threshold, linear |
| `gxExposure` | 1.0 | TonemapPass exposure |
| `gxAtmosphereDebug` | 0 | 1 inscatter only, 2 transmittance, 3 march shadow term |

Numeric defaults are starting points, tuned against the reference during verification.

Known limitation (superseded by Implementation Notes 10-11): `gxFogBaseHeight` is absolute world Y;
maps whose terrain sits far from Y = 0 need per-area values, delivered by the per-zone time-of-day
follow-up.

### Editor

World Settings panel keeps the fog toggle, removes the range slider, and adds live sliders for
density, height falloff, base height, shaft strength and exposure, writing the same
`AtmosphereSettings` the client builds from cvars.

## Section 5 — Options Menu, Testing, Rollout

### Options menu

In `data/client/Interface/GameUI/OptionsFrame.lua`, graphics section:

- Atmosphere Quality dropdown (Off/Low/Medium/High/Ultra) → `gxAtmosphereQuality`.
- Bloom dropdown (Off/Low/High) → `gxBloomQuality`.
- Brightness slider 0.5-2.0 → `gxExposure`.

String keys added to every locale's `Localization.txt` (placeholders where untranslated).

### Automated tests

Pure logic lives in headers/sources with no graphics-device dependency so the tests build on
the headless server configuration:

- Quality level → resolution divisor / steps / blur iterations mapping for both passes.
- `AtmosphereSettings` evaluation from curves × cvars, including missing-curve fallbacks.
- Inverse tonemap round trip: `Tonemap(InverseTonemap(c)) ≈ c` for c in [0, 0.999] per channel
  (C++ mirror of the HLSL functions).
- Closed-form height-fog optical depth and inscatter tail match a brute-force numerical
  integration within 1% across representative ray directions, heights and distances.

### Visual verification (real client, unattended auto-login + screenshot driver)

1. Regression checkpoint: effects off vs. develop at noon, dusk, underwater, and with
   particles/spell visuals on screen. No difference beyond dither noise.
2. Look: forest area facing a low sun and side-lit, at dawn, noon and night; tune defaults.
3. Performance: perf overlay GPU timings per quality level, with new timer marks for
   atmosphere, bloom and tonemap. Target ≤ ~3 ms combined at High/High (RTX 3070 class,
   1080p) and ≤ ~1 ms at Low/Low.
4. Editor: world editor viewport and material previews render correctly; sliders act live.

### Rollout order

1. TonemapPass, linear lighting output, inverse tonemap for unlit forward, material rebuild →
   regression checkpoint.
2. Analytic height fog (lighting composite + forward materials), time-of-day curves, cvars.
3. Ray-marched shafts, bilateral blur, bilateral upsample.
4. Bloom.
5. Options menu, editor sliders, tuning, performance pass.

All work on `feature/volumetric-atmosphere`, merged via `/gate` → `/ship`. The gate's E2E suite
has no rendering coverage; visual verification steps 1-4 are the acceptance check.

### Documentation

- New `docs/rendering-atmosphere.md`: pass order, the linear-HDR contract, cvars, curves.
- Refresh the graphics cvar list in `docs/console_commands.md`.

## Implementation Notes (added during planning, 2026-09-13)

These refine the approved design after reading the code; the plan
(`docs/superpowers/plans/2026-09-13-volumetric-atmosphere.md`) follows them.

1. **Standalone forward renderers keep tonemapping.** Eight call sites render
   `PixelShaderType::Forward` straight into a target with no `DeferredRenderer` (editor mesh,
   material, particle and item previews, the character editor, the minimap baker, and the
   client's `ModelRenderer`). Forward materials therefore tonemap unless a new
   `forwardOutputLinear` camera-buffer flag is set; only `DeferredRenderer` sets it, around its
   own forward pass.
2. **Regression guarantee, narrowed.** Opaque pixels match develop within dither noise.
   Partially transparent forward surfaces (water edges, soft particles) now alpha-blend in linear
   HDR instead of display space, so their edges can differ slightly. This is inherent to moving
   the tonemap and is judged visually at checkpoint 1.
3. **Fog parameters travel in the camera cbuffer (b1).** `fogStart`/`fogEnd`/`fogColor` and the
   padding slots are repurposed (density, height falloff, tint, base height, anisotropy, output
   mode) and one row is appended (sun scatter colour, shaft strength), because generated
   forward materials can only see b1. Size goes from 160 to 176 bytes.
4. **Fog radiance model.** `FogColor.hccv` rgb is the fog's ambient radiance (what fully fogged
   geometry converges to when looking away from the sun); the old horizon-colour fog is its
   default. Per step: `σ · (FogTint + SunScatterColor · SunColor · SunIntensity · ShaftStrength ·
   Phase · shadow)` with the phase normalised so isotropic = 1. The existing ambient colour curve
   is not used by the fog.
5. **Composite target.** The atmosphere composite reads `m_renderTexture` and writes
   `m_sceneColorCopy`; the existing single `CopyResource` runs in the opposite direction. No new
   full-resolution target. Bloom is composited inside the TonemapPass shader.
6. **Skip conditions.** The atmosphere and bloom passes run while `UnderwaterState::active` is
   false (not the transition predicate, so fog returns as soon as the camera surfaces) and the
   atmosphere pass only runs while `Scene::IsFogEnabled()`.
7. **Editor exposure slider dropped.** `WorldSettingsPanel` has no renderer reference; exposure
   stays a client option (`gxExposure`). The editor sliders cover the Scene atmosphere parameters.
8. **Fallback shaft multipliers.** `SunScatter.hccv` defaults use 0.35 at noon and 1.0 at dawn and
   dusk (not 0.8 / 1.6): with an unnormalised HG lobe at g = 0.7, looking straight at a noon sun
   already multiplies the sun term by about 15.
9. **Density exponent clamp.** Fog density is `Density · exp(min(−Falloff·(y − BaseHeight), 12))`
   so rays reaching far below the base height cannot overflow half/single floats.
10. Camera-relative fog base (in-game verification, 2026-09-13): absolute base height buried the
   test map (terrain far below Y = 0) in maximum-density fog. `gxFogBaseHeight` is now an offset
   from the camera height (default −10 m), density default 0.01, and the fallback curves gained
   pre-dawn/after-dusk keys so nights are dark. No shader change: `Scene::RefreshCameraBuffer`
   adds the camera height before upload.
11. Gate review (2026-09-14): fog base anchored to the controlled player's height in the client
   and the camera pivot in the editor (camera fallback) to stop fog pumping with camera orbit;
   density exponent clamp lowered 12 → 3 so fog below the base saturates (≈20×) instead of
   turning overlooks opaque; NaN/Inf sanitising uses a bitwise test because FXC folds isnan
   without IEEE strictness.
12. Density tuning (user feedback, 2026-09-14): fog read as far too dense in game (100 m ≈ 63 % fog
   at dawn). `gxFogDensity` 0.01 → 0.004; fallback density multipliers night 1.1, pre-dawn 1.2,
   dawn 1.5, noon 1.0, dusk 1.3, after dusk 1.15; `gxShaftStrength` 1.0 → 2.0 because scattered
   light scales with density and the shafts should stay readable in the thinner fog.
