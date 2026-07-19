# Terrain Region Clipboard + Height Stamps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add terrain region select/cut/copy/move with ghost-preview drag and undo, plus greyscale height-stamp brushes, to the world editor.

**Architecture:** A `TerrainRegionSnapshot` struct captures every terrain channel (outer+inner heights, vertex colors, splat pixels, holes, area IDs) for a vertex-aligned rectangle. `Terrain::CaptureRegion` / `ApplyRegion` / `FillRegionFromEdges` are the only mutation primitives; the editor clipboard, cut-fill, and a bounded undo stack are all built from them. A new `TerrainEditType::Region` hosts the selection state machine; a new `TerrainDeformMode::Stamp` applies mask-driven height stamps. Pure index/interpolation math lives in a header-only `region_math` namespace with Catch2 unit tests.

**Tech Stack:** C++17, ImGui (editor UI), Catch2 (`src/unit_tests`), CMake/MSVC.

**Spec:** `docs/superpowers/specs/2026-07-19-terrain-region-clipboard-stamps-design.md`

**Deviation from spec (justified):** The spec said to extend `TerrainEditType::Select`. During planning I found `Select` mode already drives terrain-tile selection for material management (`PerformTerrainSelection` in `world_editor_instance.cpp:712`). To avoid breaking that workflow, region selection gets its own edit type `TerrainEditType::Region` ("Region Select" in the UI). Everything else follows the spec.

## Global Constraints

- Braces: Allman style, every `{`/`}` on its own line; if-blocks always braced.
- Indentation: tabs.
- Members `m_camelCase`; methods `PascalCase`; locals/free functions `camelCase`; files `snake_case`.
- Every source file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- `#pragma once` in headers; Doxygen comments on public members.
- No exceptions. Use `ASSERT`/`VERIFY` from `base/macros.h`; log with `DLOG`/`WLOG`/`ELOG`.
- Root namespace `mmo`; nested `namespace mmo { namespace terrain { ... } }` style (match surrounding files).
- CMake targets auto-glob their directories (`add_lib`/`add_exe` macros) — new files in existing library dirs need **no** CMakeLists changes.
- Build: `cmake --build build -t <target> --config Debug`. Test binaries land in `bin/` (multi-config generators may use `bin/Debug/`; check both).

## Key engine facts (read before any task)

- Outer-vertex grid: `constants::OuterVerticesPerPageSide` = 129 per page side → **128 cells per page side**. Global outer vertex `(vx, vz)`, accessed via `Terrain::GetHeightAt(vx, vz)` / `SetHeightAt(vx, vz, h)` (SetHeightAt mirrors shared edge vertices into neighbor pages automatically).
- Inner (cell-center) vertices: one per cell, global inner index `(ix, iz)` with `ix ∈ [0, width*128-1]`. Page mapping: `pageX = ix / 128`, `localX = ix % 128`, then `Page::GetInnerHeightAt/SetInnerHeightAt`. Brush callbacks encode inner vertices as negative indices `(-(ix+1), -(iz+1))`; `Terrain::GetColorAt`/`SetColorAt` already accept that encoding for inner colors.
- Splat pixels: separate grid, `constants::PixelsPerPage` = 1009 per page side → **1008 pixel cells per page**. `GetLayersAt(px, pz)` returns packed uint32 (4 × 8-bit layers, layer i at bits `8*i`); `SetLayerAt(px, pz, layer, float01)` writes one layer and mirrors page edges. Ratio pixel-cells : vertex-cells = 1008/128 = **63/8 exactly**.
- Holes: per *inner vertex*, stored as 64-bit masks per tile. `Page::IsHole(tileX, tileY, innerX, innerY)` / `SetHole(...)`; tile mapping from global inner index: `tileX = (ix % 128) / 8`, `innerX = ix % 8`.
- Area IDs: per tile. `Terrain::GetAreaForTile(gx, gy)` / `SetAreaForTile(gx, gy, area)`; global tile count per axis = `width * constants::TilesPerPage` (= width\*16). One tile spans 8 vertex cells.
- World mapping: terrain is centered; `worldX = vx * scaleV - halfW` where `scaleV = PageSize / 128`, `halfW = width * PageSize / 2`. Same for pixels with `scaleP = PageSize / 1008`.
- After height/color edits call `Terrain::UpdateTiles(fromVx, fromVz, toVx, toVz)`; after splat edits call `UpdateTileCoverage(fromPx, fromPz, toPx, toPz)` (private but callable from Terrain members). `UpdateInnerVertices(fromCell, fromCell, toCell, toCell)` regenerates inner heights by averaging the 4 surrounding outer vertices.
- All `Page` mutators no-op safely when the page is missing/unprepared; `Terrain::GetAt` does NOT null-check its page — in Capture loops use the same guarded pattern as `Terrain::Smooth` (fetch page, check `IsPrepared()`).
- Editor mouse flow: `WorldEditorInstance::OnMouseDown/Up` call `m_editMode->OnMouseDown/Up` (world_editor_instance.cpp:655, 682); `OnMouseHold` is called every frame while a button is held (line 462). Terrain raycast → `TerrainEditMode::SetBrushPosition` runs every mouse move except in `Select` type (line 765). `DrawViewportOverlay` runs every frame from viewport_panel.cpp:79.

---

### Task 1: Region math header + unit tests

**Files:**
- Create: `src/shared/terrain/terrain_region_math.h`
- Create: `src/unit_tests/test_terrain_region_math.cpp`

**Interfaces:**
- Produces (used by every later task):
  - `terrain::region_math::VertexRect { int32 minX, minZ, sizeX, sizeZ; bool IsEmpty() const; }`
  - `VertexRect ClampToBounds(const VertexRect&, int32 widthPages, int32 heightPages)`
  - `VertexRect VertexRectForBrush(float centerX, float centerZ, float radius, int32 widthPages, int32 heightPages)`
  - `int32 RoundWorldToVertex(float world, int32 pages)` / `float VertexToWorld(int32 v, int32 pages)`
  - `void PixelRangeInside(const VertexRect&, int32& minPX, int32& minPZ, int32& maxPX, int32& maxPZ)`
  - `void PixelRangePadded(const VertexRect&, int32& minPX, int32& minPZ, int32& maxPX, int32& maxPZ)`
  - `int32 MapDestPixelToSourcePixel(int32 destPixel, int32 srcMinVert, int32 destMinVert)`
  - `bool TileRangeFullyCovered(const VertexRect&, int32& minTX, int32& minTZ, int32& maxTX, int32& maxTZ)`
  - `int32 MapDestTileToSourceTile(int32 destTile, int32 srcMinVert, int32 destMinVert)`
  - `float CoonsHeight(float u, float v, float left, float right, float top, float bottom, float h00, float h10, float h01, float h11)`
  - Constants `CellsPerPage` (128), `PixelCellsPerPage` (1008), `PixelRatioNum` (63), `PixelRatioDen` (8)

- [ ] **Step 1: Write the failing tests**

Create `src/unit_tests/test_terrain_region_math.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "terrain/terrain_region_math.h"

using namespace mmo;
using namespace mmo::terrain::region_math;

TEST_CASE("ClampToBounds_Clamps_Negative_Origin", "[terrain_region]")
{
	// 1x1 page terrain = 128x128 cells
	const VertexRect r = ClampToBounds(VertexRect{ -10, -5, 50, 50 }, 1, 1);
	CHECK(r.minX == 0);
	CHECK(r.minZ == 0);
	CHECK(r.sizeX == 40);
	CHECK(r.sizeZ == 45);
}

TEST_CASE("ClampToBounds_Clamps_Overshoot", "[terrain_region]")
{
	const VertexRect r = ClampToBounds(VertexRect{ 100, 120, 50, 50 }, 1, 1);
	CHECK(r.minX == 100);
	CHECK(r.sizeX == 28);
	CHECK(r.minZ == 120);
	CHECK(r.sizeZ == 8);
}

TEST_CASE("ClampToBounds_Empty_When_Fully_Outside", "[terrain_region]")
{
	const VertexRect r = ClampToBounds(VertexRect{ 200, 0, 50, 50 }, 1, 1);
	CHECK(r.IsEmpty());
}

TEST_CASE("Vertex_World_Roundtrip", "[terrain_region]")
{
	// 2x2 pages: vertex 0 is at -PageSize, vertex 256 at +PageSize.
	const float w0 = VertexToWorld(0, 2);
	const float wMid = VertexToWorld(128, 2);
	CHECK(w0 == Catch::Approx(-terrain::constants::PageSize));
	CHECK(wMid == Catch::Approx(0.0));
	CHECK(RoundWorldToVertex(w0, 2) == 0);
	CHECK(RoundWorldToVertex(wMid + 0.1f, 2) == 128);
	// Clamped at bounds
	CHECK(RoundWorldToVertex(-1e6f, 2) == 0);
	CHECK(RoundWorldToVertex(1e6f, 2) == 256);
}

TEST_CASE("PixelRangeInside_Full_Page_Is_Full_Pixel_Grid", "[terrain_region]")
{
	int32 minPX, minPZ, maxPX, maxPZ;
	PixelRangeInside(VertexRect{ 0, 0, 128, 128 }, minPX, minPZ, maxPX, maxPZ);
	CHECK(minPX == 0);
	CHECK(minPZ == 0);
	CHECK(maxPX == 1008);
	CHECK(maxPZ == 1008);
}

TEST_CASE("PixelRangeInside_Uses_Exact_63_Over_8_Ratio", "[terrain_region]")
{
	// 8 vertex cells = exactly 63 pixel cells
	int32 minPX, minPZ, maxPX, maxPZ;
	PixelRangeInside(VertexRect{ 8, 16, 8, 8 }, minPX, minPZ, maxPX, maxPZ);
	CHECK(minPX == 63);
	CHECK(maxPX == 126);
	CHECK(minPZ == 126);
	CHECK(maxPZ == 189);
	// Non-aligned rect: pixels strictly inside the world span
	PixelRangeInside(VertexRect{ 1, 0, 1, 1 }, minPX, minPZ, maxPX, maxPZ);
	// 1 cell spans pixels [7.875, 15.75] -> inside range [8, 15]
	CHECK(minPX == 8);
	CHECK(maxPX == 15);
}

TEST_CASE("PixelRangePadded_Covers_World_Span", "[terrain_region]")
{
	int32 minPX, minPZ, maxPX, maxPZ;
	PixelRangePadded(VertexRect{ 1, 1, 1, 1 }, minPX, minPZ, maxPX, maxPZ);
	// world span [7.875, 15.75] -> padded [7, 16]
	CHECK(minPX == 7);
	CHECK(maxPX == 16);
	CHECK(minPZ == 7);
	CHECK(maxPZ == 16);
}

TEST_CASE("MapDestPixelToSourcePixel_Identity_And_Shift", "[terrain_region]")
{
	CHECK(MapDestPixelToSourcePixel(500, 32, 32) == 500);
	// 8-cell shift = exactly 63 pixels
	CHECK(MapDestPixelToSourcePixel(500, 40, 32) == 563);
	// 1-cell shift = 7.875 pixels, rounded
	CHECK(MapDestPixelToSourcePixel(500, 33, 32) == 508);
}

TEST_CASE("TileRangeFullyCovered_Aligned_And_Partial", "[terrain_region]")
{
	int32 minTX, minTZ, maxTX, maxTZ;
	// Tile-aligned rect (tiles are 8 cells): cells [8,24) = tiles [1,2]
	CHECK(TileRangeFullyCovered(VertexRect{ 8, 8, 16, 16 }, minTX, minTZ, maxTX, maxTZ));
	CHECK(minTX == 1);
	CHECK(maxTX == 2);
	// Off-by-one shrinks to fully covered tiles only
	CHECK(TileRangeFullyCovered(VertexRect{ 9, 8, 16, 16 }, minTX, minTZ, maxTX, maxTZ));
	CHECK(minTX == 2);
	CHECK(maxTX == 2);
	// Too small to cover any tile
	CHECK(!TileRangeFullyCovered(VertexRect{ 9, 9, 6, 6 }, minTX, minTZ, maxTX, maxTZ));
}

TEST_CASE("MapDestTileToSourceTile_Identity_And_Shift", "[terrain_region]")
{
	CHECK(MapDestTileToSourceTile(5, 16, 16) == 5);
	// 8-cell (one tile) shift
	CHECK(MapDestTileToSourceTile(5, 24, 16) == 6);
	// 4-cell (half-tile) shift: sampled center cell 5*8+4+4 = 48 lands in tile 6
	CHECK(MapDestTileToSourceTile(5, 20, 16) == 6);
}

TEST_CASE("CoonsHeight_Reproduces_Borders", "[terrain_region]")
{
	// Flat field: everything 3
	CHECK(CoonsHeight(0.3f, 0.7f, 3, 3, 3, 3, 3, 3, 3, 3) == Catch::Approx(3.0f));
	// u=0 must return the left border value regardless of right
	// (top/bottom evaluated at u=0 must equal corners h00/h01 for consistency)
	const float left = 5.0f;
	CHECK(CoonsHeight(0.0f, 0.5f, left, 99.0f, 5.0f, 5.0f, 5.0f, 7.0f, 5.0f, 7.0f) == Catch::Approx(left));
	// v=0 must return the top border value
	const float top = 4.0f;
	CHECK(CoonsHeight(0.5f, 0.0f, 4.0f, 4.0f, top, 99.0f, 4.0f, 4.0f, 8.0f, 8.0f) == Catch::Approx(top));
}

TEST_CASE("VertexRectForBrush_Covers_Footprint", "[terrain_region]")
{
	// 1x1 page terrain, brush at world center with radius = 1 cell size
	const float cellSize = static_cast<float>(terrain::constants::PageSize) / 128.0f;
	const VertexRect r = VertexRectForBrush(0.0f, 0.0f, cellSize, 1, 1);
	// Center is vertex 64; radius 1 cell -> at least cells [63,65)
	CHECK(r.minX <= 63);
	CHECK(r.minX + r.sizeX >= 65);
	CHECK(!r.IsEmpty());
}
```

