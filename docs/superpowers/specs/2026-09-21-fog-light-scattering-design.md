# Point and Spot Light Scattering in Volumetric Fog — Design

Project B of the volumetric fog series (A: froxel fog, shipped 2026-09-20; C: local fog volumes).
Branch: `feature/fog-light-scattering`.

## Goal

Point and spot lights scatter into the froxel fog: lanterns get halos in fog, spot lights draw visible
cones. Every point and spot light scatters by default; each light carries a multiplier to tune or mute
it (for example lights inside buildings, since point and spot lights have no shadows).

Spot lights do not work today, so full spot support is part of this project: a defined cone
convention, authored cone angles on world-model lights, correct surface lighting and fog cones.

## Current state (2026-09-21)

- Up to 256 visible lights are gathered per frame (`OctreeScene::GatherVisibleLights`, frustum-culled,
  priority-sorted), packed into 64-byte `ShaderLight` entries and uploaded as a `StructuredBuffer` bound at
  PS `t9` for the single fullscreen deferred lighting pass (`PS_DeferredLighting.hlsl`), which loops all
  lights per pixel.
- Point attenuation is `pow(1 - saturate(d / range), 2)`, zero beyond the range.
- Spot lights produce no light: nothing calls `Light::SetInner/OuterConeAngle`, so the outer angle stays
  0 and the shader's cutoff `cos(radians(SpotAngle * 0.5))` is 1. `light.h` documents the angles as
  radians while the shader reads a full angle in degrees. The inner angle is unused.
- Only the sun has shadows (CSM). Point and spot lights never cast shadows.
- `StructuredBufferD3D11::BindToStage` supports only the vertex and pixel stages.
- Point and spot lights come from world-model (WMO) lights (`WorldModelLight`, authored in the world model
  editor) and from spell-kit and projectile lights (`PointLightConfig`, always point lights).
- The world model editor draws spot gizmos along -Z with a hardcoded 45 degree cone; runtime spots shine
  along the light's derived +Z direction.

## 1. Light data and authoring

### Cone convention

- `Light` inner and outer cone angles are **full cone angles in degrees**. Defaults: outer 45, inner 30.
  Setters clamp to [1, 179] and keep inner <= outer (setting the outer below the inner lowers the inner).
- Spot falloff is `smoothstep(cos(outer / 2), cos(inner / 2), dot(-L, spotDirection))`, replacing the
  fixed `smoothstep(cutoff, cutoff + 0.1, ...)`.
- `light.h` documentation is corrected.

### Per-light fog multiplier

- `Light` gains `fogScattering` (default 1, clamped [0, 8]; 0 = never scatters into fog).

### GPU light layout (unchanged size)

`ShaderLight` stays 64 bytes, with the same order in C++ (`deferred_renderer.h`) and HLSL:

```
float3 position;   float range;
float3 color;      float intensity;
float3 direction;  float spotCosOuter;   // cos(outer / 2), was spotAngle
uint   type;       int   shadowMap;
float  spotCosInner;                     // cos(inner / 2), was padding
float  fogScattering;                    // was padding
```

Cosines are computed on the CPU once per light. `VisibleLightInfo` and both `GatherVisibleLights`
implementations (`Scene` and `OctreeScene`) carry the inner angle, outer angle and fog scattering.

### World-model lights: file format 2.1

- `WorldModelLight` gains `innerConeAngle`, `outerConeAngle` (degrees) and `fogScattering`.
- `world_model_serializer` adds `Version_2_1`. `Latest` writes 2.1. Each `MOLT` entry grows from 48 to
  60 bytes: the three floats follow `attenuationEnd`. The writer's hardcoded chunk size follows the new
  entry size.
- Reading version 2.0 keeps the 48-byte layout and fills the defaults (30, 45, 1). Existing assets load
  unchanged until they are re-saved.
- `WorldModelInstance` applies the cone angles and fog scattering to the scene `Light`.
- World model editor (`world_model_properties_panel.cpp`): Inner Cone and Outer Cone drags (spot only) and
  a Fog Scattering drag (all lights). The editor preview light gets the same values. The spot gizmo draws the
  real outer cone. The gizmo/runtime direction mismatch is resolved so the gizmo points where the light
  shines in game.

### Spell and projectile lights

- `PointLightConfig` gains `optional float fog_scattering = 9 [default = 1];` in both
  `proto_data/spell_visualizations.proto` and the `client_data` mirror (same field number).
- `spell_visualization_service.cpp`, `projectile_manager.cpp` and their editor preview copies apply it.
- The spell visualization editor gets a Fog Scattering drag in the kit and projectile light sections.
- These lights remain point lights.

### Surface lighting

The deferred lighting pass uses the new cone math, so spot lights light surfaces. Content that already
contains spot lights starts producing light.

## 2. Fog inject

