# Colour Grading (LUT and Sliders)

Environment profiles can grade the final image the way Unreal's legacy colour grading does: a
per-profile colour lookup table (LUT), plus three quick parametric sliders for tweaks that don't
need a LUT round-trip. Grades cross-fade at zone borders along with the rest of the environment
look.

## Where it runs

Grading happens in `TonemapPass` (`src/shared/deferred_shading/tonemap_pass.cpp`,
`shaders/PS_Tonemap.hlsl`), **after** exposure, ACES and gamma, and **before** dither:

```
hdr = scene + bloom * BloomScale
c = pow(ACES(hdr * Exposure), 1 / 2.2)              // unchanged tonemap
c *= ColorFilter                                    // colour filter
c = lerp(luma(c), c, Saturation)                    // saturation (Rec. 709 luma)
c = saturate((c - 0.5) * Contrast + 0.5)             // contrast around mid-grey
c = lerp(SampleLut(LutFrom, c), SampleLut(Lut, c), LutBlend)  // zone LUT cross-fade
c += dither                                          // unchanged
```

The sliders (colour filter, then saturation, then contrast) always run first, then the LUT is
applied on top of their result. This means the LUT is the final say on the image, and the sliders
still have an effect even when a profile has no LUT.

Because grading runs after gamma, it operates on display-referred (roughly perceptual) colour,
which is what an artist sees when grading a screenshot in an ordinary image editor.

Unlit forward effects render before the tonemap pass, into the HDR scene; they end up graded
because the tonemap pass grades the whole frame. The underwater `PostProcessPass`, which does
run downstream of the tonemap pass, also sees the graded image.

## Profile fields

Both `src/shared/proto_data/environment_profiles.proto` and the `client_data` mirror carry the
same field numbers:

| Field | Number | Type | Default | Range |
|---|---|---|---|---|
| `color_lut` | 26 | string | empty (`""`) | texture path of a strip LUT; empty = no LUT |
| `saturation` | 27 | float | 1 | clamped to [0, 2]; 0 = grey, 1 = unchanged |
| `contrast` | 28 | float | 1 | clamped to [0, 2]; contrast around mid-grey, 1 = unchanged |
| `color_filter_r` | 29 | float | 1 | clamped to [0, 2]; multiplies the graded colour |
| `color_filter_g` | 30 | float | 1 | clamped to [0, 2] |
| `color_filter_b` | 31 | float | 1 | clamped to [0, 2] |

The loader (`environment_profile_proto.h`) clamps saturation, contrast and each colour filter
channel to [0, 2] when building the runtime `EnvironmentProfile`; a slider value outside that
range never reaches the shader.

### Zone cross-fade

`EnvironmentState` carries the LUT as a pair rather than a single texture, because two LUTs
cannot be blended by averaging their texels the way the other profile curves can:

- `colorLut` - the target zone's LUT.
- `colorLutFrom` - the LUT of the strongest other profile currently blending in (empty if there
  is none).
- `colorLutBlend` - the weight of `colorLut`; `colorLutFrom` gets `1 - colorLutBlend`.

`EnvironmentController::Evaluate` picks this pair after folding all blending profiles together:
the target profile's LUT, faded in over the highest-weight non-target profile's LUT. The sliders
(saturation, contrast, colour filter) blend linearly like every other profile curve instead.
While crossing a zone border, both LUTs are sampled and the tonemap pass lerps between the two
results by `LutBlend`.

Only two LUTs ever blend at once. If a third zone is entered while a fade is still running, the
weakest blending profile's LUT drops out immediately in favour of the new zone's, producing a
small colour step, while the sliders (saturation, contrast, colour filter) stay continuous.

## Strip LUT format

A LUT is a single 2D texture that packs an `N x N x N` colour cube as a strip of `N` slices laid
out side by side, the way Unreal's legacy colour grading LUTs work:

- The texture is **`N*N` wide and `N` high** (`IsStripLut` in
  `src/shared/deferred_shading/color_grading.h` checks `width == height * height`).
- **Blue** selects which of the `N` slices to read (`slice = x // N`).
- **Red** selects the column inside that slice (`column = x % N`).
- **Green** selects the row (`y`).
- The tonemap pass samples mip 0 only, with bilinear filtering and clamp addressing, at texel
  centres; it takes two reads (the slices either side of the input blue value) and blends them by
  the fractional slice, so the strip layout never leaks a neighbouring slice into a read.
- Values are read back **unchanged** - there is no sRGB decode - so an 8-bit LUT must store
  `round(value * 255)` for each channel, matching what an ordinary image editor produces when it
  saves a PNG.

Two edge sizes are supported by the workflow below: `16` (a `256x16` texture) and `32`
(`1024x32`, the shipped neutral LUT). Larger strips work too as long as the width-equals-height-
squared rule holds.

### Rules

- Width must equal height squared, or the texture is rejected.
- The LUT must be **imported uncompressed** (compression quantizes the strip into blocks and
  destroys the exact addressing the lookup depends on).
- A LUT that fails to load, or whose dimensions don't satisfy `width == height * height`, is
  **ignored** (grading proceeds without it) and logs **one warning** per texture path - not one
  per frame.

## Authoring workflow

1. Take a screenshot of the zone you want to grade.
2. Paste `data/client/Textures/ColorGrading/NeutralLUT32.png` (1024x32, the identity grade) into
   the same image, next to or on top of the screenshot.
3. Grade the whole image (screenshot + LUT strip together) in any image editor, using its normal
   colour tools (curves, colour balance, etc.). Because the LUT strip is graded by the exact same
   operations as the screenshot, it ends up encoding the same transform.
4. Crop the strip back out at **exactly** `1024x32` (or `256x16` for a 16^3 LUT) and save it as a
   PNG. Any resizing or resampling here will corrupt the addressing - the crop must be pixel
   exact.
5. Import the cropped strip in the editor's texture import with **compression off**, then pick it
   as the profile's Color LUT in the Environment Profile Editor's "Color Grading" section.

The neutral LUT PNG and its imported `.htex` are shipped in `data/client` precisely so every
grading session starts from the same identity strip.

## Tool commands

`tools/color_grading/make_luts.py` (numpy + PIL) generates strip LUTs:

```bash
# Identity LUT: writes NeutralLUT32.png and NeutralLUT32.htex.
python tools/color_grading/make_luts.py neutral --size 32 --out-dir data/client/Textures/ColorGrading

# Strong warm, high-contrast test grade, for verification only - never shipped as content.
python tools/color_grading/make_luts.py test-grade --size 32 --out <path.htex>
```

`neutral` maps every texel back to its own strip coordinate, so applying it should be visually
identical to no LUT at all (within 8-bit rounding). `test-grade` applies a strong,
easy-to-spot transform (`r' = r^0.85 * 1.1`, `g' = g`, `b' = b^1.2 * 0.85`, clamped) so a visual
check can confirm the LUT path and the zone cross-fade are actually wired up.

## See also

- [docs/rendering-atmosphere.md](rendering-atmosphere.md) - the wider `DeferredRenderer` frame
  order, including where `TonemapPass` sits.
- `src/shared/deferred_shading/color_grading.h` - the strip addressing math, unit-tested and
  mirrored in `PS_Tonemap.hlsl`.
