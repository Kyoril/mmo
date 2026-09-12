# Stylized Ocean Water — Design

Date: 2026-09-12
Status: Approved, ready for implementation planning

## Goal

A stylized-realistic ocean water material for Alestia Online, in the Sea of Thieves
register: Fresnel-driven sky/depth blend, screen-space reflections of the coastline,
refracted seafloor with chromatic depth tint, animated shore foam with sun scatter.
Plus an underwater post-process so swimming and diving read correctly.

The water surface stays **geometrically flat**. Wave height would have to be network
synchronised for swimming to work, so all sense of motion comes from normals, foam and
parallax — never from displacement.

## Existing foundation

The engine already provides more than expected:

- **Water geometry.** Terrain pages carry per-tile water quad masks (8x8 bits per tile),
  per-page-vertex water heights, a per-tile `terrain::WaterType`
  (None/Water/Ocean/Lava/Slime), and a per-page water material name.
  `Page::RebuildWaterMesh()` emits flat quads of `TileSize / 8` = ~4.17 world units with
  world-continuous UVs and both faces, rendered in the forward transparent queue.
- **Material graph.** `SceneDepth`, `SceneColor`, `ScreenPosition`, `Fresnel`, `Panner`,
  `Rotator`, `Time`, `CameraVector`, `ReflectionVector`, `SmoothStep`, `Lerp`, `Mask`,
  `Append`, texture parameters, material functions (`.hmf`) and material instances
  (`.hmi`).
- **Renderer hooks.** `DeferredRenderer` copies the lit opaque scene colour to
  `kSceneColorTextureSlot` (t30) and binds the G-buffer normal RT (whose alpha carries
  linear view depth) at `kSceneDepthTextureSlot` (t31) before the forward pass.
  Refraction and depth fade are therefore already wired.
- **Globals.** `GlobalShaderParameters` (shared cbuffer at b13) already publishes
  `SkyHorizonColor` and `SkyZenithColor` from `SkyComponent`.
- **Forward lighting.** `PsCameraConstantBuffer` already carries sun direction, sun
  colour, sun intensity, ambient, fog and time for forward/translucent objects.
- **Server.** `server_water_map` already drives swim detection; swimming, dive pitch and
  swim ascent are implemented client and server side.

### Gaps

- No post-process stage of any kind. `GetFinalRenderTarget()` goes straight to a
  `frame_ui` world renderer frame (client) or `ImGui::Image` (editor).
- No reflection system.
- `ManualRenderObject` hardcodes normal = binormal = tangent = `Vector3::UnitY` on every
  triangle vertex. Tangent-space normal mapping on the water mesh is therefore broken
  today. **This is a blocker.**
- `IAudio` exposes no DSP/filter hooks.
- No `Terrain::GetWaterTypeAtWorldPos`.

## Decisions

| Question | Decision |
|---|---|
| Look target | Sea of Thieves / stylized-realistic |
| Scope | Material graph nodes + underwater post-process; look stays data-driven |
| Reflections | Screen-space ray march with sky fallback |
| Shore waves | Depth-driven now; material function keeps an optional distance-field input for a later bake |
| Underwater | Fog + tint, distortion + caustics, surface crossing, muffled audio + god rays |
| Water binding | `WaterType` to profile table; page material name stays as an override |
| Verification | Editor viewport iteration, then real client screenshots |

### Why SSR is a material node, not a pass

A dedicated full-screen SSR pass would have to run before the forward pass, but water is
drawn *in* the forward pass, so the pass could not know where the water is without a
depth prepass. Instead SSR is a material-graph node that ray-marches the already-bound
scene depth (t31) and samples scene colour (t30) inside the water pixel shader. The
compiler already emits helper functions conditionally (`m_needsSceneColor` gates a
texture declaration; `fresnelSchlick` / `DistributionGGX` / `GeometrySmith` are emitted
as helper bodies), so an `m_needsScreenSpaceReflection` gate emitting a `ComputeSSR()`
helper is the established pattern.

Consequences: the reflection ray is perturbed by the water's own animated normal for
free (exactly the distorted look wanted); cost falls only on visible water pixels; the
node is reusable later for wet ground or ice.

### Why underwater is a pass inside DeferredRenderer

`GetFinalRenderTarget()` is consumed by the client world renderer, the editor viewport,
the world model editor, the material instance editor and the spell visualization preview.
Putting the post-process inside `DeferredRenderer` and returning the post output from
`GetFinalRenderTarget()` means every consumer inherits it with no change. When the camera
is dry the pass is skipped entirely and the raw texture is returned, so the cost is
exactly zero and no existing editor tool is affected.

