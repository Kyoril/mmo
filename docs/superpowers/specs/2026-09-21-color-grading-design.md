# Colour Grading (LUT and Sliders) in Environment Profiles — Design

Branch: `feature/fog-light-scattering` (follows the fog light scattering work on the same branch).

## Goal

Zones can grade the final image like Unreal's legacy colour grading: a per-profile colour lookup table
(LUT) authored in any image editor, plus quick parametric controls (saturation, contrast, colour filter)
for small tweaks without a LUT round-trip. Grades cross-fade at zone borders with the rest of the look.

Motivation: Falwyn Forest's fog and light shafts desaturate the forest. A saturation boost fixes that
case; LUTs give full artistic control for any zone.

## Current state (2026-09-21)

- `TonemapPass` (`deferred_shading/tonemap_pass.cpp`, `shaders/PS_Tonemap.hlsl`) is the last pass of the
  deferred frame: scene + bloom, exposure, ACES, gamma 1/2.2, dither. Its cbuffer (b2) is 16 bytes
  `{exposure, bloomScale, ditherStrength, pad}`. The underwater `PostProcessPass` runs after it.
- There is no colour grading or LUT code.
- Textures: PNGs are imported to `.htex` in the editor (`TextureImport`); compression is optional (off by
  default); power-of-two images always get mipmaps; there is no sRGB format, so 8-bit values read back
  unchanged. `TextureManager::Get().CreateOrRetrieve(path)` loads synchronously, caches, and returns
  `nullptr` for a missing file.
- `BindTexture` re-applies each texture's own legacy sampler state, so passes that need specific filtering
  use `SamplerState` objects (`GraphicsDevice::CreateSamplerState`) and `SampleLevel`.
- Environment profiles carry fields 1-25; `EnvironmentController::Evaluate` folds up to four weighted
  profiles pairwise through `LerpEnvironment`.

## 1. Data and authoring

### Profile fields

In both `proto_data/environment_profiles.proto` and the `client_data` mirror, same numbers:

```proto
	// Colour grading. Applied after tone mapping; see docs/color-grading.md.
	optional string color_lut = 26;                   // texture path of a strip LUT, empty = none
	optional float saturation = 27 [default = 1];     // 0 = grey, 1 = unchanged
	optional float contrast = 28 [default = 1];       // around mid-grey, 1 = unchanged
	optional float color_filter_r = 29 [default = 1]; // multiplies the graded colour
	optional float color_filter_g = 30 [default = 1];
	optional float color_filter_b = 31 [default = 1];
```

The loader clamps saturation, contrast and each filter channel to [0, 2].

### Runtime values

- `EnvironmentProfile`: `String colorLut; float saturation = 1; float contrast = 1; Vector3 colorFilter{1, 1, 1};`
- `EnvironmentState`: `saturation`, `contrast`, `colorFilter` (blended linearly in `LerpEnvironment`), plus
  the LUT pair `String colorLut` (the target profile's LUT), `String colorLutFrom` (the LUT being faded
  out) and `float colorLutBlend` (weight of `colorLut`, 0..1).
- `EnvironmentController::Evaluate` sets the LUT pair after the fold: `colorLut` = the target entry's LUT,
  `colorLutFrom` = the LUT of the highest-weight non-target entry (empty when there is none),
  `colorLutBlend` = the target entry's weight (1 when it is the only entry). A third blending profile's
  LUT is dropped; its sliders still blend.
- `EvaluateEnvironment(profile, time)` sets `colorLut = profile.colorLut`, `colorLutFrom = ""`,
  `colorLutBlend = 1`.

### Editor

Environment Profile Editor, new "Color Grading" section:
- LUT texture picker (`AssetPickerWidget`, as in the water profile editor) with a tooltip: strip LUT,
  256x16 (16^3) or 1024x32 (32^3), imported uncompressed.
- Saturation and Contrast sliders (0-2), Color Filter `ColorEdit3` (0-2 per channel via HDR edit).

### Authoring workflow (`docs/color-grading.md`)

