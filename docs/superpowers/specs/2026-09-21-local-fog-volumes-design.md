# Local Fog Volumes — Design

Project C of the volumetric fog series (A: froxel fog, B: point and spot light scattering, plus colour
grading — all on `develop`). Branch: `feature/local-fog-volumes`.

## Goal

Level designers place local fog volumes in the world editor — mist in a hollow, fog over a pond, gas over
a swamp, dust in a cave mouth — that join the froxel volumetric fog: lit by the sun (with shadows), the
zone's ambient fog light and point/spot lights, temporally smoothed and depth-sorted with everything else.

## Current state (2026-09-21)

- `VolumetricFogPass` injects height fog per froxel in `CS_FogInject.hlsl` (`[numthreads(8,8,8)]`), with
  per-block culling of the frame's point/spot lights into a 64-entry group-shared list, then temporal blend,
  integration and composite. The froxel grid covers `gxVolumetricFogRange` (default 200 m); beyond it an
  analytic height-fog tail is used.
- Froxel output is `rgb = radiance * sigma, a = sigma`, where `radiance = FogSource(sun, shadow) + point/spot
  light scattering`.
- Wind: `WindState` gives a noise offset in zone-noise tiles, wrapped to [0, 1); the noise volume tiles at 1.
- Placed world content: entities (`.wobj` per page), foliage (`.hfol` per page), terrain/water (`.tile`),
  and the `.hwld` world file (`MVER`/`RRET`/`MESH`). Area triggers (proto table) are the only box/sphere
  volumes, with an edit mode, wireframe display, gizmo and picking (`area_trigger_edit_mode.cpp`,
  `SelectedAreaTrigger`, `AddAreaTrigger`).
- The client reads `Worlds/<dir>/<dir>.hwld` in `WorldState::LoadMap`; foliage and entities stream per page.

## 1. Data, file and editor

### Fog volume

| Field | Type / range | Meaning |
|---|---|---|
| `id` | uint32, unique per map | Editor identity. |
| `name` | string | Optional label for the editor list. |
| `shape` | `Box` = 0, `Ellipsoid` = 1 | |
| `position` | Vector3 | Centre, world space. |
| `size` | Vector3, each axis >= 0.5 m | Full extents (box edge lengths, ellipsoid diameters). |
| `yaw` | degrees | Rotation around +Y. |
| `density` | [0, 1] | Extinction per metre at the volume's bottom. Typical mist 0.02. |
| `color` | RGB, each [0, 2] | Multiplies the light the volume scatters. White looks like zone fog. |
| `edgeFade` | [0, 1] | Fraction of the half-size over which density fades to 0 at the border. |
| `heightFalloff` | [0, 1] per metre | Density *= exp(-falloff * height above the volume's bottom). |
| `activeFrom`, `activeTo` | hours [0, 24) | Active window; wraps past midnight (from 22 to 6 is valid). from == to = always on. |
| `fadeHours` | [0, 6] | Fade in after `activeFrom` and out before `activeTo`, linear. |
| `noiseAmount` | [0, 1] | Same meaning as the zone's `fog_noise_amount`. |
| `noiseDetail` | 1, 2 or 4 | Noise is this many times finer than the zone's `fog_noise_size`. |

The shared C++ struct lives in `src/shared/game_common/world_fog_volumes.h` with clamping on load.

### File format: `Worlds/<dir>/<dir>.hfog`

Chunk format like the other world files (`ChunkWriter` / `ChunkReader`):

- `MVER`: uint32 version, currently 1.
- `FVOL`: uint32 count, then per volume: uint32 id, uint16-length name, uint8 shape, Vector3 position,
  Vector3 size, float yaw, float density, Vector3 color, float edgeFade, float heightFalloff,
  float activeFrom, float activeTo, float fadeHours, float noiseAmount, uint8 noiseDetail.
- Unknown chunks are skipped. A missing file means no volumes. Out-of-range values are clamped when read.

Reader and writer: `world_fog_volumes.cpp` in `game_common`. Only the client and the editor read it; the
server and the nav builder never do.

### World editor

A new "Fog Volumes" edit mode (`edit_modes/fog_volume_edit_mode.*`), modelled on the area-trigger mode:

- List of the map's volumes; "Add Box" and "Add Ellipsoid" create a volume at the camera focus
  (size 20 x 6 x 20 m, density 0.02, edge fade 0.3, height falloff 0.1, always on, noise 0.4 at detail 1).
- Property panel for every field above.
- Wireframe display (`Editor/Wireframe` manual render object): box edges; ellipsoid as three rings.
- Selection and picking; gizmo translate, yaw rotate, scale of `size` per axis; delete and duplicate.
- The world editor's Save writes `<dir>.hfog` when volumes changed (removing the file when the list is empty).
- The editor viewport renders the volumes live through its own fog pass.

### Client

`WorldState::LoadMap` reads the `.hfog` for the map (empty list when absent) and keeps it until the next map
load.

## 2. Rendering

### CPU selection (each frame)

`deferred_shading/fog_volume_math.h` (dependency-free apart from `base` and `math`, mirrored in HLSL) holds
the time-of-day factor, shape distance, edge fade, height falloff and noise-detail helpers.

The client and the world editor call `DeferredRenderer::SetFogVolumes(...)` each frame with the volumes that
are active (time-of-day factor > 0), whose bounding sphere lies within the fog range of the camera and
intersects the view frustum, sorted by distance, at most 64. Density is pre-multiplied by the time-of-day
factor.

### GPU record (80 bytes, C++ and HLSL identical, size-asserted)

```
float3 center;      float density;        // density already scaled by time of day
float3 halfSize;    float heightFalloff;
float3 color;       float edgeFade;
float yawSin; float yawCos; float noiseAmount; float noiseDetail;
uint shape; float3 padding;
```

`VolumetricFogPass` owns a dynamic structured buffer of 64 records bound at CS `t10`. The fog constant buffer
appends `uint FogVolumeCount` (+ padding) and grows by 16 bytes to 256.

### Culling

After the light list, each 8x8x8 thread group tests the volume list against its block's bounding sphere
(volume bounding sphere radius = length(halfSize)) into a second group-shared list of up to 16 indices
(overflow counted, extra volumes dropped).

### Per froxel

For each kept volume, with `p` = froxel position in the volume's local frame (translated to the centre,
rotated by -yaw):