Note: if the existing tests use `Approx` unqualified (Catch2 v2), match that — check `test_math.cpp` for the incumbent spelling and use the same.

- [ ] **Step 2: Run tests to verify they fail to compile**

Run: `cmake --build build -t unit_tests --config Debug`
Expected: compile error — `terrain/terrain_region_math.h` not found.

- [ ] **Step 3: Write the header**

Create `src/shared/terrain/terrain_region_math.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "constants.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace terrain
	{
		/// Pure, GPU-free index and interpolation math for terrain region operations
		/// (capture/apply/fill/stamp). Everything here is unit-testable in isolation.
		namespace region_math
		{
			/// Vertex cells per page side on the outer-vertex grid (128).
			constexpr int32 CellsPerPage = static_cast<int32>(constants::OuterVerticesPerPageSide) - 1;

			/// Pixel cells per page side on the coverage grid (1008).
			constexpr int32 PixelCellsPerPage = static_cast<int32>(constants::PixelsPerPage) - 1;

			/// Exact rational ratio between pixel cells and vertex cells: 1008/128 = 63/8.
			constexpr int32 PixelRatioNum = 63;
			constexpr int32 PixelRatioDen = 8;
			static_assert(CellsPerPage * PixelRatioNum == PixelCellsPerPage * PixelRatioDen,
				"Pixel/vertex grid ratio must be exactly 63/8");

			/// A rectangle on the global outer-vertex grid. minX/minZ are the minimum outer-vertex
			/// indices; sizeX/sizeZ count *cells*, so the rect covers outer vertices
			/// [minX .. minX+sizeX] inclusive and exactly sizeX*sizeZ inner (cell-center) vertices.
			struct VertexRect
			{
				int32 minX = 0;
				int32 minZ = 0;
				int32 sizeX = 0;
				int32 sizeZ = 0;

				/// True if the rect covers no cells.
				[[nodiscard]] bool IsEmpty() const
				{
					return sizeX <= 0 || sizeZ <= 0;
				}
			};

			/// Floor division that is correct for negative numerators.
			inline int32 floorDiv(const int32 a, const int32 b)
			{
				int32 q = a / b;
				if ((a % b != 0) && ((a < 0) != (b < 0)))
				{
					--q;
				}
				return q;
			}

			/// Ceiling division that is correct for negative numerators.
			inline int32 ceilDiv(const int32 a, const int32 b)
			{
				return -floorDiv(-a, b);
			}

			/// Clamps a rect to the terrain's cell bounds, shrinking it as needed.
			inline VertexRect ClampToBounds(const VertexRect& rect, const int32 widthPages, const int32 heightPages)
			{
				const int32 maxCellsX = widthPages * CellsPerPage;
				const int32 maxCellsZ = heightPages * CellsPerPage;

				const int32 x0 = std::clamp(rect.minX, 0, maxCellsX);
				const int32 z0 = std::clamp(rect.minZ, 0, maxCellsZ);
				const int32 x1 = std::clamp(rect.minX + rect.sizeX, 0, maxCellsX);
				const int32 z1 = std::clamp(rect.minZ + rect.sizeZ, 0, maxCellsZ);

				return VertexRect{ x0, z0, x1 - x0, z1 - z0 };
			}

			/// World position of a global outer vertex index (terrain is centered on the origin).
			inline float VertexToWorld(const int32 v, const int32 pages)
			{
				const double scale = constants::PageSize / static_cast<double>(CellsPerPage);
				const double half = pages * constants::PageSize * 0.5;
				return static_cast<float>(v * scale - half);
			}

			/// Nearest global outer vertex index for a world coordinate, clamped to bounds.
			inline int32 RoundWorldToVertex(const float world, const int32 pages)
			{
				const double scale = constants::PageSize / static_cast<double>(CellsPerPage);
				const double half = pages * constants::PageSize * 0.5;
				const int32 v = static_cast<int32>(std::lround((world + half) / scale));
				return std::clamp(v, 0, pages * CellsPerPage);
			}

			/// Smallest clamped vertex rect whose world footprint contains the circle
			/// (centerX, centerZ, radius).
			inline VertexRect VertexRectForBrush(const float centerX, const float centerZ, const float radius,
				const int32 widthPages, const int32 heightPages)
			{
				const double scale = constants::PageSize / static_cast<double>(CellsPerPage);
				const double halfW = widthPages * constants::PageSize * 0.5;
				const double halfH = heightPages * constants::PageSize * 0.5;

				const int32 x0 = static_cast<int32>(std::floor((centerX - radius + halfW) / scale));
				const int32 x1 = static_cast<int32>(std::ceil((centerX + radius + halfW) / scale));
				const int32 z0 = static_cast<int32>(std::floor((centerZ - radius + halfH) / scale));
				const int32 z1 = static_cast<int32>(std::ceil((centerZ + radius + halfH) / scale));

				return ClampToBounds(VertexRect{ x0, z0, x1 - x0, z1 - z0 }, widthPages, heightPages);
			}

			/// Inclusive pixel index range whose world positions lie inside the vertex rect.
			inline void PixelRangeInside(const VertexRect& rect, int32& minPX, int32& minPZ, int32& maxPX, int32& maxPZ)
			{
				minPX = ceilDiv(rect.minX * PixelRatioNum, PixelRatioDen);
				minPZ = ceilDiv(rect.minZ * PixelRatioNum, PixelRatioDen);
				maxPX = floorDiv((rect.minX + rect.sizeX) * PixelRatioNum, PixelRatioDen);
				maxPZ = floorDiv((rect.minZ + rect.sizeZ) * PixelRatioNum, PixelRatioDen);
			}

			/// Inclusive pixel index range that fully covers the vertex rect's world span,
			/// padded outward by up to one pixel for resampling.
			inline void PixelRangePadded(const VertexRect& rect, int32& minPX, int32& minPZ, int32& maxPX, int32& maxPZ)
			{
				minPX = floorDiv(rect.minX * PixelRatioNum, PixelRatioDen);
				minPZ = floorDiv(rect.minZ * PixelRatioNum, PixelRatioDen);
				maxPX = ceilDiv((rect.minX + rect.sizeX) * PixelRatioNum, PixelRatioDen);
				maxPZ = ceilDiv((rect.minZ + rect.sizeZ) * PixelRatioNum, PixelRatioDen);
			}

			/// Maps a destination pixel index to the nearest source pixel index for a paste whose
			/// source rect starts at srcMinVert and destination rect at destMinVert (same axis).
			inline int32 MapDestPixelToSourcePixel(const int32 destPixel, const int32 srcMinVert, const int32 destMinVert)
			{
				const double shift = (srcMinVert - destMinVert) * (static_cast<double>(PixelRatioNum) / PixelRatioDen);
				return static_cast<int32>(std::lround(destPixel + shift));
			}

			/// Inclusive range of tiles (8-cell squares) fully covered by the rect.
			/// Returns false if no tile is fully covered.
			inline bool TileRangeFullyCovered(const VertexRect& rect, int32& minTX, int32& minTZ, int32& maxTX, int32& maxTZ)
			{
				constexpr int32 cellsPerTile = static_cast<int32>(constants::OuterVerticesPerTileSide) - 1;
				minTX = ceilDiv(rect.minX, cellsPerTile);
				minTZ = ceilDiv(rect.minZ, cellsPerTile);
				maxTX = floorDiv(rect.minX + rect.sizeX, cellsPerTile) - 1;
				maxTZ = floorDiv(rect.minZ + rect.sizeZ, cellsPerTile) - 1;
				return maxTX >= minTX && maxTZ >= minTZ;
			}

			/// Maps a destination tile index to the source tile whose center corresponds to it
			/// after shifting by (srcMinVert - destMinVert) cells.
			inline int32 MapDestTileToSourceTile(const int32 destTile, const int32 srcMinVert, const int32 destMinVert)
			{
				constexpr int32 cellsPerTile = static_cast<int32>(constants::OuterVerticesPerTileSide) - 1;
				const int32 centerCell = destTile * cellsPerTile + cellsPerTile / 2 + (srcMinVert - destMinVert);
				return floorDiv(centerCell, cellsPerTile);
			}

			/// Bilinearly blended Coons patch: interpolates an interior height from the four border
			/// curves so that every border value is reproduced exactly.
			/// left/right are the border heights at parameter v on the x=min / x=max edges;
			/// top/bottom at parameter u on the z=min / z=max edges; hXY are the corners
			/// (h00 = min/min, h10 = max/min x/z ... h11 = max/max).
			inline float CoonsHeight(const float u, const float v,
				const float left, const float right, const float top, const float bottom,
				const float h00, const float h10, const float h01, const float h11)
			{
				return (1.0f - u) * left + u * right
					+ (1.0f - v) * top + v * bottom
					- ((1.0f - u) * (1.0f - v) * h00 + u * (1.0f - v) * h10
						+ (1.0f - u) * v * h01 + u * v * h11);
			}
		}
	}
}
```

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build build -t unit_tests --config Debug` then `bin/unit_tests.exe "[terrain_region]"` (or `bin/Debug/unit_tests.exe`).
Expected: all `[terrain_region]` tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/shared/terrain/terrain_region_math.h src/unit_tests/test_terrain_region_math.cpp
git commit -m "Add region math helpers for terrain region operations"
```