1. Take a screenshot of the zone.
2. Paste `data/client/Textures/ColorGrading/NeutralLUT32.png` (1024x32, neutral) into it.
3. Grade the whole image in any image editor.
4. Crop the strip back out at exactly 1024x32 (or 256x16 for a 16^3 LUT) and save it as PNG.
5. Import it in the editor with compression off and pick it in the profile.

The neutral LUT PNG and its imported `.htex` are shipped in `data/client`.

### Content

Falwyn Forest (profile 1) gets `saturation = 1.15`; its LUT stays empty.

## 2. Tonemap pass

### Renderer API

- `deferred_shading/color_grading.h` (dependency-free apart from `base` and `math`): `ColorGradingSettings`
  `{ float saturation = 1; float contrast = 1; Vector3 colorFilter{1,1,1}; }` with clamping setters, and the
  strip-LUT addressing math mirrored by the shader.
- `DeferredRenderer::SetColorGrading(const ColorGradingSettings& settings, const String& lut,
  const String& lutFrom, float lutBlend)`, called each frame by the client and the world editor next to
  `SetExposure`. The tonemap pass loads LUT textures through `TextureManager` (cached). An empty path or a
  missing texture means "no LUT"; a missing file logs one warning per path.
- A texture whose width is not the square of its height is rejected the same way (one warning, no LUT).

### Constant buffer

`TonemapBuffer` (b2) grows from 16 to 48 bytes; `TonemapConstants` changes with it (`static_assert == 48`):

```hlsl
cbuffer TonemapBuffer : register(b2)
{
    float Exposure;
    float BloomScale;
    float DitherStrength;
    float Saturation;
    float3 ColorFilter;
    float Contrast;
    float LutBlend;     // weight of LutTexture; LutFromTexture gets 1 - LutBlend
    float LutSize;      // edge length of LutTexture, 0 = none
    float LutFromSize;  // edge length of LutFromTexture, 0 = none
    float _GradingPadding;
};
```

### Shader order

```
hdr = scene + bloom * BloomScale
c = pow(ACES(hdr * Exposure), 1 / 2.2)              // unchanged
c *= ColorFilter
c = lerp(dot(c, float3(0.2126, 0.7152, 0.0722)), c, Saturation)
c = saturate((c - 0.5) * Contrast + 0.5)
c = lerp(SampleLut(LutFromTexture, LutFromSize, c), SampleLut(LutTexture, LutSize, c), LutBlend)
c += dither                                          // unchanged
```

`SampleLut` returns its input unchanged when the size is 0.

### Strip sampling

For a LUT of edge `N` (texture `N*N` x `N`): blue selects the slice (`b * (N - 1)`), red the column inside the
slice, green the row. Texel centres: `u = (slice * N + r * (N - 1) + 0.5) / (N * N)`,
`v = (g * (N - 1) + 0.5) / N`. Two bilinear reads at mip 0 (the two slices around `b * (N - 1)`) are blended
by the fractional slice. The half-texel centring keeps every read inside its slice. LUTs are bound at `t2`
(`LutTexture`) and `t3` (`LutFromTexture`) with a dedicated linear-clamp `SamplerState` at `s1`. Unbound
slots use the pass's 1x1 black texture with size 0.

### Side effects

- The underwater post-process runs after the tonemap pass, so it sees the graded image.
- Unlit forward effects are graded with the scene.

## 3. Tests and verification

### Unit tests

- `color_grading.h`: settings clamps; strip texel addressing for N = 16 and N = 32; a simulated neutral strip
  (generated in the test, sampled with bilinear + slice blend exactly as the shader does) maps a grid of
  colours back to themselves within 1/255.
- Profile fields: defaults, loader clamps, linear blend of saturation / contrast / filter.
- `EnvironmentController`: LUT pair for a single profile (blend 1, from empty), during a fade (target LUT,
  previous LUT, weight), and with three profiles (strongest non-target kept).
- `TonemapConstants` size assert (48).

### Visual verification

- Falwyn Forest with the neutral LUT: identical to no LUT.
- Falwyn Forest at saturation 1.15.
- A strong test LUT (generated, not shipped): the grade applies; crossing the zone border fades it.

## Out of scope

- Time-of-day curves for grading values.
- HDR/log-space LUTs, more than two LUTs blending at once, per-camera or per-volume grading.
- An in-editor LUT painter or screenshot tool.
