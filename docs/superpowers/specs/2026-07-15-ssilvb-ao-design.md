# Screen-Space Ambient Occlusion via Visibility Bitmask (SSILVB-AO) — Design

**Date:** 2026-07-15
**Status:** Approved

## Problem

The deferred renderer has no screen-space ambient occlusion. The lighting pass applies only
the material's baked AO channel, so contact shadows in corners, under objects, and between
overlapping geometry are missing, leaving the ambient term flat.

## Decisions

- **Technique: the visibility-bitmask occlusion core of SSILVB** (Therrien, Levesque & Gilet,
  2023), not classic hemisphere-sampling SSAO and not GTAO. It costs the same class as GTAO
  but replaces the single-horizon-angle model — which treats every occluder as an infinitely
  thick heightfield and over-darkens behind thin geometry — with a 32-sector bitmask that lets
  light correctly pass behind railings and foliage.
- **AO only this round; GI deferred.** SSILVB's AO and GI ride the same sample loop, so the
  loop is structured to accept a radiance gather later. The GI half is *not* in scope because
  it requires a reprojected, denoised history buffer, and the engine has no motion vectors,
  no TAA, and no reprojection anywhere. That is its own project; its real prerequisite is
  motion vectors plus a temporal resolve, not SSAO.
- **Spatial-only noise.** A consequence of having no TAA. See "Noise and denoising" below.
- **Backend-neutral pass code.** `SsaoPass` contains no `#ifdef` and no D3D11 references.
- **Pixel shader, not compute.** Matches every existing deferred pass; needs no UAV plumbing.

## Scope and the backend question

The deferred renderer is Windows/D3D11-only today, and SSAO does not change that in either
direction. Confirmed:

- There are no `.metal` shader files anywhere in the repo.
- `ShaderCompilerMetal::Compile` (`src/shared/graphics_metal/shader_compiler_metal.cpp:8`) is
  a stub: it ignores its input and returns `succeeded = true` with a one-byte zero blob.
- `graphics_device_metal.mm` is a 288-line skeleton.
- `ShaderCompilerD3D11` is by contrast a real `D3DCompile` implementation.

Two distinct things were conflated under "D3D11-only", and they are handled differently:

1. **Shader bytecode is HLSL.** Backend-bound, and out of scope. Making it otherwise means
   writing MSL for the whole deferred path and implementing the Metal compiler for real.
2. **C++ pass code reaching into `ID3D11*`.** Avoidable, and avoided here.

`SsaoPass` needs nothing that is not already abstracted. The `#ifdef WIN32` blocks in
`deferred_renderer.cpp` exist for exactly three things — GPU timestamp queries, the
`CopyResource` scene-color hazard workaround, and the comparison sampler (which already
carries a `// TODO: Fix me: Move me to graphicsd3d11` at line 128). SSAO needs none of them.
It needs render targets, a constant buffer, a shader, a bound texture, and a fullscreen quad
draw — each already a `virtual` on `GraphicsDevice`.

The single backend-bound fact — which bytecode blob to hand `CreateShader` — sits behind one
narrow seam (`ssao_shaders.h`) resolving to the D3D_SM5 blob today. When the Metal backend
grows a real compiler and MSL deferred shaders, SSAO joins by adding a source file, with no
change to pass logic.

**Explicitly rejected:** implementing `ShaderCompilerMetal` and porting the deferred path to
MSL as part of this work. There is no Mac in the loop to test on, and a speculative untested
Metal SSAO path bolted onto a renderer that cannot run on Metal is unverifiable abstraction.

## Where the pass sits

New `SsaoPass` class in `src/shared/deferred_shading/` (`ssao_pass.h` / `ssao_pass.cpp`),
owned by `DeferredRenderer`, run between the geometry and lighting passes:

```
FindLights -> shadows -> RenderGeometryPass -> [SsaoPass] -> RenderLightingPass -> forward
```