---

### Task 2: TerrainRegionSnapshot + Capture/Apply

**Files:**
- Create: `src/shared/terrain/terrain_region_snapshot.h`
- Modify: `src/shared/terrain/terrain.h` (new public methods, after the `Stamp`-free brush section around line 349)
- Modify: `src/shared/terrain/terrain.cpp`

**Interfaces:**
- Consumes: `region_math` from Task 1.
- Produces:
  - `struct terrain::TerrainRegionSnapshot` (fields below)
  - `TerrainRegionSnapshot Terrain::CaptureRegion(const region_math::VertexRect& rect)`
  - `void Terrain::ApplyRegion(const TerrainRegionSnapshot& snapshot, int32 destMinVertX, int32 destMinVertZ, float heightOffset = 0.0f)`
  - `float Terrain::GetInnerHeightAt(int32 ix, int32 iz) const` / `void Terrain::SetInnerHeightAt(int32 ix, int32 iz, float height) const`
  - `bool Terrain::IsHoleAtInnerVertex(int32 ix, int32 iz) const` / `void Terrain::SetHoleAtInnerVertex(int32 ix, int32 iz, bool hole) const`

- [ ] **Step 1: Create the snapshot struct**

Create `src/shared/terrain/terrain_region_snapshot.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain_region_math.h"

#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// A full capture of every terrain channel inside a vertex-aligned rectangle.
		/// Used as the clipboard for region copy/cut/paste, as the cut-fill source, and as
		/// the undo/redo payload. Produced by Terrain::CaptureRegion, consumed by
		/// Terrain::ApplyRegion.
		struct TerrainRegionSnapshot
		{
			/// The captured outer-vertex cell rect (see region_math::VertexRect semantics).
			region_math::VertexRect rect;

			/// Outer-vertex heights, (sizeX+1) * (sizeZ+1), row-major by z.
			std::vector<float> outerHeights;

			/// Outer-vertex colors (ARGB), same layout as outerHeights.
			std::vector<uint32> outerColors;

			/// Inner (cell-center) vertex heights, sizeX * sizeZ, row-major by z.
			std::vector<float> innerHeights;

			/// Inner vertex colors (ARGB), same layout as innerHeights.
			std::vector<uint32> innerColors;

			/// Hole flags per inner vertex (1 = hole), same layout as innerHeights.
			std::vector<uint8> holes;

			/// Padded splat pixel capture range (inclusive origin + counts).
			int32 minPixelX = 0;
			int32 minPixelZ = 0;
			int32 pixelCountX = 0;
			int32 pixelCountZ = 0;

			/// Packed 4-layer coverage per captured pixel, row-major by z.
			std::vector<uint32> splatPixels;

			/// Tiles fully covered by the rect (inclusive origin + counts; counts may be 0).
			int32 minTileX = 0;
			int32 minTileZ = 0;
			int32 tileCountX = 0;
			int32 tileCountZ = 0;

			/// Area IDs of the fully covered tiles, row-major by z.
			std::vector<uint32> areaIds;

			/// True if the snapshot covers at least one cell.
			[[nodiscard]] bool IsValid() const
			{
				return !rect.IsEmpty();
			}
		};
	}
}
```

- [ ] **Step 2: Declare the new Terrain methods**

In `src/shared/terrain/terrain.h`, add `#include "terrain_region_snapshot.h"` after the `constants.h` include, and declare after `void Color(...)` (line ~349):

```cpp
			/// @brief Gets the inner (cell-center) vertex height at global inner indices.
			/// @param ix Global inner vertex X index in [0, width * (OuterVerticesPerPageSide-1) - 1].
			/// @param iz Global inner vertex Z index.
			/// @return The inner vertex height, or 0.0f if out of bounds / page not prepared.
			[[nodiscard]] float GetInnerHeightAt(int32 ix, int32 iz) const;

			/// @brief Sets the inner (cell-center) vertex height at global inner indices.
			void SetInnerHeightAt(int32 ix, int32 iz, float height) const;

			/// @brief Returns whether the inner vertex at global inner indices is a terrain hole.
			[[nodiscard]] bool IsHoleAtInnerVertex(int32 ix, int32 iz) const;

			/// @brief Sets or clears the hole flag of the inner vertex at global inner indices.
			void SetHoleAtInnerVertex(int32 ix, int32 iz, bool hole) const;

			/// @brief Captures every terrain channel (heights, colors, splats, holes, area IDs)
			///        inside the given vertex rect. The rect is clamped to terrain bounds.
			/// @return The captured snapshot; IsValid() is false if the clamped rect is empty.
			TerrainRegionSnapshot CaptureRegion(const region_math::VertexRect& rect);

			/// @brief Hard-replaces terrain content at the destination with the snapshot.
			///        Writes are clipped at terrain bounds. Area IDs are written only for
			///        destination tiles fully covered by the pasted rect. Ends with a single
			///        UpdateTiles / UpdateTileCoverage refresh over the affected region.
			/// @param heightOffset Uniform offset added to every pasted height value.
			void ApplyRegion(const TerrainRegionSnapshot& snapshot, int32 destMinVertX, int32 destMinVertZ, float heightOffset = 0.0f);
```

- [ ] **Step 3: Implement the inner-vertex/hole accessors**

In `terrain.cpp` (place near `SetColorAt`, which shows the same mapping):

```cpp
		float Terrain::GetInnerHeightAt(const int32 ix, const int32 iz) const
		{
			const int32 maxInner = static_cast<int32>(m_width * (constants::OuterVerticesPerPageSide - 1)) - 1;
			if (ix < 0 || iz < 0 || ix > maxInner || iz > maxInner)
			{
				return 0.0f;
			}

			const uint32 pageX = static_cast<uint32>(ix) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 pageZ = static_cast<uint32>(iz) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 localX = static_cast<uint32>(ix) % (constants::OuterVerticesPerPageSide - 1);
			const uint32 localZ = static_cast<uint32>(iz) % (constants::OuterVerticesPerPageSide - 1);

			Page* page = GetPage(pageX, pageZ);
			if (!page || !page->IsPrepared())
			{
				return 0.0f;
			}

			return page->GetInnerHeightAt(localX, localZ);
		}

		void Terrain::SetInnerHeightAt(const int32 ix, const int32 iz, const float height) const
		{
			const int32 maxInner = static_cast<int32>(m_width * (constants::OuterVerticesPerPageSide - 1)) - 1;
			if (ix < 0 || iz < 0 || ix > maxInner || iz > maxInner)
			{
				return;
			}

			const uint32 pageX = static_cast<uint32>(ix) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 pageZ = static_cast<uint32>(iz) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 localX = static_cast<uint32>(ix) % (constants::OuterVerticesPerPageSide - 1);
			const uint32 localZ = static_cast<uint32>(iz) % (constants::OuterVerticesPerPageSide - 1);

			Page* page = GetPage(pageX, pageZ);
			if (page && page->IsPrepared())
			{
				page->SetInnerHeightAt(localX, localZ, height);
			}
		}

		bool Terrain::IsHoleAtInnerVertex(const int32 ix, const int32 iz) const
		{
			const int32 maxInner = static_cast<int32>(m_width * (constants::OuterVerticesPerPageSide - 1)) - 1;
			if (ix < 0 || iz < 0 || ix > maxInner || iz > maxInner)
			{
				return false;
			}

			const uint32 pageX = static_cast<uint32>(ix) / constants::InnerVerticesPerPageSide;
			const uint32 pageZ = static_cast<uint32>(iz) / constants::InnerVerticesPerPageSide;
			const uint32 remX = static_cast<uint32>(ix) % constants::InnerVerticesPerPageSide;
			const uint32 remZ = static_cast<uint32>(iz) % constants::InnerVerticesPerPageSide;

			Page* page = GetPage(pageX, pageZ);
			if (!page || !page->IsPrepared())
			{
				return false;
			}

			return page->IsHole(remX / constants::InnerVerticesPerTileSide, remZ / constants::InnerVerticesPerTileSide,
				remX % constants::InnerVerticesPerTileSide, remZ % constants::InnerVerticesPerTileSide);
		}

		void Terrain::SetHoleAtInnerVertex(const int32 ix, const int32 iz, const bool hole) const
		{
			const int32 maxInner = static_cast<int32>(m_width * (constants::OuterVerticesPerPageSide - 1)) - 1;
			if (ix < 0 || iz < 0 || ix > maxInner || iz > maxInner)
			{
				return;
			}

			const uint32 pageX = static_cast<uint32>(ix) / constants::InnerVerticesPerPageSide;
			const uint32 pageZ = static_cast<uint32>(iz) / constants::InnerVerticesPerPageSide;
			const uint32 remX = static_cast<uint32>(ix) % constants::InnerVerticesPerPageSide;
			const uint32 remZ = static_cast<uint32>(iz) % constants::InnerVerticesPerPageSide;

			Page* page = GetPage(pageX, pageZ);
			if (page && page->IsPrepared())
			{
				page->SetHole(remX / constants::InnerVerticesPerTileSide, remZ / constants::InnerVerticesPerTileSide,
					remX % constants::InnerVerticesPerTileSide, remZ % constants::InnerVerticesPerTileSide, hole);
			}
		}
```

Note: `InnerVerticesPerPageSide` (128) equals `OuterVerticesPerPageSide - 1`, so both mapping styles above are equivalent; the hole accessors use the inner-grid constants to make the tile decomposition obvious.

- [ ] **Step 4: Implement CaptureRegion**

In `terrain.cpp`:

```cpp
		TerrainRegionSnapshot Terrain::CaptureRegion(const region_math::VertexRect& rect)
		{
			TerrainRegionSnapshot snap;
			snap.rect = region_math::ClampToBounds(rect, static_cast<int32>(m_width), static_cast<int32>(m_height));
			if (snap.rect.IsEmpty())
			{
				return snap;
			}

			const int32 vx0 = snap.rect.minX;
			const int32 vz0 = snap.rect.minZ;
			const int32 sx = snap.rect.sizeX;
			const int32 sz = snap.rect.sizeZ;

			// Outer vertices: heights + colors.
			snap.outerHeights.resize(static_cast<size_t>(sx + 1) * (sz + 1));
			snap.outerColors.resize(snap.outerHeights.size());
			for (int32 z = 0; z <= sz; ++z)
			{
				for (int32 x = 0; x <= sx; ++x)
				{
					const size_t i = static_cast<size_t>(z) * (sx + 1) + x;
					snap.outerHeights[i] = GetHeightAt(vx0 + x, vz0 + z);
					snap.outerColors[i] = GetColorAt(vx0 + x, vz0 + z);
				}
			}

			// Inner vertices: heights + colors + hole flags.
			snap.innerHeights.resize(static_cast<size_t>(sx) * sz);
			snap.innerColors.resize(snap.innerHeights.size());
			snap.holes.resize(snap.innerHeights.size());
			for (int32 z = 0; z < sz; ++z)
			{
				for (int32 x = 0; x < sx; ++x)
				{
					const size_t i = static_cast<size_t>(z) * sx + x;
					const int32 ix = vx0 + x;
					const int32 iz = vz0 + z;
					snap.innerHeights[i] = GetInnerHeightAt(ix, iz);
					snap.innerColors[i] = GetColorAt(-(ix + 1), -(iz + 1));
					snap.holes[i] = IsHoleAtInnerVertex(ix, iz) ? 1 : 0;
				}
			}

			// Splat pixels (padded so pastes at unaligned offsets can resample).
			int32 minPX, minPZ, maxPX, maxPZ;
			region_math::PixelRangePadded(snap.rect, minPX, minPZ, maxPX, maxPZ);
			const int32 maxPixel = static_cast<int32>(m_width) * region_math::PixelCellsPerPage;
			minPX = std::clamp(minPX, 0, maxPixel);
			minPZ = std::clamp(minPZ, 0, maxPixel);
			maxPX = std::clamp(maxPX, 0, maxPixel);
			maxPZ = std::clamp(maxPZ, 0, maxPixel);

			snap.minPixelX = minPX;
			snap.minPixelZ = minPZ;
			snap.pixelCountX = maxPX - minPX + 1;
			snap.pixelCountZ = maxPZ - minPZ + 1;
			snap.splatPixels.resize(static_cast<size_t>(snap.pixelCountX) * snap.pixelCountZ);
			for (int32 z = 0; z < snap.pixelCountZ; ++z)
			{
				for (int32 x = 0; x < snap.pixelCountX; ++x)
				{
					snap.splatPixels[static_cast<size_t>(z) * snap.pixelCountX + x] =
						GetLayersAt(minPX + x, minPZ + z);
				}
			}

			// Area IDs of fully covered tiles.
			int32 minTX, minTZ, maxTX, maxTZ;
			if (region_math::TileRangeFullyCovered(snap.rect, minTX, minTZ, maxTX, maxTZ))
			{
				snap.minTileX = minTX;
				snap.minTileZ = minTZ;
				snap.tileCountX = maxTX - minTX + 1;
				snap.tileCountZ = maxTZ - minTZ + 1;
				snap.areaIds.resize(static_cast<size_t>(snap.tileCountX) * snap.tileCountZ);
				for (int32 z = 0; z < snap.tileCountZ; ++z)
				{
					for (int32 x = 0; x < snap.tileCountX; ++x)
					{
						snap.areaIds[static_cast<size_t>(z) * snap.tileCountX + x] =
							GetAreaForTile(minTX + x, minTZ + z);
					}
				}
			}

			return snap;
		}
```

- [ ] **Step 5: Implement ApplyRegion**

```cpp
		void Terrain::ApplyRegion(const TerrainRegionSnapshot& snap, const int32 destMinVertX, const int32 destMinVertZ, const float heightOffset)
		{
			if (!snap.IsValid())
			{
				return;
			}

			const int32 sx = snap.rect.sizeX;
			const int32 sz = snap.rect.sizeZ;
			const int32 maxVert = static_cast<int32>(m_width) * region_math::CellsPerPage;

			// Outer vertices (SetHeightAt / SetColorAt clip out-of-range indices themselves,
			// but skip early to avoid pointless page lookups).
			for (int32 z = 0; z <= sz; ++z)
			{
				const int32 dz = destMinVertZ + z;
				if (dz < 0 || dz > maxVert)
				{
					continue;
				}
				for (int32 x = 0; x <= sx; ++x)
				{
					const int32 dx = destMinVertX + x;
					if (dx < 0 || dx > maxVert)
					{
						continue;
					}
					const size_t i = static_cast<size_t>(z) * (sx + 1) + x;
					SetHeightAt(dx, dz, snap.outerHeights[i] + heightOffset);
					SetColorAt(dx, dz, snap.outerColors[i]);
				}
			}

			// Inner vertices + holes.
			for (int32 z = 0; z < sz; ++z)
			{
				const int32 dz = destMinVertZ + z;
				if (dz < 0 || dz >= maxVert)
				{
					continue;
				}
				for (int32 x = 0; x < sx; ++x)
				{
					const int32 dx = destMinVertX + x;
					if (dx < 0 || dx >= maxVert)
					{
						continue;
					}
					const size_t i = static_cast<size_t>(z) * sx + x;
					SetInnerHeightAt(dx, dz, snap.innerHeights[i] + heightOffset);
					SetColorAt(-(dx + 1), -(dz + 1), snap.innerColors[i]);
					SetHoleAtInnerVertex(dx, dz, snap.holes[i] != 0);
				}
			}

			// Splat coverage: nearest-resample the captured pixels into the destination range.
			const region_math::VertexRect destRect{ destMinVertX, destMinVertZ, sx, sz };
			int32 minPX, minPZ, maxPX, maxPZ;
			region_math::PixelRangeInside(destRect, minPX, minPZ, maxPX, maxPZ);
			const int32 maxPixel = static_cast<int32>(m_width) * region_math::PixelCellsPerPage;
			minPX = std::clamp(minPX, 0, maxPixel);
			minPZ = std::clamp(minPZ, 0, maxPixel);
			maxPX = std::clamp(maxPX, 0, maxPixel);
			maxPZ = std::clamp(maxPZ, 0, maxPixel);

			for (int32 pz = minPZ; pz <= maxPZ; ++pz)
			{
				const int32 spz = std::clamp(
					region_math::MapDestPixelToSourcePixel(pz, snap.rect.minZ, destMinVertZ) - snap.minPixelZ,
					0, snap.pixelCountZ - 1);
				for (int32 px = minPX; px <= maxPX; ++px)
				{
					const int32 spx = std::clamp(
						region_math::MapDestPixelToSourcePixel(px, snap.rect.minX, destMinVertX) - snap.minPixelX,
						0, snap.pixelCountX - 1);
					const uint32 packed = snap.splatPixels[static_cast<size_t>(spz) * snap.pixelCountX + spx];
					for (uint8 layer = 0; layer < 4; ++layer)
					{
						SetLayerAt(px, pz, layer, ((packed >> (8 * layer)) & 0xFF) / 255.0f);
					}
				}
			}

			// Area IDs: only destination tiles fully covered by the pasted rect.
			if (snap.tileCountX > 0 && snap.tileCountZ > 0)
			{
				int32 minTX, minTZ, maxTX, maxTZ;
				if (region_math::TileRangeFullyCovered(destRect, minTX, minTZ, maxTX, maxTZ))
				{
					const int32 maxTile = static_cast<int32>(m_width * constants::TilesPerPage) - 1;
					for (int32 tz = std::max(0, minTZ); tz <= std::min(maxTZ, maxTile); ++tz)
					{
						const int32 stz = region_math::MapDestTileToSourceTile(tz, snap.rect.minZ, destMinVertZ) - snap.minTileZ;
						if (stz < 0 || stz >= snap.tileCountZ)
						{
							continue;
						}
						for (int32 tx = std::max(0, minTX); tx <= std::min(maxTX, maxTile); ++tx)
						{
							const int32 stx = region_math::MapDestTileToSourceTile(tx, snap.rect.minX, destMinVertX) - snap.minTileX;
							if (stx < 0 || stx >= snap.tileCountX)
							{
								continue;
							}
							SetAreaForTile(tx, tz, snap.areaIds[static_cast<size_t>(stz) * snap.tileCountX + stx]);
						}
					}
				}
			}

			// Single refresh over the affected region (expanded by one vertex so normals of
			// neighboring cells pick up the new border heights).
			UpdateTiles(destMinVertX - 1, destMinVertZ - 1, destMinVertX + sx + 1, destMinVertZ + sz + 1);
			UpdateTileCoverage(minPX, minPZ, maxPX, maxPZ);
		}
```

Note: `UpdateTileCoverage` is private but `ApplyRegion` is a member, so calling it is fine. `terrain.cpp` already includes `<algorithm>` transitively; add `#include <algorithm>` explicitly at the top if missing.

- [ ] **Step 6: Build the terrain library and full editor**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build (mmo_edit links the terrain lib; this proves header + impl compile).

- [ ] **Step 7: Commit**

```bash
git add src/shared/terrain/terrain_region_snapshot.h src/shared/terrain/terrain.h src/shared/terrain/terrain.cpp
git commit -m "Add TerrainRegionSnapshot with CaptureRegion/ApplyRegion primitives"
```

---

### Task 3: FillRegionFromEdges (cut-fill)

**Files:**
- Modify: `src/shared/terrain/terrain.h` (declare after `ApplyRegion`)
- Modify: `src/shared/terrain/terrain.cpp`

**Interfaces:**
- Consumes: Task 1 math, Task 2 accessors.
- Produces: `void Terrain::FillRegionFromEdges(const region_math::VertexRect& rect)`

- [ ] **Step 1: Declare**

In `terrain.h` after `ApplyRegion`:

```cpp
			/// @brief Fills a region as if it were cut out: interior heights are Coons-patch
			///        interpolated from the rect's border vertices, inner vertices are regenerated,
			///        splat coverage is cleared to the base material, vertex colors reset to white
			///        and holes removed. Area IDs are left unchanged. Border vertices are untouched.
			void FillRegionFromEdges(const region_math::VertexRect& rect);
```

- [ ] **Step 2: Implement**

In `terrain.cpp`:

```cpp
		void Terrain::FillRegionFromEdges(const region_math::VertexRect& rect)
		{
			const region_math::VertexRect r = region_math::ClampToBounds(rect, static_cast<int32>(m_width), static_cast<int32>(m_height));
			if (r.sizeX < 2 || r.sizeZ < 2)
			{
				// No interior vertices to fill.
				return;
			}

			const int32 vx0 = r.minX;
			const int32 vz0 = r.minZ;
			const int32 sx = r.sizeX;
			const int32 sz = r.sizeZ;

			// Copy the border heights first — they are the interpolation source and stay fixed.
			std::vector<float> top(sx + 1), bottom(sx + 1), left(sz + 1), right(sz + 1);
			for (int32 x = 0; x <= sx; ++x)
			{
				top[x] = GetHeightAt(vx0 + x, vz0);
				bottom[x] = GetHeightAt(vx0 + x, vz0 + sz);
			}
			for (int32 z = 0; z <= sz; ++z)
			{
				left[z] = GetHeightAt(vx0, vz0 + z);
				right[z] = GetHeightAt(vx0 + sx, vz0 + z);
			}

			// Interior outer vertices: Coons interpolation + white color.
			for (int32 z = 1; z < sz; ++z)
			{
				const float v = static_cast<float>(z) / sz;
				for (int32 x = 1; x < sx; ++x)
				{
					const float u = static_cast<float>(x) / sx;
					const float h = region_math::CoonsHeight(u, v,
						left[z], right[z], top[x], bottom[x],
						top[0], top[sx], bottom[0], bottom[sx]);
					SetHeightAt(vx0 + x, vz0 + z, h);
					SetColorAt(vx0 + x, vz0 + z, 0xFFFFFFFFu);
				}
			}

			// Inner vertices: regenerate from the new outer heights, reset colors and holes.
			UpdateInnerVertices(vx0, vz0, vx0 + sx - 1, vz0 + sz - 1);
			for (int32 z = 0; z < sz; ++z)
			{
				for (int32 x = 0; x < sx; ++x)
				{
					SetColorAt(-(vx0 + x + 1), -(vz0 + z + 1), 0xFFFFFFFFu);
					SetHoleAtInnerVertex(vx0 + x, vz0 + z, false);
				}
			}

			// Splat coverage: clear all four paint layers so the base material shows.
			int32 minPX, minPZ, maxPX, maxPZ;
			region_math::PixelRangeInside(r, minPX, minPZ, maxPX, maxPZ);
			for (int32 pz = minPZ; pz <= maxPZ; ++pz)
			{
				for (int32 px = minPX; px <= maxPX; ++px)
				{
					for (uint8 layer = 0; layer < 4; ++layer)
					{
						SetLayerAt(px, pz, layer, 0.0f);
					}
				}
			}

			UpdateTiles(vx0 - 1, vz0 - 1, vx0 + sx + 1, vz0 + sz + 1);
			UpdateTileCoverage(minPX, minPZ, maxPX, maxPZ);
		}
```

- [ ] **Step 3: Build**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/shared/terrain/terrain.h src/shared/terrain/terrain.cpp
git commit -m "Add Terrain::FillRegionFromEdges cut-fill"
```

---

### Task 4: Terrain::Stamp

**Files:**
- Modify: `src/shared/terrain/terrain.h` (declare after `ApplyNoise`, line ~329)
- Modify: `src/shared/terrain/terrain.cpp` (implement near `ApplyNoise`)

**Interfaces:**
- Consumes: existing `TerrainVertexBrush`, `BrushMaskSampler`, `GetGlobalVertexWorldPosition`.
- Produces: `void Terrain::Stamp(float brushCenterX, float brushCenterZ, float outerRadius, float heightScale, const BrushMaskSampler& maskSampler)`

- [ ] **Step 1: Declare**

```cpp
			/// @brief Applies a single height stamp: every vertex inside the brush circle gets
			///        heights += maskSampler(u, v) * heightScale, where (u, v) span the square
			///        footprint of side 2*outerRadius (same mask mapping as Paint). There is no
			///        radial falloff — the mask fully defines the shape. Negative heightScale carves.
			void Stamp(float brushCenterX, float brushCenterZ, float outerRadius, float heightScale, const BrushMaskSampler& maskSampler);
```

- [ ] **Step 2: Implement**

```cpp
		void Terrain::Stamp(const float brushCenterX, const float brushCenterZ, const float outerRadius, const float heightScale, const BrushMaskSampler& maskSampler)
		{
			if (!maskSampler)
			{
				return;
			}

			const float maskExtent = outerRadius * 2.0f;
			const float invMaskExtent = maskExtent > 0.0f ? 1.0f / maskExtent : 0.0f;
			const float maskOriginX = brushCenterX - outerRadius;
			const float maskOriginZ = brushCenterZ - outerRadius;

			// Constant intensity: the mask alone shapes the stamp.
			const auto constantIntensity = [](const float, const float, const float)
			{
				return 1.0f;
			};

			TerrainVertexBrush(brushCenterX, brushCenterZ, outerRadius, outerRadius, true, constantIntensity,
				[&](const int32 vx, const int32 vy, const float)
				{
					float worldX = 0.0f, worldZ = 0.0f;
					if (vx >= 0 && vy >= 0)
					{
						GetGlobalVertexWorldPosition(vx, vy, &worldX, &worldZ);
					}
					else
					{
						// Inner vertex: world position is the average of the 4 surrounding corners.
						const int32 ix = -vx - 1;
						const int32 iz = -vy - 1;
						float v0x, v0z, v1x, v1z, v2x, v2z, v3x, v3z;
						GetGlobalVertexWorldPosition(ix, iz, &v0x, &v0z);
						GetGlobalVertexWorldPosition(ix + 1, iz, &v1x, &v1z);
						GetGlobalVertexWorldPosition(ix, iz + 1, &v2x, &v2z);
						GetGlobalVertexWorldPosition(ix + 1, iz + 1, &v3x, &v3z);
						worldX = (v0x + v1x + v2x + v3x) * 0.25f;
						worldZ = (v0z + v1z + v2z + v3z) * 0.25f;
					}

					const float u = (worldX - maskOriginX) * invMaskExtent;
					const float v = (worldZ - maskOriginZ) * invMaskExtent;
					const float factor = maskSampler(u, v);
					if (factor == 0.0f)
					{
						return;
					}

					if (vx >= 0 && vy >= 0)
					{
						SetHeightAt(vx, vy, GetHeightAt(vx, vy) + heightScale * factor);
					}
					else
					{
						const int32 ix = -vx - 1;
						const int32 iz = -vy - 1;
						SetInnerHeightAt(ix, iz, GetInnerHeightAt(ix, iz) + heightScale * factor);
					}
				});
		}
```

Note: like `Paint`, the effective footprint is the circle inscribed in the mask square (TerrainVertexBrush culls by circular distance). This matches the existing paint-mask behavior exactly.

- [ ] **Step 3: Build**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/shared/terrain/terrain.h src/shared/terrain/terrain.cpp
git commit -m "Add Terrain::Stamp mask-driven height stamping"
```

---

### Task 5: TerrainUndoStack

**Files:**
- Create: `src/mmo_edit/editors/world_editor/terrain_undo_stack.h`
- Create: `src/mmo_edit/editors/world_editor/terrain_undo_stack.cpp`

**Interfaces:**
- Consumes: `Terrain::CaptureRegion` / `ApplyRegion` (Task 2), `TerrainRegionSnapshot`.
- Produces (used by Tasks 7–8):
  - `void TerrainUndoStack::Push(String label, std::vector<terrain::TerrainRegionSnapshot> before)`
  - `bool CanUndo() const` / `bool CanRedo() const`
  - `const String* GetUndoLabel() const` / `const String* GetRedoLabel() const`
  - `void Undo(terrain::Terrain&)` / `void Redo(terrain::Terrain&)` / `void Clear()`

- [ ] **Step 1: Write the header**

`src/mmo_edit/editors/world_editor/terrain_undo_stack.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "terrain/terrain_region_snapshot.h"

#include <deque>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	/// Bounded undo/redo stack for region-based terrain operations (cut/paste/move/stamp).
	/// Each entry holds the before-state snapshots of every region an operation touched;
	/// undoing captures the current state as the redo entry, then re-applies the snapshots
	/// at their original positions.
	class TerrainUndoStack final : public NonCopyable
	{
	public:
		/// Maximum retained entries; the oldest entry is dropped beyond this.
		static constexpr size_t MaxEntries = 16;

		/// Registers a completed operation. `before` holds the state of each affected region
		/// captured immediately before the operation mutated it. Clears the redo stack.
		void Push(String label, std::vector<terrain::TerrainRegionSnapshot> before);

		/// True if there is an operation to undo.
		[[nodiscard]] bool CanUndo() const
		{
			return !m_undo.empty();
		}

		/// True if there is an operation to redo.
		[[nodiscard]] bool CanRedo() const
		{
			return !m_redo.empty();
		}

		/// Label of the next undo operation, or nullptr.
		[[nodiscard]] const String* GetUndoLabel() const;

		/// Label of the next redo operation, or nullptr.
		[[nodiscard]] const String* GetRedoLabel() const;

		/// Reverts the most recent operation and moves it to the redo stack.
		void Undo(terrain::Terrain& terrain);

		/// Re-applies the most recently undone operation.
		void Redo(terrain::Terrain& terrain);

		/// Drops all undo and redo entries.
		void Clear();

	private:
		struct Entry
		{
			String label;
			std::vector<terrain::TerrainRegionSnapshot> snapshots;
		};

		/// Captures the current terrain state of every region in `entry` (the counterpart
		/// entry that makes the operation reversible in the other direction).
		static Entry CaptureCounterpart(terrain::Terrain& terrain, const Entry& entry);

		/// Applies every snapshot of the entry at its original position.
		static void Apply(terrain::Terrain& terrain, const Entry& entry);

		std::deque<Entry> m_undo;
		std::deque<Entry> m_redo;
	};
}
```

- [ ] **Step 2: Write the implementation**

`src/mmo_edit/editors/world_editor/terrain_undo_stack.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_undo_stack.h"

#include "terrain/terrain.h"

namespace mmo
{
	void TerrainUndoStack::Push(String label, std::vector<terrain::TerrainRegionSnapshot> before)
	{
		// Drop invalid snapshots (e.g. selections clamped to nothing).
		std::erase_if(before, [](const terrain::TerrainRegionSnapshot& s)
		{
			return !s.IsValid();
		});

		if (before.empty())
		{
			return;
		}

		m_undo.push_back(Entry{ std::move(label), std::move(before) });
		while (m_undo.size() > MaxEntries)
		{
			m_undo.pop_front();
		}

		m_redo.clear();
	}

	const String* TerrainUndoStack::GetUndoLabel() const
	{
		return m_undo.empty() ? nullptr : &m_undo.back().label;
	}

	const String* TerrainUndoStack::GetRedoLabel() const
	{
		return m_redo.empty() ? nullptr : &m_redo.back().label;
	}

	void TerrainUndoStack::Undo(terrain::Terrain& terrain)
	{
		if (m_undo.empty())
		{
			return;
		}

		Entry entry = std::move(m_undo.back());
		m_undo.pop_back();

		m_redo.push_back(CaptureCounterpart(terrain, entry));
		Apply(terrain, entry);
	}

	void TerrainUndoStack::Redo(terrain::Terrain& terrain)
	{
		if (m_redo.empty())
		{
			return;
		}

		Entry entry = std::move(m_redo.back());
		m_redo.pop_back();

		m_undo.push_back(CaptureCounterpart(terrain, entry));
		while (m_undo.size() > MaxEntries)
		{
			m_undo.pop_front();
		}
		Apply(terrain, entry);
	}

	void TerrainUndoStack::Clear()
	{
		m_undo.clear();
		m_redo.clear();
	}

	TerrainUndoStack::Entry TerrainUndoStack::CaptureCounterpart(terrain::Terrain& terrain, const Entry& entry)
	{
		Entry counterpart;
		counterpart.label = entry.label;
		counterpart.snapshots.reserve(entry.snapshots.size());
		for (const auto& snapshot : entry.snapshots)
		{
			counterpart.snapshots.push_back(terrain.CaptureRegion(snapshot.rect));
		}
		return counterpart;
	}

	void TerrainUndoStack::Apply(terrain::Terrain& terrain, const Entry& entry)
	{
		for (const auto& snapshot : entry.snapshots)
		{
			terrain.ApplyRegion(snapshot, snapshot.rect.minX, snapshot.rect.minZ);
		}
	}
}
```

