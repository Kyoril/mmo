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

Density `σ(y) = density · densityMultiplier · exp(min(−fog_height_falloff · (y − fog_base_height), 3))`.
The exponent clamp of 3 means fog below the base saturates at e³ ≈ 20× the base density instead of
climbing toward opacity. `fog_base_height` is an offset from the fog reference height — the
controlled player's height in the client (`Scene::SetAtmosphereReferenceHeight`, set every frame
from `WorldState::OnIdle`), the camera orbit pivot in the editor, or the camera's own derived Y as a
fallback (`Scene::RefreshCameraBuffer`) — rather than the camera height directly, so fog density at a
fixed ground point no longer pumps as the orbit camera zooms or pitches.

Per metre the fog scatters `σ · (FogTint + SunScatterColor · SunColor · SunIntensity · shaft_strength ·
Phase(cosθ) · shadow)` toward the camera, with a Henyey-Greenstein lobe (g = fog_anisotropy) blended
80/20 with isotropic. The first `gxAtmosphereMarchDistance` metres are marched through the shadow maps;
the rest of the ray uses the closed-form integral with the sun unshadowed.

The formulas exist three times and must change together:
`shaders/AtmosphereCommon.hlsli`, the forward fog emitted by `MaterialCompilerD3D11`, and
`deferred_shading/atmosphere_math.h` (unit-tested).

The camera cbuffer (b1, 176 bytes) is declared three times as well: `PsCameraConstantBuffer` in
`scene.cpp`, `CameraBuffer` in `AtmosphereCommon.hlsli`, `CameraParameters` in the material compiler.
Any change there requires **Tools → Rebuild All Materials** in the editor.

## Environment profiles

The look comes from environment profiles (`environment_profiles` game-data table, edited in the
editor's Environment Profile Editor). A profile has eight day curves and fixed values:

| Curve | rgb | alpha |
|---|---|---|
| `sky_horizon`, `sky_zenith`, `clouds` | sky material colours | unused |
| `ambient` | scene ambient | unused |
| `sun`, `moon` | light colour | intensity |
| `fog` | fog ambient radiance | density multiplier |
| `sun_scatter` | sun colour inside fog | shaft multiplier |

Fixed values: fog density, height falloff, base height, anisotropy, shaft strength, exposure, bloom
intensity, bloom threshold, transition seconds. An empty curve uses the built-in Default
(`EnvironmentProfile::MakeDefault`), which reproduces the look from before profiles existed.

A zone uses its own profile, else its parent's, else the map's default profile, else the built-in
Default (`ResolveEnvironmentProfileId`). `EnvironmentController` fades to a new profile over that
profile's transition time; `SkyComponent::ApplyEnvironment` pushes the result into the sky, the
sun/moon light, the scene's atmosphere values and the global shader parameters every frame.

## Console variables

| Cvar | Default | Meaning |
|---|---|---|
| `gxAtmosphereQuality` | 3 | 0 Off (height fog only), 1 Low, 2 Medium, 3 High, 4 Ultra |
| `gxAtmosphereMarchDistance` | 200 | metres marched for shafts (≤ 300) |
| `gxAtmosphereDebug` | 0 | 1 scattered light, 2 transmittance, 3 shaft shadow term |
| `gxBloomQuality` | 2 | 0 Off, 1 Low, 2 High |
| `gxExposure` | 1.0 | player brightness, multiplies the profile exposure |

## Known limitations

- Shafts need shadow-casting geometry and end at the 300 m shadow range. Terrain casts into the
  cascades (so nearby hills block shafts and shadow the ground), but mountains farther than 300 m
  cannot block the sun.
- Shafts are not drawn over forward surfaces (water, particles); those get closed-form fog only.
- Point and spot lights do not scatter in the fog.
- Bloom strength is the environment profile's bloom intensity divided by the number of bloom levels, because every level adds its own copy of the light.
- Debug views (gxAtmosphereDebug) are composited before bloom and tone mapping, so they appear tone-mapped; view 3 is black at gxAtmosphereQuality 0 (nothing is marched).
