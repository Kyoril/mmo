# Terrain Region Clipboard + Height Stamps — Design

**Date:** 2026-07-19
**Status:** Approved
**Goal:** Make hand-painting interesting terrain shapes easier and faster in the world editor, focused on the two biggest gaps: (1) no way to select, move, cut and copy terrain regions, and (2) cliffs/ridges/mountains are slow to shape with round falloff brushes.

## Background

Current terrain tooling in `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.{h,cpp}`:

- Deform: Sculpt, Smooth, Flatten (Ctrl picks height), Noise (fBm with preview)
- Paint: 4 splat layers with an importable greyscale brush mask (rotate/invert) — mask applies to painting only
- Brush: radius/hardness/power sliders; Shift+wheel = radius, Ctrl+wheel = hardness
- Area, Vertex Shading, Holes, Water sub-modes
- No undo/redo of any terrain edit; no region operations; no height stamping

Terrain data layout (from `src/shared/terrain/terrain.h`):

- **Heights** are stored per *outer* vertex on a global grid indexed via `GetHeightAt`/`SetHeightAt`, plus independently editable *inner* (cell-center) vertices (encoded with negative indices in `TerrainVertexBrush`).
- **Vertex colors** per vertex (`GetColorAt`/`SetColorAt`).
- **Splat coverage** lives in a separate global *pixel* grid (`constants::PixelsPerPage`), 4 layers packed per pixel (`GetLayersAt`/`SetLayerAt`).
- **Area IDs** and **holes** are per tile (`GetAreaForTile`/`SetAreaForTile`, `PaintHoles`/`IsHoleAt`).
- After edits, `UpdateTiles` / `UpdateTileCoverage` / `UpdateInnerVertices` refresh GPU data for a rect.

## Decisions (from brainstorm)

| Question | Decision |
|---|---|
| Copied data | Everything: heights + splats + vertex colors + holes + area IDs |
| Selection shape | Rectangle drag, snapped to outer-vertex grid |
| Paste edge treatment | Hard replace (no feathering) |
| Cut fill | Heights interpolated from selection border ("flatten to edge level") |
| Move UX | Ghost preview drag; click commits, Esc cancels; wheel = vertical offset |
| Undo scope | New operations only (cut/paste/move/stamp), not brush strokes |
| Cliff tooling | Height stamps from greyscale masks (reuse existing mask loader) |
| Architecture | One snapshot primitive powers clipboard, cut-fill and undo (Approach B) |

## 1. Core primitive — `TerrainRegionSnapshot`

New files in `src/shared/terrain/` (e.g. `terrain_region_snapshot.h/.cpp`).

```
struct TerrainRegionSnapshot
{
    // Global outer-vertex rect: minimum vertex index + size in *cells*.
    // The rect covers outer vertices [minVertX .. minVertX+sizeX] inclusive,
    // i.e. sizeX*sizeZ cells and (sizeX+1)*(sizeZ+1) outer vertices.
    int32 minVertX, minVertZ;
    int32 sizeX, sizeZ;

    std::vector<float>  outerHeights;   // (sizeX+1) * (sizeZ+1)
    std::vector<float>  innerHeights;   // sizeX * sizeZ (cell centers)
    std::vector<uint32> vertexColors;   // per outer vertex
    // World-aligned pixel rect derived from the vertex rect:
    int32 minPixelX, minPixelZ, pixelSizeX, pixelSizeZ;
    std::vector<uint32> splatPixels;    // packed 4-layer coverage per pixel
    // Tiles fully inside the vertex rect:
    int32 minTileX, minTileZ, tileSizeX, tileSizeZ;
    std::vector<uint32> areaIds;        // per tile
    std::vector<uint8>  holeFlags;      // per tile (format mirrors tile storage)
};
```

Inner heights are **captured**, not regenerated — inner vertices can be hand-sculpted and must survive a move. If reading inner heights requires new accessors on `Terrain`/`Page`, add them.

Three new `terrain::Terrain` methods:

- `TerrainRegionSnapshot CaptureRegion(rect)` — reads all channels; no mutation.
- `void ApplyRegion(const TerrainRegionSnapshot&, int32 destMinVertX, int32 destMinVertZ)` — hard 1:1 replace at the destination, clipped at terrain bounds. Tile data (area/holes) is written only for destination tiles fully covered by the pasted rect. Optional uniform height offset parameter (for the vertical-offset drag feature). Ends with a single `UpdateTiles` + `UpdateTileCoverage` + `UpdateInnerVertices` over the affected rect.
- `void FillRegionFromEdges(rect)` — the cut-fill: interior outer heights bilinearly interpolated from the rect's border vertices (for each interior vertex, blend the four border intersection heights of its row/column by normalized distance); inner vertices regenerated via `UpdateInnerVertices`. Splat coverage resets to base layer (layer 0 full), vertex colors to white, holes cleared. **Area IDs unchanged** — zoning is not a visual property and usually shouldn't move.

The pure math (rect intersection/clipping, vertex-rect → pixel-rect/tile-rect derivation, border bilinear interpolation) is factored into free functions so it can be unit-tested without a `Terrain` instance.