Note: `std::erase_if` on vectors requires C++20; if the project is strictly C++17, replace with `before.erase(std::remove_if(before.begin(), before.end(), ...), before.end());` and include `<algorithm>`. Check how other files in the repo do it (grep `erase_if`) and match.

- [ ] **Step 3: Build**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build (files are auto-globbed into the mmo_edit target).

- [ ] **Step 4: Commit**

```bash
git add src/mmo_edit/editors/world_editor/terrain_undo_stack.h src/mmo_edit/editors/world_editor/terrain_undo_stack.cpp
git commit -m "Add bounded TerrainUndoStack for region-based terrain operations"
```

---

### Task 6: Region edit type — selection rubber band

**Files:**
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.h`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.cpp`
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.cpp` (camera-rotation exclusion, line ~736)

**Interfaces:**
- Consumes: `region_math` (Task 1).
- Produces: `TerrainEditType::Region` enum value; selection state (`m_selection`, `m_regionState`) that Task 7 builds clipboard operations on; private methods `UpdateRegionOverlay()`, `ClearRegionSelection()`, `SelectionFromWorldCorners(a, b)`.

- [ ] **Step 1: Add the enum value and strings**

In `terrain_edit_mode.h`, insert into `TerrainEditType` directly after `Select`:

```cpp
		/// Rectangle-select terrain regions for copy/cut/paste/move operations.
		Region,
```

In `terrain_edit_mode.cpp`, insert `"Region Select",` after `"Select",` in `s_terrainEditModeStrings` (the `static_assert` keeps the arrays honest).

- [ ] **Step 2: Add state to the header**

In `terrain_edit_mode.h` add includes:

```cpp
#include "terrain/terrain_region_snapshot.h"
#include "editors/world_editor/terrain_undo_stack.h"
#include <optional>
```

(If the include root makes that path wrong, use `#include "../terrain_undo_stack.h"` — match how `world_edit_mode.h` is included by siblings.)

Add to the class (private section):

```cpp
		/// State machine for the Region edit type.
		enum class RegionEditState : uint8
		{
			/// Nothing selected.
			Idle,

			/// Left mouse held, rubber-banding the selection rectangle.
			Dragging,

			/// A selection rectangle exists.
			Selected,

			/// A clipboard ghost follows the cursor awaiting a commit click (move/paste).
			GhostDrag,
		};

		RegionEditState m_regionState = RegionEditState::Idle;
		terrain::region_math::VertexRect m_selection{};
		Vector3 m_regionDragStart{};
		std::optional<terrain::TerrainRegionSnapshot> m_clipboard;
		bool m_ghostIsMove = false;
		float m_ghostHeightOffset = 0.0f;
		ManualRenderObject* m_regionOverlay = nullptr;
		SceneNode* m_regionOverlayNode = nullptr;
		ManualRenderObject* m_ghostOverlay = nullptr;
		SceneNode* m_ghostOverlayNode = nullptr;
		TerrainUndoStack m_undoStack;
```

And private methods:

```cpp
		/// Converts two world-space drag corners into a clamped, vertex-snapped selection rect.
		[[nodiscard]] terrain::region_math::VertexRect SelectionFromWorldCorners(const Vector3& a, const Vector3& b) const;

		/// Rebuilds the draped selection-rectangle outline (or clears it when idle).
		void UpdateRegionOverlay();

		/// Clears the selection and any ghost drag, hiding both overlays.
		void ClearRegionSelection();
```

- [ ] **Step 3: Create/destroy the overlay objects**

In the constructor (after the area overlay creation), repeat the established pattern:

```cpp
		m_regionOverlay = m_worldEditor.CreateManualRenderObject("TerrainRegionOverlay");
		if (m_regionOverlay)
		{
			m_regionOverlay->SetCastShadows(false);
		}
		m_regionOverlayNode = m_worldEditor.CreateChildSceneNode();
		if (m_regionOverlayNode && m_regionOverlay)
		{
			m_regionOverlayNode->AttachObject(*m_regionOverlay);
		}

		m_ghostOverlay = m_worldEditor.CreateManualRenderObject("TerrainRegionGhost");
		if (m_ghostOverlay)
		{
			m_ghostOverlay->SetCastShadows(false);
		}
		m_ghostOverlayNode = m_worldEditor.CreateChildSceneNode();
		if (m_ghostOverlayNode && m_ghostOverlay)
		{
			m_ghostOverlayNode->AttachObject(*m_ghostOverlay);
		}
```

In the destructor, destroy both (same guarded pattern as `m_areaOverlay`/`m_areaOverlayNode`).

- [ ] **Step 4: Implement selection helpers**

In `terrain_edit_mode.cpp`:

```cpp
	terrain::region_math::VertexRect TerrainEditMode::SelectionFromWorldCorners(const Vector3& a, const Vector3& b) const
	{
		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());

		const int32 x0 = terrain::region_math::RoundWorldToVertex(std::min(a.x, b.x), pagesW);
		const int32 x1 = terrain::region_math::RoundWorldToVertex(std::max(a.x, b.x), pagesW);
		const int32 z0 = terrain::region_math::RoundWorldToVertex(std::min(a.z, b.z), pagesH);
		const int32 z1 = terrain::region_math::RoundWorldToVertex(std::max(a.z, b.z), pagesH);

		return terrain::region_math::VertexRect{ x0, z0, x1 - x0, z1 - z0 };
	}

	void TerrainEditMode::UpdateRegionOverlay()
	{
		if (!m_regionOverlay)
		{
			return;
		}

		m_regionOverlay->Clear();

		if (m_type != TerrainEditType::Region || m_regionState == RegionEditState::Idle || m_selection.IsEmpty())
		{
			return;
		}

		if (m_regionOverlayNode)
		{
			m_regionOverlayNode->SetPosition(Vector3::Zero);
		}

		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());
		constexpr float yBias = 0.2f;

		MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		auto lineOp = m_regionOverlay->AddLineListOperation(mat);

		// Draw the four draped edges; cap segments so huge selections stay cheap.
		const int32 stepX = std::max(1, m_selection.sizeX / 128);
		const int32 stepZ = std::max(1, m_selection.sizeZ / 128);

		auto edgePoint = [&](const int32 vx, const int32 vz) -> Vector3
		{
			const float wx = terrain::region_math::VertexToWorld(vx, pagesW);
			const float wz = terrain::region_math::VertexToWorld(vz, pagesH);
			return Vector3(wx, m_terrain.GetSmoothHeightAt(wx, wz) + yBias, wz);
		};

		constexpr uint32 selColor = 0xFFFFD800u; // selection yellow

		for (int32 x = 0; x < m_selection.sizeX; x += stepX)
		{
			const int32 x2 = std::min(x + stepX, m_selection.sizeX);
			auto& l1 = lineOp->AddLine(edgePoint(m_selection.minX + x, m_selection.minZ), edgePoint(m_selection.minX + x2, m_selection.minZ));
			l1.SetColor(selColor);
			auto& l2 = lineOp->AddLine(edgePoint(m_selection.minX + x, m_selection.minZ + m_selection.sizeZ), edgePoint(m_selection.minX + x2, m_selection.minZ + m_selection.sizeZ));
			l2.SetColor(selColor);
		}
		for (int32 z = 0; z < m_selection.sizeZ; z += stepZ)
		{
			const int32 z2 = std::min(z + stepZ, m_selection.sizeZ);
			auto& l1 = lineOp->AddLine(edgePoint(m_selection.minX, m_selection.minZ + z), edgePoint(m_selection.minX, m_selection.minZ + z2));
			l1.SetColor(selColor);
			auto& l2 = lineOp->AddLine(edgePoint(m_selection.minX + m_selection.sizeX, m_selection.minZ + z), edgePoint(m_selection.minX + m_selection.sizeX, m_selection.minZ + z2));
			l2.SetColor(selColor);
		}
	}

	void TerrainEditMode::ClearRegionSelection()
	{
		m_regionState = RegionEditState::Idle;
		m_selection = {};
		m_ghostIsMove = false;
		m_ghostHeightOffset = 0.0f;
		if (m_ghostOverlay)
		{
			m_ghostOverlay->Clear();
		}
		UpdateRegionOverlay();
	}
```

- [ ] **Step 5: Wire mouse handling**

In `TerrainEditMode::OnMouseDown`, after the water delegation block:

```cpp
		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::GhostDrag)
			{
				// Task 7 replaces this with CommitGhostDrag().
				return;
			}

			if (m_brushPositionValid)
			{
				m_regionDragStart = m_brushPosition;
				m_selection = SelectionFromWorldCorners(m_regionDragStart, m_brushPosition);
				m_regionState = RegionEditState::Dragging;
				UpdateRegionOverlay();
			}
			return;
		}
```

In `OnMouseHold`, insert before the deform handling (right after the water block):

```cpp
		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::Dragging && m_brushPositionValid)
			{
				m_selection = SelectionFromWorldCorners(m_regionDragStart, m_brushPosition);
				UpdateRegionOverlay();
			}
			return;
		}
```

In `OnMouseUp`, after the water block:

```cpp
		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::Dragging)
			{
				m_regionState = m_selection.IsEmpty() ? RegionEditState::Idle : RegionEditState::Selected;
				UpdateRegionOverlay();
			}
			return;
		}
```

In `UpdateBrushOverlay`, add an early-out after the water early-out so the brush circles/dots hide in Region mode:

```cpp
		// Region mode uses its own selection/ghost overlays; hide the brush visuals.
		if (m_type == TerrainEditType::Region)
		{
			return;
		}
```

In `DrawDetails`, in the type-change detection block (`if (m_type != m_lastTerrainType)`), add:

```cpp
			// Leaving Region mode drops any in-progress selection or ghost drag.
			if (previousType == TerrainEditType::Region)
			{
				ClearRegionSelection();
			}
```

Also in `DrawDetails`, exclude Region from the brush sliders — change `if (m_type != TerrainEditType::Water)` (around the Brush Radius sliders) to:

```cpp
		if (m_type != TerrainEditType::Water && m_type != TerrainEditType::Region)
```

- [ ] **Step 6: Stop camera rotation during region drags**

