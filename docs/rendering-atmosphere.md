# Atmosphere: Height Fog, Light Shafts, Bloom

Design: [superpowers/specs/2026-09-13-volumetric-atmosphere-design.md](superpowers/specs/2026-09-13-volumetric-atmosphere-design.md)

## Frame order (DeferredRenderer::Render)

1. Cascaded shadow maps → G-Buffer → SSAO → contact shadows
2. Lighting (`PS_DeferredLighting`) — **linear HDR**, no fog, no tonemap
3. `AtmospherePass` — march (reduced resolution) → bilateral blur → composite into `m_sceneColorCopy`,
   copied back into the scene target. Skipped while submerged, with `Scene::IsFogEnabled()` false,
   or when the combined fog density is zero (time-of-day can scale the base density to nothing).
4. Forward pass — `Scene::SetForwardOutputLinear(true)`; materials apply the same height fog analytically
5. `BloomPass` — 13-tap downsample chain, tent upsample chain. Skipped while submerged.
6. `TonemapPass` — scene + bloom, exposure, ACES, gamma, dither
7. `PostProcessPass` (underwater only) — display-referred input

## The linear-HDR contract

Nothing before the TonemapPass may tone map or gamma-encode. Forward materials rendered outside
DeferredRenderer (editor previews, the client's model frames, the minimap baker) keep tone mapping
in the material because they never set `forwardOutputLinear`. Unlit forward materials author
display-referred colour and inverse-tonemap it inside the deferred forward pass.

Material graphs sampling Scene Color / SSR receive a display-referred sample (a generated
`LoadSceneColor` helper tonemaps the linear scene copy when `forwardOutputLinear` is set), so
existing water materials keep their tuned look.

## Fog model

Density `σ(y) = gxFogDensity · densityMultiplier · exp(min(−gxFogHeightFalloff · (y − gxFogBaseHeight), 3))`.
The exponent clamp of 3 means fog below the base saturates at e³ ≈ 20× the base density instead of
climbing toward opacity. `gxFogBaseHeight` is an offset from the fog reference height — the
controlled player's height in the client (`Scene::SetAtmosphereReferenceHeight`, set every frame
from `WorldState::OnIdle`), the camera orbit pivot in the editor, or the camera's own derived Y as a
fallback (`Scene::RefreshCameraBuffer`) — rather than the camera height directly, so fog density at a
fixed ground point no longer pumps as the orbit camera zooms or pitches.

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
| `gxFogDensity` | 0.004 | extinction per metre at the base height |
| `gxFogHeightFalloff` | 0.05 | falloff per metre of height |
| `gxFogBaseHeight` | -10 | height of the base relative to the player (client) / camera pivot (editor), in metres, where density equals `gxFogDensity` |
| `gxFogAnisotropy` | 0.7 | sun glow tightness |
| `gxShaftStrength` | 2.0 | sun scattering multiplier |
| `gxBloomQuality` | 2 | 0 Off, 1 Low, 2 High |
| `gxBloomIntensity` | 0.08 | bloom weight |
| `gxBloomThreshold` | 0.8 | soft-knee threshold (linear) |
| `gxExposure` | 1.0 | brightness before tone mapping |

## Known limitations

- The fog base follows the player/camera-pivot reference height until per-zone atmosphere data exists (planned per-zone time of day).
- Shafts need shadow-casting geometry and end at the 300 m shadow range.
- Shafts are not drawn over forward surfaces (water, particles); those get closed-form fog only.
- Point and spot lights do not scatter in the fog.
- Bloom strength is gxBloomIntensity divided by the number of bloom levels, because every level adds its own copy of the light.
- Debug views (gxAtmosphereDebug) are composited before bloom and tone mapping, so they appear tone-mapped; view 3 is black at gxAtmosphereQuality 0 (nothing is marched).