## Engine additions

| Addition | Location | Rationale |
|---|---|---|
| Per-vertex normal/tangent/binormal on manual triangles | `scene_graph/manual_render_object` | Blocker. Explicit setters; existing `UnitY` default retained so nothing else changes. |
| `ScreenSpaceReflection` node | `graphics/material_compiler` + D3D11 and Metal impls + editor `material_node` | The only genuinely new shader capability. Returns reflected colour and a hit mask. |
| `SunDirection` / `SunColor` globals | `graphics/sky_component` | Published next to the existing sky colour globals. Gives the graph sun glint with no compiler change. |
| `PostProcessPass` | new `deferred_shading/post_process_pass` | Underwater rendering. Modelled on `SsaoPass` / `ContactShadowPass`. |
| `IAudio::SetLowPassCutoff(float hz)` | `audio/audio.h`, `fmod_audio`, `null_audio` | FMOD low-pass DSP on the master channel group; no-op in the null backend. |
| `water_profiles.proto` | `shared/client_data` | `WaterType` to surface material plus underwater settings. |
| `Terrain::GetWaterTypeAtWorldPos` | `shared/terrain` | Sibling of the existing `GetWaterHeightAtWorldPos`. |
| Per-`WaterType` batching in `RebuildWaterMesh` | `shared/terrain/page` | One triangle-list op per distinct type present, plus bottom-face vertex tagging. |
| `WaterVolumeSystem` | `shared/game_client` | Submersion state and transitions. Lives in the library, not in `mmo_client`, so the existing `game_client_tests` suite can cover it — `mmo_client` is an executable and cannot be linked into a test suite. `game_client_tests` is unguarded in `src/tests/CMakeLists.txt`, so these tests run on the headless Linux build too. |

Everything else — the entire look — is data in `.hmat` / `.hmf` / `.hmi`, retunable in
the editor without a rebuild.

## The ocean material

### Baseline: what `Water_Base.hmat` already does

`Worlds/Water_Base.hmat` is an existing 72-node translucent graph and already implements
a large part of this design. Verified by `material_tool.py inspect` / `export-json`:

- Root `MaterialNode` flags are already **Lit, two-sided, Translucent, depth-write off,
  casts no shadows** — exactly what this design calls for.
- `SceneDepthNode` + `PixelDepthNode` — depth fade already present.
- `SceneColorNode` — refraction already present.
- `FresnelNode` — already present.
- `ReflectionVectorNode` + two `GlobalVectorParameterNode`s — sky reflection along the
  reflection vector already present.
- Four `PannerNode`s with `TimeNode` / `SineNode` / `CosineNode` — animated scrolling
  normals already present.
- Exposed parameters: `RefractionStrength`, `UnderwaterFogDepth`, `FoamWidth`,
  `FoamSharpness`, `FoamTiling`, `FoamContrast`, `Specular`, `Roughness`, `Metallic`,
  `EdgeFadeDistance`, `WaterColorShallow`, `WaterColorDeep`, `FoamColor`, and a `Normal`
  texture parameter.
- `Emissive Color` is **unconnected**; everything currently routes through `Base Color`.

This material has never looked right in practice because of the `ManualRenderObject`
tangent-basis bug — its normal map input resolves to nonsense. Fixing that alone will
change how it reads.

### Therefore: extend, do not rebuild

`Worlds/Water_Ocean.hmat` starts as a copy of `Water_Base.hmat` and is extended. This
is a much smaller and lower-risk change than the from-scratch graph originally sketched,
and it preserves the existing parameter names so any `.hmi` instances keep working.

What gets added:

1. **Screen-space reflection.** The new `ScreenSpaceReflection` node, blended by its hit
   mask into the existing sky-colour reflection path, which becomes the fallback.
2. **Sun glint.** Driven by the new `SunDirection` / `SunColor` globals.
3. **Chromatic extinction.** The existing shallow-to-deep `Lerp` gains a Beer-Lambert
   term so red extinguishes before green.
4. **Shore swash.** The existing foam path gains a time-varying envelope so foam advances
   up the beach, thins and retreats, plus an optional `ShoreDistance` input defaulting to
   the depth-derived value (the Phase 2 hook for a baked distance field).
5. **Whitecaps.** A tiling noise thresholded against the large-scale normal.
6. **Underside shading.** A `VertexColorNode` alpha test switching to a darker, silvery
   total-internal-reflection look for bottom-face triangles.