- Normalised distance `d`: box = max(|p.x|/h.x, |p.y|/h.y, |p.z|/h.z); ellipsoid = length(p / h).
  Outside when `d >= 1`.
- Edge fade: `smoothstep(0, edgeFade, 1 - d)` (1 when `edgeFade` is 0 and `d < 1`).
- Height falloff: `exp(-heightFalloff * (p.y + h.y))`.
- Noise: sample the zone noise volume at `worldPos * noiseDetail / NoiseSize - WindOffset * noiseDetail`
  (wraps consistently because the noise tiles at 1 and `noiseDetail` is an integer), then
  `NoiseDensityFactor(noise, noiseAmount)`.
- `sigmaVolume = density * fade * height * noise`.

The froxel's lighting (`FogSource` + point/spot light scattering) is computed once. Output:
`rgb = radiance * (sigmaZone + sum(sigmaVolume * color))`, `a = sigmaZone + sum(sigmaVolume)`.

### Limits

- Volumes exist only inside the froxel range; a large volume crossing the range is cut there, and the analytic
  tail beyond ignores volumes.
- Debug view 3 (density) includes volumes; no new debug mode.

## 3. Tests and verification

### Unit tests

- `.hfog`: round trip with both shapes and every field; missing file = empty list; unknown chunk skipped;
  clamping of out-of-range values.
- `fog_volume_math.h`: box and ellipsoid distance with yaw; edge fade at centre, fade start and border; height
  falloff; time-of-day factor inside, outside, fading in and out, across midnight, always-on; noise offset
  scaling.
- CPU selection: inactive, out-of-range and off-frustum volumes dropped; cap keeps the nearest 64.
- Size asserts: GPU record 80 bytes, fog constant buffer 256 bytes.

### Visual verification

- A box of mist in an Oakenshire hollow or along the pond, placed in the editor, saved, visible in the client.
- A soft-edged ellipsoid; a tinted volume; morning mist fading out at the configured hour; god rays and
  lantern halos inside a volume.

## Out of scope

- Volumes beyond the froxel range, full 3D rotation, arbitrary meshes or height-field shapes.
- Per-volume curves over the day (a single active window with fades only).
- Server-side use of fog volumes.
