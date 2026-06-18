# Terrain Detail Workflow

Use this workflow when terrain looks flat, repetitive, uniformly muddy, or disconnected from placed props.

## 1. Inspect The Active Instance

Start with the HMI assigned to the terrain, not a similarly named HMAT.

Record:

- Parent material
- Splat/control texture
- Every albedo and normal layer
- Per-layer tiling
- Per-layer strength
- Roughness and other scalar overrides

For the current Falwyn Plains terrain instances, `Models/Grass.hmat` exposes four albedo layers, four normal layers, a splat texture, tiling values, strength values, and default roughness.

## 2. Preview Every Texture

Render PNG previews for:

- Splat/control map
- Each albedo
- Each normal
- Any masks used for macro variation

Check:

- Whether the albedo itself contains useful scale variation
- Whether the normal has meaningful medium and fine detail
- Whether the normal is sampled with sampler type `1`
- Whether tiling creates visible repetition at gameplay camera distance
- Whether the splat has soft, organic boundaries instead of broad uniform fills

## 3. Tune The Instance First

Use HMI overrides for the lowest-risk iteration:

- Increase tiling when texels appear stretched or blurry.
- Decrease tiling when the pattern repeats too obviously.
- Reduce a layer's strength when one surface blankets the whole valley.
- Use a rougher dry soil and a smoother/darker wet mud layer instead of one uniform brown layer.
- Keep normal strength believable; strong normal maps cannot replace missing color and macro variation.

Make one controlled change at a time and compare screenshots from the same camera and lighting.

## 4. Improve Splatting

Material quality cannot compensate for a weak control map.

For a boar wallow or muddy valley:

- Put the wettest mud in depressions, trails, and around water.
- Break the edge with grass islands and irregular tongues.
- Reserve compacted soil for travel lines and feeding areas.
- Keep dry soil near rocks, roots, and slightly raised ground.
- Avoid a single broad, evenly painted oval.

## 5. Add Graph-Level Detail When Needed

Change the HMAT only when the HMI lacks necessary controls. Useful additions include:

- A second, larger-scale color variation texture multiplied subtly into base color.
- A detail normal sampled at higher frequency and blended with the layer normal.
- World-position noise to disrupt visible UV repetition.
- Roughness variation linked to mud wetness.
- Parameter nodes for macro tiling, detail tiling, detail strength, wetness, and tint.

Use existing nodes from the live catalog. Prefer exposing controls as parameters so future valley variants can be HMI-only.

After graph changes, save in `mmo_edit` to regenerate compiled shaders and runtime parameter tables.

## 6. Judge In Context

Review at:

- Gameplay camera height
- Low grazing angles where normal and roughness response are visible
- Bright daylight and shadow
- Near and far distances

Terrain detail also depends on geometry and placement. Use material work together with:

- Shallow terrain depressions and drainage lines
- Small rock and root clusters
- Fallen branches, churned earth decals, puddles, and reeds
- Denser edge vegetation and deliberate clearings
- Landmark composition around Grimtusk rather than uniform scatter