7. **Emissive routing.** View-dependent optics (refraction, SSR, sky) move from
   `Base Color` to the currently-unconnected `Emissive Color`, so already-lit scene
   colour is not lit a second time. Foam stays on `Base Color` so it catches the sun.

### Authoring workflow and its manual step

`material_tool.py` can export a graph to JSON, validate it, and `apply-json` it back,
rewriting only the `GRPH` chunk and preserving compiled shader chunks byte-for-byte. It
has **no create command** — a new material must start from a copy of an existing one.

Critically, the tool does not fabricate shader bytecode: after any graph write the
material must be **opened and saved in `mmo_edit`** to recompile shaders and regenerate
parameter tables. That is a GUI step that cannot be automated from this session, so every
graph task ends with an explicit hand-off for that save, and no graph change can be
visually verified before it happens.

Reusable material functions in `Worlds/Functions/` are a refactor to do *after* the look
is working, not before — splitting the graph up front would multiply the number of
manual editor saves for no gain.

### New textures

A caustics loop and a tiling noise, generated offline and imported as `.htex`.
`Foam_01_C.htex` and `WaterNormal_01.htex` already exist.

## Underwater post-process

A new `PostProcessPass` in `deferred_shading`, built like `SsaoPass` /
`ContactShadowPass`: owns a render texture, draws one full-screen triangle with the
shared `m_quadBuffer` and `m_deferredLightVs`, and reads the G-buffer linear depth.
`DeferredRenderer::Render` calls it after the forward pass.

Driven by an `UnderwaterState` struct set by the client each frame — `submersionDepth`,
`surfaceHeight`, the resolved `WaterProfile`, and `transitionPhase`. An explicit
parameter rather than a hidden global, so it is testable.

Effects in one shader, each individually gated:

- **Fog + tint.** Exponential extinction on the G-buffer linear depth using the profile's
  absorption colour. Density and colour additionally lerp with depth below the surface,
  so a dive grows progressively darker and bluer. This is the effect that does the actual
  work; the rest is garnish.
- **Distortion.** Two low-frequency sine warps of the screen UV at different rates.
  Amplitude scales down with depth, so it is strongest just under the surface.
- **Caustics.** The caustics texture projected in world space by reconstructing world
  position from depth, two layers at different scales scrolling against each other,
  multiplied over the scene. Masked to upward-facing surfaces via the G-buffer normal and
  faded with depth below the surface.
- **God rays.** Radial blur from the sun's screen position, masked to the upper screen and
  to shallow depths. 16 taps on a half-resolution buffer. Behind the `gxUnderwaterGodRays`
  cvar, default on.
- **Surface crossing.** `transitionPhase` drives a droplet/meniscus band. When the camera
  plane straddles the surface the shader splits the screen at the waterline and applies
  the underwater treatment only below it, rather than snapping the whole frame.

**Audio.** The client ramps `IAudio::SetLowPassCutoff` over the transition rather than
switching hard; the target cutoff comes from the profile.

All tuning lives in the water profile, so fog colour, density, caustic strength and audio
cutoff are editable per liquid without a rebuild.

## Data flow and client integration

### `water_profiles.proto`

New client-side proto in `src/shared/client_data/`, modelled on `surface_types.proto` and
registered in `project.h` / `project_saver` like every other client proto. Client-only —
the server needs nothing beyond the existing `server_water_map`.

```
message WaterProfile {
  required uint32 id                 // matches terrain::WaterType
  required string name
  optional string surface_material   // "Worlds/Water_Ocean.hmat"
  optional uint32 fog_color          // packed RGBA
  optional float  fog_density
  optional uint32 absorption_color
  optional float  caustics_strength
  optional string caustics_texture
  optional float  audio_lowpass_hz
  optional float  distortion_strength
}

message WaterProfiles { repeated WaterProfile entry = 1; }
```

Editable in the editor's data editors and exported to ClientDB like the others. Ocean is
the first entry; lake, river and lava are data-only additions later.

### Terrain

- `Terrain::GetWaterTypeAtWorldPos(x, z)` — walks page, then tile, then `m_waterTypes`.
- `Page::RebuildWaterMesh` gains a bucketing step: group quads by their tile's
  `WaterType`, emit one triangle-list operation per distinct type present, and resolve
  each type's material through the profile table. The existing per-page
  `m_waterMaterialName` stays and wins when set, so nothing currently authored breaks.
  Bottom-face vertices get the alpha tag. Minimap mode is untouched — it already
  overrides everything with a single opaque material.