### Resources

- `StructuredBufferD3D11::BindToStage` handles `ShaderType::ComputeShader` (`CSSetShaderResources`).
  `ClearComputeBindings` already clears CS SRV slots 0-15.
- `VolumetricFogPass::Render` receives the light buffer and the light count from `DeferredRenderer` and
  binds the buffer at CS `t9`.
- The fog constant buffer (b2, `VolumetricFogConstants` / `VolumetricFogBuffer`) appends
  `uint LightCount; float LightScatterStrength; float2 _pad;` and grows from 224 to 240 bytes. The C++
  `static_assert` and the HLSL declaration change together.

### Shared light math

`LightCommon.hlsli` holds point attenuation and the spot cone factor. `PS_DeferredLighting.hlsl` and
`CS_FogInject.hlsl` both include it. The same formulas live in a dependency-free C++ header
(`deferred_shading/light_math.h`) with unit tests, mirroring how `volumetric_fog_settings.h` pairs with
`VolumetricFogCommon.hlsli`.

### Per-group culling

`CS_FogInject` runs `[numthreads(8, 8, 8)]`. At the start of each group:

1. Compute the world-space bounding sphere of the group's 8x8x8 froxel block from its corner positions
   (screen-space corners at the block's first and last depth slice boundaries).
2. The 512 threads walk the light list together: thread `i` tests lights `i`, `i + 512`, ...
3. A light is kept when its type is point or spot, `fogScattering > 0`, its range sphere intersects the
   block sphere, and (spot only) a conservative cone-vs-sphere test passes. Culling may keep extra lights,
   never drop a light that reaches the block.
4. Kept indices go into a `groupshared` list (capacity 64) via `InterlockedAdd`. Indices past the capacity
   are dropped. `GroupMemoryBarrierWithGroupSync` follows.

### Per-froxel light term

For each kept light, with `toFroxel = froxelPos - lightPos`, `d = length(toFroxel)`,
`L = toFroxel / d`:

```
contribution = color * intensity * Attenuation(d, range) * SpotFactor(L, ...)
             * fogScattering * LightScatterStrength * ScatterPhase(dot(ray, -L))
```

`ray` is the camera-to-froxel direction. Light travelling along `L` scatters toward the camera (`-ray`),
so the scattering cosine is `dot(L, -ray) = dot(ray, -L)`, the same convention as the sun term's
`dot(ray, SunDirection)`. The phase peaks when the light sits behind the froxel as seen from the camera
(forward scattering, same Henyey-Greenstein blend as the sun). The sum is added to
the froxel's source radiance before it is multiplied by sigma. Density, noise, temporal blend and
integration are unchanged. The attenuation reaches 1 at the light, so froxels next to a light stay bounded.

### Known limitation

The 90% temporal history smears fast-moving lights (projectiles). Accepted for now and checked in game.

## 3. Controls, quality, testing

### Controls

- Environment profile field `light_scattering = 25 [default = 1]` in both `environment_profiles.proto`
  copies, clamped [0, 8] by the loader, carried through `EnvironmentProfile`, `EnvironmentState` and
  `LerpEnvironment` (linear blend) into `LightScatterStrength`.
- Environment Profile Editor: "Light Scattering" slider in the Fog section.
- No new cvar.

### Quality and debug

- Light scattering is active at fog quality 1-4. Quality 0 disables all fog.
- `gxAtmosphereDebug 4`: per-block count of kept lights as a heat map, overflowing blocks in red. The
  cvar range grows from 0-3 to 0-4.

### Performance

Target at most +0.3 ms at quality 3 (High) with about 20 point lights on screen at 1080p. Debug builds
report GPU timings as 0, so the number is measured in Release.

### Tests

- `light_math.h`: cone cosines from degrees, spot falloff at and between the inner and outer edge, point
  attenuation, block-sphere vs light-sphere, conservative cone-vs-sphere.
- Light setters: cone clamps and inner <= outer, fog scattering clamp.
- World-model serializer: 2.1 round trip; a 2.0 buffer reads back with the defaults.
- `PointLightConfig.fog_scattering` default; profile `light_scattering` load, clamp and blend.
- Size asserts for `ShaderLight` (64) and the fog constant buffer (240).

### Visual verification (client)

- Oakenshire at night under Coastal Sea Fog: lantern halos, stronger when looking toward a lantern.
- A spell light in fog.
- A spot light on an existing Oakenshire world model (authored for the check): cone on the ground and in
  the fog.
- `gxAtmosphereDebug 4` screenshots to confirm the culling.

## Out of scope

- Shadows for point and spot lights (and therefore light occlusion inside the fog).
- Temporal history rejection for moving lights.
- Area lights, light cookies, per-light volumetric noise.
- Standalone editor-placed world lights (lights still come from world models and spell visuals).