In `world_editor_instance.cpp` line ~736, the left-drag camera-rotation condition lists terrain types that must NOT rotate the camera. Add Region:

```cpp
				(m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Deform &&
				 m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Region &&
				 m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Paint &&
```

(No change needed at line 765: the `!= TerrainEditType::Select` check already lets Region receive brush-position raycasts.)

- [ ] **Step 7: Build and smoke-test**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build. Launch the editor, open a world, switch Terrain → "Region Select", drag on terrain: a yellow draped rectangle outline appears and persists after mouse-up; Esc handling comes in Task 7.

- [ ] **Step 8: Commit**

```bash
git add src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.h src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.cpp src/mmo_edit/editors/world_editor/world_editor_instance.cpp
git commit -m "Add Region Select terrain edit type with rubber-band selection"
```

---

### Task 7: Clipboard operations, ghost drag, undo integration, shortcuts

**Files:**
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.h`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.cpp`

**Interfaces:**
- Consumes: Tasks 2, 3, 5, 6 (`CaptureRegion`, `ApplyRegion`, `FillRegionFromEdges`, `TerrainUndoStack`, selection state).
- Produces: user-facing Copy/Cut/Paste/Move/undo/redo; private methods `CopySelection()`, `CutSelection()`, `BeginGhostDrag(bool isMove)`, `CommitGhostDrag()`, `CancelGhostDrag()`, `ComputeGhostDestRect()`, `UpdateGhostOverlay()`, `HandleShortcuts()`, `DrawRegionDetails()`.

- [ ] **Step 1: Declare the methods**

Add to the private section of `TerrainEditMode`:

```cpp
		/// True when a committed (non-empty, non-dragging) selection exists.
		[[nodiscard]] bool HasRegionSelection() const
		{
			return (m_regionState == RegionEditState::Selected || m_regionState == RegionEditState::GhostDrag)
				&& !m_selection.IsEmpty();
		}

		/// Captures the current selection into the clipboard.
		void CopySelection();

		/// Captures the selection into the clipboard, records undo, and edge-fills the source.
		void CutSelection();

		/// Enters ghost-drag mode. isMove additionally captures the selection as the move source
		/// so the commit can edge-fill it.
		void BeginGhostDrag(bool isMove);

		/// Applies the clipboard at the current ghost position (move also fills the source),
		/// recording a single undo entry.
		void CommitGhostDrag();

		/// Leaves ghost-drag mode without mutating the terrain.
		void CancelGhostDrag();

		/// Destination rect of the ghost, centered on the cursor and clamped so it fits.
		[[nodiscard]] terrain::region_math::VertexRect ComputeGhostDestRect() const;

		/// Rebuilds the translucent clipboard height-grid preview at the cursor.
		void UpdateGhostOverlay();

		/// Keyboard shortcuts: Ctrl+C/X/V, Esc, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y.
		void HandleShortcuts();

		/// Region-mode section of the details panel.
		void DrawRegionDetails();
```

- [ ] **Step 2: Implement clipboard + ghost logic**

```cpp
	void TerrainEditMode::CopySelection()
	{
		if (!HasRegionSelection())
		{
			return;
		}

		auto snapshot = m_terrain.CaptureRegion(m_selection);
		if (snapshot.IsValid())
		{
			m_clipboard = std::move(snapshot);
		}
	}

	void TerrainEditMode::CutSelection()
	{
		if (!HasRegionSelection())
		{
			return;
		}

		auto snapshot = m_terrain.CaptureRegion(m_selection);
		if (!snapshot.IsValid())
		{
			return;
		}

		m_clipboard = snapshot;
		m_undoStack.Push("Cut Region", { std::move(snapshot) });
		m_terrain.FillRegionFromEdges(m_selection);
		UpdateRegionOverlay();
	}

	void TerrainEditMode::BeginGhostDrag(const bool isMove)
	{
		if (isMove)
		{
			if (!HasRegionSelection())
			{
				return;
			}
			CopySelection();
		}

		if (!m_clipboard || !m_clipboard->IsValid())
		{
			return;
		}

		m_ghostIsMove = isMove;
		m_ghostHeightOffset = 0.0f;
		m_regionState = RegionEditState::GhostDrag;
		UpdateGhostOverlay();
	}

	terrain::region_math::VertexRect TerrainEditMode::ComputeGhostDestRect() const
	{
		if (!m_clipboard)
		{
			return {};
		}

		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());
		const int32 sizeX = m_clipboard->rect.sizeX;
		const int32 sizeZ = m_clipboard->rect.sizeZ;

		int32 minX = terrain::region_math::RoundWorldToVertex(m_brushPosition.x, pagesW) - sizeX / 2;
		int32 minZ = terrain::region_math::RoundWorldToVertex(m_brushPosition.z, pagesH) - sizeZ / 2;

		// Clamp so the whole rect stays inside the terrain without shrinking.
		minX = std::clamp(minX, 0, pagesW * terrain::region_math::CellsPerPage - sizeX);
		minZ = std::clamp(minZ, 0, pagesH * terrain::region_math::CellsPerPage - sizeZ);

		return terrain::region_math::VertexRect{ minX, minZ, sizeX, sizeZ };
	}

	void TerrainEditMode::CommitGhostDrag()
	{
		if (!m_clipboard || !m_clipboard->IsValid() || !m_brushPositionValid)
		{
			CancelGhostDrag();
			return;
		}

		const auto destRect = ComputeGhostDestRect();
		if (destRect.IsEmpty())
		{
			CancelGhostDrag();
			return;
		}

		std::vector<terrain::TerrainRegionSnapshot> before;
		before.push_back(m_terrain.CaptureRegion(destRect));
		if (m_ghostIsMove)
		{
			// The clipboard IS the source's before-state.
			before.push_back(*m_clipboard);
		}
		m_undoStack.Push(m_ghostIsMove ? "Move Region" : "Paste Region", std::move(before));

		if (m_ghostIsMove)
		{
			m_terrain.FillRegionFromEdges(m_clipboard->rect);
		}
		m_terrain.ApplyRegion(*m_clipboard, destRect.minX, destRect.minZ, m_ghostHeightOffset);

		// The pasted area becomes the new selection.
		m_selection = destRect;
		m_regionState = RegionEditState::Selected;
		m_ghostIsMove = false;
		if (m_ghostOverlay)
		{
			m_ghostOverlay->Clear();
		}
		UpdateRegionOverlay();
	}

	void TerrainEditMode::CancelGhostDrag()
	{
		if (m_regionState != RegionEditState::GhostDrag)
		{
			return;
		}

		m_ghostIsMove = false;
		m_regionState = m_selection.IsEmpty() ? RegionEditState::Idle : RegionEditState::Selected;
		if (m_ghostOverlay)
		{
			m_ghostOverlay->Clear();
		}
	}

	void TerrainEditMode::UpdateGhostOverlay()
	{
		if (!m_ghostOverlay)
		{
			return;
		}

		m_ghostOverlay->Clear();

		if (m_regionState != RegionEditState::GhostDrag || !m_clipboard || !m_brushPositionValid)
		{
			return;
		}

		if (m_ghostOverlayNode)
		{
			m_ghostOverlayNode->SetPosition(Vector3::Zero);
		}

		const auto destRect = ComputeGhostDestRect();
		if (destRect.IsEmpty())
		{
			return;
		}

		const int32 pagesW = static_cast<int32>(m_terrain.GetWidth());
		const int32 pagesH = static_cast<int32>(m_terrain.GetHeight());
		const int32 sizeX = destRect.sizeX;
		const int32 sizeZ = destRect.sizeZ;

		// Decimate so even page-sized ghosts stay around ~32x32 grid lines.
		const int32 step = std::max(1, std::max(sizeX, sizeZ) / 32);

		MaterialPtr mat = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		auto lineOp = m_ghostOverlay->AddLineListOperation(mat);

		auto ghostPoint = [&](const int32 x, const int32 z) -> Vector3
		{
			const float wx = terrain::region_math::VertexToWorld(destRect.minX + x, pagesW);
			const float wz = terrain::region_math::VertexToWorld(destRect.minZ + z, pagesH);
			const float h = m_clipboard->outerHeights[static_cast<size_t>(z) * (m_clipboard->rect.sizeX + 1) + x]
				+ m_ghostHeightOffset;
			return Vector3(wx, h, wz);
		};

		constexpr uint32 ghostColor = 0xFF40C8FFu; // ghost cyan

		for (int32 z = 0; z <= sizeZ; z += step)
		{
			const int32 zc = std::min(z, sizeZ);
			for (int32 x = 0; x < sizeX; x += step)
			{
				const int32 x2 = std::min(x + step, sizeX);
				auto& line = lineOp->AddLine(ghostPoint(x, zc), ghostPoint(x2, zc));
				line.SetColor(ghostColor);
			}
		}
		for (int32 x = 0; x <= sizeX; x += step)
		{
			const int32 xc = std::min(x, sizeX);
			for (int32 z = 0; z < sizeZ; z += step)
			{
				const int32 z2 = std::min(z + step, sizeZ);
				auto& line = lineOp->AddLine(ghostPoint(xc, z), ghostPoint(xc, z2));
				line.SetColor(ghostColor);
			}
		}
	}
```

- [ ] **Step 3: Route input**

Replace the Task 6 placeholder in `OnMouseDown`:

```cpp
			if (m_regionState == RegionEditState::GhostDrag)
			{
				CommitGhostDrag();
				return;
			}
```

In `SetBrushPosition` (which runs on every terrain raycast hit), keep the ghost following the cursor — change the body to:

```cpp
	void TerrainEditMode::SetBrushPosition(const Vector3& position)
	{
		m_brushPosition = position;
		m_brushPositionValid = true;
		UpdateBrushOverlay();

		if (m_type == TerrainEditType::Region && m_regionState == RegionEditState::GhostDrag)
		{
			UpdateGhostOverlay();
		}
	}
```

In `OnMouseWheel`, insert before the existing shift/ctrl handling:

```cpp
		if (m_type == TerrainEditType::Region)
		{
			if (m_regionState == RegionEditState::GhostDrag && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl)
			{
				m_ghostHeightOffset += delta * 0.5f;
				UpdateGhostOverlay();
			}
			return;
		}
```

In `DrawViewportOverlay`, call `HandleShortcuts();` as the very first statement (before the water/area early-outs) so shortcuts work in every terrain sub-mode:

```cpp
		HandleShortcuts();
```

- [ ] **Step 4: Implement shortcuts**

```cpp
	void TerrainEditMode::HandleShortcuts()
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput)
		{
			return;
		}

		// Undo/redo apply to all terrain sub-modes (they only cover region/stamp ops).
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			if (io.KeyShift)
			{
				m_undoStack.Redo(m_terrain);
			}
			else
			{
				m_undoStack.Undo(m_terrain);
			}
			UpdateRegionOverlay();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
		{
			m_undoStack.Redo(m_terrain);
			UpdateRegionOverlay();
		}

		if (m_type != TerrainEditType::Region)
		{
			return;
		}

		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			CopySelection();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
		{
			CutSelection();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
		{
			BeginGhostDrag(false);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			if (m_regionState == RegionEditState::GhostDrag)
			{
				CancelGhostDrag();
			}
			else
			{
				ClearRegionSelection();
			}
		}
	}
```