### `WaterVolumeSystem`

In `src/shared/game_client/` — the library, not the `mmo_client` executable, so it is
reachable from the existing `game_client_tests` suite. The only stateful new logic,
deliberately pure:

1. Sample water height and type at the camera and at the player each frame.
2. Compute `submersionDepth` for the camera, and separately track the player, because the
   third-person camera can dip under while the character is dry.
3. Run a transition state machine — Dry, Crossing, Submerged, Crossing, Dry — with the
   crossing driving `transitionPhase` over roughly 0.4s.
4. Push `UnderwaterState` to the renderer and the ramped cutoff to `IAudio`.

It takes a terrain query interface and returns a state struct. No graphics, no Lua, no
signals, so it unit-tests headlessly.

### Editor

`water_edit_mode` gains a profile-aware material field: selecting a `WaterType` shows
which profile material it will use, and the free-text page override remains for one-off
cases.

## Testing

Headless tests in the gate:

- `WaterVolumeSystem` transition state machine — dry to submerged and back, half-submerged
  camera, teleport straight into deep water, water type changing under the camera.
- Water profile resolution — missing profile, missing material, page override precedence.
- `Terrain::GetWaterTypeAtWorldPos` — tile boundaries, page boundaries, no-water tiles.
- Per-`WaterType` quad bucketing, extracted as a free function so it tests without a
  `Scene`.

Suite placement, given the existing guards in `src/tests/CMakeLists.txt`:

- `terrain_tests` and `game_client_tests` are **unguarded** — the water type query, quad
  bucketing and the `WaterVolumeSystem` state machine go there and must keep building on
  the headless Linux server build.
- `client_data_tests` is gated on `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR` — water profile
  schema round-tripping goes there. The gate builds on Windows with the client, so these
  still run.
- Profile *resolution* logic (fallbacks, page override precedence) lives in
  `game_client` alongside `WaterVolumeSystem` rather than in `client_data`, so it is
  covered by the unguarded suite.

Also add a `deferred_shading_tests` case for the `PostProcessPass` skip decision — dry
camera means `GetFinalRenderTarget()` returns the raw texture, so no existing consumer
pays for the feature.

Visual iteration happens in the `mmo_edit` world editor viewport, then confirmation in the
real client via the scripted auto-login and screenshot driver, including a swim and a dive.

**Prerequisite:** a coastline to look at. If no existing map has ocean painted, the first
task is painting a test bay — a beach shelving into deep water — so shore foam, depth
tint and diving all have somewhere to happen.

## Risks

1. **Metal.** Adding a virtual to `MaterialCompiler` requires implementing
   `AddScreenSpaceReflection` in `material_compiler_metal.mm`, and the SSR helper must be
   written in both HLSL and MSL or the macOS build breaks. Real duplicated cost.
2. **Water height defaults to 0.** `m_waterVertexHeights` is zero-initialised on every
   page, so `GetWaterHeightAtWorldPos` returns 0 where there is no water at all.
   Underwater detection must gate on the quad mask, never on height alone, or every
   character standing below y=0 reads as swimming. This is the most likely bug in the
   feature.
3. **Material bytecode drift.** Compiler changes must be strictly additive and gated
   behind `m_needsScreenSpaceReflection`, or every material in the project needs
   rebuilding. A "Rebuild All Materials" tool exists if it comes to that.
4. **SSR cost under overdraw.** Water is two-sided and at grazing angles covers most of
   the screen. Step count needs a cvar and a distance cutoff beyond which reflection is
   sky-only.
5. **No tonemapping stage.** The target is `R16G16B16A16`, so sun glints can exceed 1.0
   and clip hard. Keep glint intensity conservative rather than adding a tonemapper here.
6. **No protocol change.** Water is entirely client-visual and the server's swim detection
   already exists, so no `ProtocolVersion` bump is expected — to be re-verified before
   shipping.
7. **Submodules.** `data/client` and `data/editor` are git submodules; asset work needs
   its own commits there.

## Out of scope

- **Any vertical displacement, even visual-only.** A small swell in the vertex shader
  would desync from the flat height the character floats at, so swimmers would clip
  through crests. Revisit only if the flat read genuinely fails.
- Baked shore distance field. The material function keeps the input; the bake tool is not
  built.
- Lake, river and lava profiles. The system supports them; only Ocean is authored.
- Water interaction — ripples around swimmers, wakes, splash particles.
- Reflections on surfaces other than water.