It gets its own file rather than joining `deferred_renderer.cpp` (already 776 lines, 336-line
header) because it has a clean boundary: it owns its render targets, shaders, constant
buffer, and resize logic, and exposes `Render(camera)` plus a result texture.

## Inputs — no new G-Buffer targets

The G-Buffer's normal target already packs everything the technique needs into one texture:

- `rgb` = world-space normal, encoded `* 0.5 + 0.5`
- `a`   = linear **radial** depth, i.e. `length(viewPos)` — *not* view-space Z

The AO pass reconstructs view-space position exactly as `PS_DeferredLighting.hlsl:472-497`
does, so the two passes cannot disagree:

```hlsl
float2 ndc    = float2(uv.x * 2 - 1, 1 - uv.y * 2);
float4 viewH  = mul(float4(ndc, 1, 1), InverseProjection);
float3 viewRay = normalize(viewH.xyz / viewH.w);
float3 viewPos = viewRay * normalData.a;   // radial depth, hence viewRay * d
```

The pass works in view space, transforming the decoded world normal in with `matView` (present
in the `ViewMatrices` cbuffer and currently unused by the lighting shader). No new G-Buffer
targets and no additional geometry submission.

## Algorithm

`PS_Ssao.hlsl`, a fullscreen-quad pixel shader. `countbits()` is available in SM5.0 pixel
shaders, which is what the existing CMake rules already compile to.

Per pixel:

1. Decode `viewPos` and the view-space normal as above.
2. For each of N slices, with the slice basis rotated per-pixel by interleaved gradient noise:
   - Project the normal into the slice plane; note its angle.
   - Zero a `uint` sector mask.
   - March M screen-space steps in both directions along the slice. Per sample:
     - Reconstruct the sample's view position.
     - Compute the **front** angle it subtends from `viewPos`.
     - Compute the **back** angle from the front point pushed away by `thickness`.
     - Map that `[front, back]` angular range to sector indices and OR them into the mask.
   - Slice occlusion = `countbits(mask) / 32`, weighted by the projected normal's cosine.
3. `AO = 1 - (sum of slice occlusion) / N`, then apply `intensity`.

The `thickness` parameter is the whole point of the technique: it is what stops a thin railing
from occluding as though it were a solid wall. The per-sector cosine weighting is the fiddliest
part of the math and the most likely place for a first-pass bug — it is where to look first if
the AO reads too dark or too flat.

**GI-readiness:** the radiance gather would hook in at step 2 where a sample *newly* claims
sectors, weighting fetched color by the newly-claimed sector count. The loop is written so this
is an addition, not a restructure. No GI code ships now.

## Noise and denoising

Standard implementations rotate the slice basis with interleaved gradient noise keyed on the
**frame index** and rely on temporal accumulation to resolve it. With no history buffer that
would visibly shimmer.

Therefore: **IGN is keyed on pixel position only** — stable across frames — and a depth-aware
separable bilateral blur (`PS_SsaoBlur.hlsl`, horizontal then vertical) resolves the resulting
fixed pattern. This is the main quality ceiling versus a temporally-accumulated version, and
it is precisely the ceiling that the deferred GI phase would later lift.

## Composite

Already wired. `PS_DeferredLighting.hlsl:492-503` reads `float ao = materialData.a` and does
`lighting = albedo * AmbientColor * ao + emissive`. The SSAO result multiplies into that same
`ao` term — so it darkens **ambient only** and leaves direct lighting untouched.

When SSAO is disabled, `SsaoPass` allocates no render targets and returns a shared 1x1 white
texture. The lighting shader therefore samples unconditionally and needs no shader permutation
and no branch.

The AO map binds to **`t4`** in the lighting shader. That slot is confirmed free: the G-Buffer
occupies `t0`-`t3`, the shadow cascades `t5`-`t8`, and the light structured buffer `t9`, leaving
`t4` as a gap directly adjacent to the G-Buffer block — a natural home for a G-Buffer-adjacent
map. (`t14`/`t15` are taken by the forward pass for scene color and scene depth.)

