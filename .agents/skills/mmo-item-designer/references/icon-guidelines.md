# Item Icon Guidelines

Create a readable inventory icon proposal for every generated item.

## References

Use these bundled images as style references:

- `assets/reference-icons/leather_gloves.png`
- `assets/reference-icons/leather_pants.png`
- `assets/reference-icons/leather_chest.png`

They establish the rendering language, lighting, backdrop, and composition. Do not simply recolor or reproduce them when designing unrelated items.

## Required Style

- Square game inventory icon.
- One item or one visually coherent item group centered in frame.
- Object fills roughly `70%` to `85%` of the canvas while remaining fully visible.
- Three-quarter or front-facing view chosen for immediate silhouette recognition.
- Dark navy-to-black radial vignette background.
- Subtle cyan-blue rim glow around the silhouette.
- Warm hand-painted fantasy rendering with believable materials, visible brush-like shading, and clear material texture.
- Strong value contrast and readable silhouette at `32x32`.
- Soft directional lighting, restrained highlights, and modest ambient occlusion.
- The icon must read as finished production concept art, not flat vector art, clipart, geometric placeholder art, or a debug stand-in.
- No border, frame, slot decoration, rarity color, text, number, logo, watermark, character, hand, or environmental scene.
- No transparency unless the user explicitly requests it. Match the opaque sample background by default.

## Prompt Template

Use the built-in `image_gen` tool with a prompt shaped like:

```text
Use case: stylized-concept
Asset type: MMO inventory item icon
Primary request: Create an icon for <item name and concise visual identity>.
Input images: the three bundled leather equipment icons are style references only.
Scene/backdrop: dark navy-black radial vignette with a subtle cyan-blue halo immediately behind the item.
Subject: one centered <item>, fully visible, isolated, no character.
Style/medium: polished hand-painted fantasy game inventory icon, simplified readable silhouette, material-focused texture.
Composition/framing: square, centered, object occupies 70-85% of canvas, generous edge clearance.
Lighting/mood: soft directional light with warm highlights and restrained cool rim light.
Constraints: no text, no border, no frame, no badge, no watermark, no scenery, no cropped item.
```

Add concrete materials, wear, motifs, magical effects, and color accents derived from the designed item. Keep effects close to the object so the silhouette remains clear.

For weapons in particular, specify blade wear, edge highlights, grip wrapping, metal temperature, wood grain, pommel and guard construction, and any chips, dents, or forged asymmetry that help the icon feel painted rather than diagrammatic.

## Post-processing

Generate a square source image, then run `scripts/normalize_item_icon.ps1`. The script center-crops non-square inputs, resizes with high-quality filtering, writes an opaque RGB PNG, and verifies `128x128`.

Inspect the final PNG after resizing. Reject and regenerate when the subject is ambiguous, cropped, too small, overly detailed, framed, text-bearing, visually inconsistent with the references, or noticeably flatter / simpler than the `image_gen` draft.

Do not replace a weak or missing `image_gen` export with locally drawn fallback art. If the actual generated image file cannot be found, report that as a blocker and preserve the last good icon rather than silently downgrading quality.