- [ ] **Step 5: Details-panel UI**

In `DrawDetails`, add a Region branch (after the Deform/Paint/Holes chain):

```cpp
		else if (m_type == TerrainEditType::Region)
		{
			DrawRegionDetails();
		}
```

And implement:

```cpp
	void TerrainEditMode::DrawRegionDetails()
	{
		if (HasRegionSelection())
		{
			const double cellSize = terrain::constants::PageSize / static_cast<double>(terrain::region_math::CellsPerPage);
			ImGui::Text("Selection: %d x %d cells (%.0f x %.0f units)",
				m_selection.sizeX, m_selection.sizeZ,
				m_selection.sizeX * cellSize, m_selection.sizeZ * cellSize);
		}
		else
		{
			ImGui::TextDisabled("Drag on the terrain to select a rectangle.");
		}

		const bool hasSelection = HasRegionSelection();
		const bool hasClipboard = m_clipboard && m_clipboard->IsValid();
		const bool ghostActive = m_regionState == RegionEditState::GhostDrag;

		ImGui::BeginDisabled(!hasSelection || ghostActive);
		if (ImGui::Button("Copy (Ctrl+C)"))
		{
			CopySelection();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cut (Ctrl+X)"))
		{
			CutSelection();
		}
		ImGui::SameLine();
		if (ImGui::Button("Move"))
		{
			BeginGhostDrag(true);
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!hasClipboard || ghostActive);
		if (ImGui::Button("Paste (Ctrl+V)"))
		{
			BeginGhostDrag(false);
		}
		ImGui::EndDisabled();

		if (ghostActive)
		{
			ImGui::TextDisabled("Click to place, Esc to cancel, mouse wheel adjusts height.");
			if (ImGui::InputFloat("Height Offset", &m_ghostHeightOffset, 0.5f, 5.0f, "%.1f"))
			{
				UpdateGhostOverlay();
			}
		}

		ImGui::BeginDisabled(!hasSelection || ghostActive);
		if (ImGui::Button("Deselect (Esc)"))
		{
			ClearRegionSelection();
		}
		ImGui::EndDisabled();
	}
```

Also add undo/redo buttons visible for all terrain sub-modes — in `DrawDetails`, right after the terrain-edit-mode combo (and after the type-change block):

```cpp
		{
			const String* undoLabel = m_undoStack.GetUndoLabel();
			const String* redoLabel = m_undoStack.GetRedoLabel();

			ImGui::BeginDisabled(!m_undoStack.CanUndo());
			if (ImGui::Button(undoLabel ? ("Undo " + *undoLabel + "##terrainUndo").c_str() : "Undo##terrainUndo"))
			{
				m_undoStack.Undo(m_terrain);
				UpdateRegionOverlay();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!m_undoStack.CanRedo());
			if (ImGui::Button(redoLabel ? ("Redo " + *redoLabel + "##terrainRedo").c_str() : "Redo##terrainRedo"))
			{
				m_undoStack.Redo(m_terrain);
				UpdateRegionOverlay();
			}
			ImGui::EndDisabled();
		}
```

- [ ] **Step 6: Build and functional check**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build. In the editor: select a region → Copy → Paste → ghost follows cursor → click places it; Cut leaves smoothly interpolated ground; Move relocates in one action; wheel changes ghost height; Esc cancels; Ctrl+Z/Ctrl+Y walk the operations both ways.

- [ ] **Step 7: Commit**

```bash
git add src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.h src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.cpp
git commit -m "Add terrain region copy/cut/paste/move with ghost drag and undo"
```

---

### Task 8: Stamp deform mode in the editor

**Files:**
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.h`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.cpp`

**Interfaces:**
- Consumes: `Terrain::Stamp` (Task 4), `TerrainUndoStack` (Task 5), `region_math::VertexRectForBrush` (Task 1), existing mask loader (`LoadBrushMask`/`SampleBrushMask`).
- Produces: `TerrainDeformMode::Stamp`; private methods `ApplyStamp()`, `DrawBrushMaskControls()`; member `float m_stampStrength`.

- [ ] **Step 1: Add the deform mode**

In `terrain_edit_mode.h`, add to `TerrainDeformMode` before `Count_`:

```cpp
		/// Applies a one-click height stamp shaped by the imported brush mask (or noise).
		Stamp,
```

Add the member and method declarations:

```cpp
		float m_stampStrength = 10.0f;

		/// Applies one stamp at the current brush position (mouse-down driven, undoable).
		void ApplyStamp();

		/// Shared brush-mask import/invert/rotation/preview controls (used by Paint and Stamp).
		void DrawBrushMaskControls();
```

In `terrain_edit_mode.cpp`, add `"Stamp"` to `s_terrainDeformModeStrings` (after `"Noise"`).

- [ ] **Step 2: Extract the mask UI into DrawBrushMaskControls**

Move the entire brush-mask block from the Paint branch of `DrawDetails` (from `ImGui::Separator(); ImGui::Checkbox("Use Brush Mask", ...)` down to the mask preview `ImGui::Image(...)` inclusive) into:

```cpp
	void TerrainEditMode::DrawBrushMaskControls()
	{
		// (moved code, unchanged)
	}
```

Call `DrawBrushMaskControls();` from the Paint branch where the block used to be.

- [ ] **Step 3: Stamp UI**

In the Deform branch of `DrawDetails`, after the deform-mode combo:

```cpp
			if (m_deformMode == TerrainDeformMode::Stamp)
			{
				ImGui::SliderFloat("Stamp Strength", &m_stampStrength, 0.1f, 100.0f, "%.1f");
				ImGui::TextDisabled("Click to stamp. Hold Shift to carve downward.");
				DrawBrushMaskControls();
			}
```

The noise-parameter block currently gated on `m_deformMode == TerrainDeformMode::Noise` also serves as the procedural fallback preview for stamps without a mask — change its condition to:

```cpp
			if (m_deformMode == TerrainDeformMode::Noise
				|| (m_deformMode == TerrainDeformMode::Stamp && (!m_useBrushMask || m_brushMaskData.empty())))
```

- [ ] **Step 4: Fire on mouse-down, not on hold**

In `OnMouseDown`, after the Region block from Task 6/7:

```cpp
		if (m_type == TerrainEditType::Deform && m_deformMode == TerrainDeformMode::Stamp && m_brushPositionValid)
		{
			ApplyStamp();
			return;
		}
```

In `OnMouseHold`'s deform `switch`, add an explicit no-op case so holding does not repeat the stamp:

```cpp
				case TerrainDeformMode::Stamp:
				{
					// One stamp per click — applied in OnMouseDown.
				} break;
```

- [ ] **Step 5: Implement ApplyStamp**

```cpp
	void TerrainEditMode::ApplyStamp()
	{
		const float radius = m_terrainBrushSize;
		const auto rect = terrain::region_math::VertexRectForBrush(
			m_brushPosition.x, m_brushPosition.z, radius,
			static_cast<int32>(m_terrain.GetWidth()), static_cast<int32>(m_terrain.GetHeight()));
		if (rect.IsEmpty())
		{
			return;
		}

		m_undoStack.Push("Stamp", { m_terrain.CaptureRegion(rect) });

		const float strength = ImGui::GetIO().KeyShift ? -m_stampStrength : m_stampStrength;

		terrain::BrushMaskSampler sampler;
		if (m_useBrushMask && !m_brushMaskData.empty())
		{
			sampler = [this](const float u, const float v)
			{
				return SampleBrushMask(u, v);
			};
		}
		else
		{
			// Procedural fallback: fBm noise with a radial falloff so the stamp blends
			// into the surroundings instead of leaving a square seam.
			sampler = [this](const float u, const float v)
			{
				if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
				{
					return 0.0f;
				}
				const float du = (u - 0.5f) * 2.0f;
				const float dv = (v - 0.5f) * 2.0f;
				const float falloff = std::max(0.0f, 1.0f - std::sqrt(du * du + dv * dv));
				const float n = (noise::fBm(u * m_noiseFrequency * 200.0f, v * m_noiseFrequency * 200.0f,
					m_noiseOctaves, m_noisePersistence) + 1.0f) * 0.5f;
				return n * falloff;
			};
		}

		m_terrain.Stamp(m_brushPosition.x, m_brushPosition.z, radius, strength, sampler);
	}
```

- [ ] **Step 6: Build and functional check**

Run: `cmake --build build -t mmo_edit --config Debug`
Expected: clean build. In the editor: Deform → Stamp → click raises a noise-shaped hill; import a greyscale mask → click stamps the mask shape; Shift-click carves; rotation slider rotates the stamp; Ctrl+Z reverts a stamp.

- [ ] **Step 7: Commit**

```bash
git add src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.h src/mmo_edit/editors/world_editor/edit_modes/terrain_edit_mode.cpp
git commit -m "Add Stamp deform mode with mask/noise height stamping and undo"
```

---

### Task 9: Full build, tests, manual verification checklist

**Files:**
- None created; verification only.

- [ ] **Step 1: Build everything and run all unit tests**

```powershell
cmake --build build --config Debug
bin/unit_tests.exe          # or bin/Debug/unit_tests.exe
```
Expected: clean build; all tests pass (including `[terrain_region]`).

- [ ] **Step 2: Manual editor verification**

Launch mmo_edit, open a world with terrain, and walk this checklist. Fix-and-recommit anything that fails:

1. Region Select: drag creates a draped yellow rectangle; a second drag replaces it; Esc clears it.
2. Copy → Paste: cyan ghost grid follows the cursor showing the copied heights; wheel raises/lowers it; numeric Height Offset field matches; click places an exact copy (heights, splat textures, vertex colors, holes, area overlay in Area mode).
3. Cut: source area becomes smoothly interpolated ground matching its border heights, base material, white vertex colors, no holes; area IDs unchanged.
4. Move: single action — source filled, destination replaced; Esc before the click leaves BOTH untouched.
5. Undo/redo: Ctrl+Z reverts paste, move (both ends), cut, and stamps; Ctrl+Y / Ctrl+Shift+Z re-applies; buttons show operation labels; 17+ operations silently drop the oldest.
6. Cross-page: select/cut/paste a region straddling a page boundary; no seams, neighbor page edges stay consistent.
7. Terrain edge: paste near the map border is clamped without crashing.
8. Stamp: no mask → noise hill with soft edges; imported mask → mask-shaped hill; Shift carves; Mask Rotation affects the stamp; holding the button does NOT repeat-stamp.
9. Other modes unaffected: Sculpt/Smooth/Flatten/Noise/Paint/Area/Holes/Water still behave as before; Select mode tile selection still works; left-drag in Region mode does not rotate the camera, in other modes camera behavior is unchanged.
10. Save + reload the world: pasted/stamped terrain persists.

- [ ] **Step 3: Commit any fixes**

```bash
git add -A src/
git commit -m "Fix issues found during terrain region/stamp verification"
```

(Skip if nothing needed fixing.)