## Engine API — no cvar knowledge

On `DeferredRenderer`, mirroring the existing `SetShadowQuality` precedent. The graphics engine
knows nothing about console variables; the client drives these setters.

```cpp
void SetSsaoEnabled(bool enabled);
bool IsSsaoEnabled() const;
void SetSsaoQuality(int level);          // 0/1/2 -> slice & step counts
void SetSsaoHalfResolution(bool enabled);
void SetSsaoRadius(float radius);
void SetSsaoIntensity(float intensity);
void SetSsaoThickness(float thickness);
void SetSsaoDebugVisualization(bool enabled);
```

## Client cvars

Registered in `WorldState::RegisterGameplayCommands`, with `Changed` handlers shaped exactly
like `OnShadowQualityChanged` (fetch `WorldFrame` -> `WorldRenderer` -> `DeferredRenderer`,
null-check each, log, call the setter). The `gx` prefix follows the newer convention set by
`gxRenderScale`, `gxDepthPrepass`, and `gxTerrainBatching`.

| cvar | default | effect |
|---|---|---|
| `gxSsao` | `1` | master enable |
| `gxSsaoQuality` | `2` | 0 = 2 slices / 4 steps, 1 = 3 / 8, 2 = 4 / 12 |
| `gxSsaoHalfRes` | `1` | AO at half resolution + bilateral upsample |
| `gxSsaoRadius` | `1.5` | sample radius, world units |
| `gxSsaoIntensity` | `1.0` | strength multiplier |
| `gxSsaoThickness` | `0.25` | occluder thickness heuristic |
| `gxSsaoDebug` | `0` | visualize raw AO (precedent: cascade debug viz) |

`gxRenderScale` needs no special handling: the AO targets size off the G-Buffer, which already
reflects render scale.

## Two enabling changes outside the pass

1. **`PixelFormat::R8` is not mapped in the D3D11 backend.** `render_texture_d3d11.cpp:130-150`
   falls through `default:` to `R8G8B8A8_UNORM`, so requesting `R8` silently yields RGBA8. A
   single-channel AO target needs `case PixelFormat::R8: -> DXGI_FORMAT_R8_UNORM` added. `R8`
   already exists in the shared, backend-neutral `PixelFormat` enum, so this fixes an existing
   leak in the abstraction rather than adding one.
2. **`GpuTimerPointCount` 5 -> 6**, with a new mark after the SSAO pass, so it gets its own
   entry in the perf overlay. This extends the existing `#ifdef _WIN32` timer block by one
   entry; it is not new backend coupling. Downstream consumers of the timer points must be
   checked, since inserting a point shifts subsequent indices.

## Editor

`mmo_edit` also uses `DeferredRenderer` and will pick up the defaults. Exposing editor-side
toggles is out of scope.

## Verification

There is no meaningful automated test here, and this should not be pretended otherwise: the
E2E harness is headless and unit tests cannot judge an AO term. Verification is visual and
manual, in the client:

- Toggle `gxSsao` 0/1 — expect contact darkening in corners and under objects, and expect
  **no change to direct lighting**, only to the ambient term.
- `gxSsaoDebug 1` — inspect the raw AO buffer for correctness and noise structure.
- Sweep `gxSsaoThickness` against thin geometry (railings, foliage) — confirm thin occluders
  do not darken like solid walls. This is the specific behaviour that justifies the technique
  over a horizon-based method; if it does not hold, the bitmask math is wrong.
- Pan the camera — confirm no frame-to-frame shimmer, which would mean the noise is
  accidentally frame-keyed.
- Read the new SSAO entry in the perf overlay across `gxSsaoQuality` 0/1/2 and
  `gxSsaoHalfRes` 0/1 — confirm the perf cvars measurably do something.