## 2. Selection UX (extends `TerrainEditType::Select`)

State machine inside `TerrainEditMode` (or a small helper class if it grows):

`Idle → Dragging (rubber band) → Selected → GhostDrag (move/paste) → Selected`

- **Rubber-band**: mouse-down on terrain starts a world-space rectangle; corners snap to the outer-vertex grid; drawn as a terrain-draped outline via `ManualRenderObject` lines (same technique as the brush circles). Mouse-up finalizes the selection.
- **Details panel** (Select mode): selection size in vertices and world units; buttons Copy, Cut, Paste, Move, Deselect; a numeric **vertical offset** field used by ghost drag.
- **Shortcuts** while the viewport is active and terrain Select mode is on: Ctrl+C copy, Ctrl+X cut, Ctrl+V paste, Esc cancels ghost drag or clears the selection, Ctrl+Z / Ctrl+Y (and Ctrl+Shift+Z) undo/redo.
- **Clipboard**: a single held `TerrainRegionSnapshot` (in-memory, editor session only).
- **Cut** immediately captures the region to the clipboard, pushes an undo entry, then `FillRegionFromEdges` on the source.
- **Move** = cut + paste as *one* undoable action, driven through ghost drag.
- **Ghost drag** (entered by Move or Paste): a translucent wireframe height-grid preview of the clipboard follows the cursor, snapped to the vertex grid. Rendered decimated (e.g. every Nth vertex line, capped line budget) so dragging stays fluid on big selections. Mouse wheel during the drag adjusts the vertical offset (also shown in the numeric field). Left-click commits: push undo entry (destination before-state; for Move also the source before-state), then `ApplyRegion` with the offset. Esc cancels with no mutation; for Move, cancel restores nothing because nothing was mutated yet — the source edge-fill happens only at commit time.
- Selecting a new rectangle or leaving Select mode exits ghost drag safely (equivalent to Esc).

**Deferred**: selection rotation, brush-painted/freeform selection masks, cross-session clipboard persistence, feathered paste blending.

## 3. Undo — `TerrainUndoStack` (editor-side)

New editor-side class (e.g. `src/mmo_edit/editors/world_editor/terrain_undo_stack.h/.cpp`):

- An undo entry = one or two `TerrainRegionSnapshot`s (destination before-state; plus source before-state for Move/Cut) + a human-readable label ("Move region", "Stamp").
- Bounded depth (16 entries); oldest dropped first. Snapshots are region-sized, so memory stays modest.
- **Undo**: capture the current state of the entry's rects as the redo entry, then `ApplyRegion` the stored before-snapshots at their original positions.
- **Redo**: symmetric.
- Scope: only the new operations (cut/paste/move/stamp) push entries. Brush strokes remain un-undoable this iteration, but the stack is deliberately operation-agnostic so per-stroke undo can adopt it later.

## 4. Height stamps — `TerrainDeformMode::Stamp`

- New deform mode `Stamp` in the existing deform-mode combo.
- The brush-mask UI (import/clear, name, invert, rotation, preview) — currently shown only for Paint — is refactored into a shared draw helper and shown for both Paint and Deform/Stamp.
- New **Strength** parameter (world-height units, e.g. slider 0.1–100) for stamp mode.
- **Behavior**: fires once on mouse-down (not continuously on hold). `heights += mask(u, v) × strength` over the square footprint of side `2 × brushRadius`, centered on the brush. **Shift inverts** the sign (carve craters/canyons). Each stamp pushes an undo entry first (bounding vertex rect of the footprint).
- New `Terrain::Stamp(float centerX, float centerZ, float outerRadius, float heightScale, const BrushMaskSampler&)` — reuses `TerrainVertexBrush` iteration but replaces the radial falloff with the mask sample: each vertex's world offset from the brush center maps to (u, v) in the footprint square, exactly mirroring how `Paint` maps mask coordinates today. Applies to outer and inner vertices.
- If no image mask is loaded, Stamp can use the existing fBm noise as its sampler (procedural mountain stamp) — the noise preview doubles as the stamp preview.
- **Deferred**: live 3D ghost preview of the stamp result before clicking (the 2D mask preview and the brush footprint outline serve for now), stamp libraries/preset browsing.

## 5. Testing & verification

- **Unit tests** (`unit_tests` target): the factored-out free functions — rect clipping, vertex→pixel/tile rect derivation, border bilinear interpolation for cut-fill, stamp UV mapping. No GPU/Scene required.
- **Manual editor checklist**: select/deselect; copy/paste with vertical offset; cut leaves seam-free edge-interpolated ground with base splat; move as single action; Esc cancels ghost drag without mutation; undo/redo of each op incl. stack-depth overflow; stamp raise/lower with rotation and invert; stamps and pastes crossing page boundaries; operations clipped at terrain edges; area overlay still correct after region ops.

## Out of scope (this iteration)

- Undo for brush strokes (Sculpt/Smooth/Flatten/Noise/Paint/Color/Holes)
- Selection rotation and non-rectangular selections
- Feathered/blended paste edges
- Ridge/canyon brush, terrace brush, erosion simulation (candidates for a future round)
- Live 3D stamp preview
