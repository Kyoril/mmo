# PCG Oasis Proof of Concept Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An editor-only "Procedural" mode in which a closed spline (an *oasis*) deterministically
shapes, paints and waters the terrain and places rocks and trees, rebuildable with a button and
fully removable, plus a shared spline editor reused by the river flow tool.

**Architecture:** Spline data lives in `shared/math` (pure). A new headless library `shared/pcg`
holds the deterministic hash/noise, the oasis rule set as pure functions over world positions, the
non-destructive terrain contribution (apply/unapply against an abstract terrain interface), the
placement reconciliation and the `.hpcg` record serializer — all unit-tested with fakes. `mmo_edit`
adds a reusable `SplineEditController`, moves river editing onto it, implements the terrain and
placement adapters, the `ProceduralEditMode` and an unattended `--pcg-rebuild` flag.

**Tech Stack:** C++17, CMake macros (`add_lib`, `mmo_add_test`), Catch2, ImGui, nlohmann/json
(`deps/json`), `binary_io` writer/reader.

**Spec:** `docs/superpowers/specs/2026-10-24-pcg-oasis-poc-design.md`

## Global Constraints

- Branch: all work on `feature/pcg-oasis-poc`. No `/ship`, no merge to `develop`, no push. Data
  submodule commits for the test oasis go on a branch `feature/pcg-oasis-poc` inside the submodule,
  never on its `master`.
- Editor-only: `pcg` is linked only by `mmo_edit` and `pcg_tests`, never by `mmo_client` or servers.
  PCG files live under `data/client/Editor/Pcg/` (not shipped).
- Determinism: no `std::*_distribution`, no `std::random_device`, no global `RandomGenerator`, no
  `unordered_*` containers or pointer order on any output path. Same inputs + seed → identical output.
- Terrain splat layer order for `Desert_Terrain.hmat`: layer 0 = R sand, 1 = G dirt, 2 = B desert
  grass, 3 = A grass (verify against the material in Task 11 before tuning).
- Grass is never placed as instances; it comes from painting layer 3.
- Code style: Allman braces, tabs, braces on every `if`, `m_camelCase` members, `PascalCase`
  methods, `#pragma once`, Doxygen on public members, header
  `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`, namespace `mmo` (`mmo::pcg`).
- No exceptions; use `ASSERT`/`VERIFY`/`ELOG`/`WLOG`.
- Tooling is C++ only (no new committed Python).

## Review Focus

1. **Oasis across a page border or page corner** — seam vertices and splat pixels must stay
   identical in both pages and the result must match an oasis away from borders. (Task 11 step.)
2. **Degenerate loops** (fewer than 3 points, zero area, self-intersecting) — Rebuild must refuse
   with a message and change nothing. (Task 5 test.)
3. **User deleted a generated rock/tree by hand, then rebuilds or deletes the oasis** — rebuild
   recreates it, Delete does not fail or leave records behind. (Task 7 test.)
4. **Two oases whose influence rects overlap** — the second rebuild is refused and the world is
   untouched. (Task 9 test via `pcg::RectsOverlap`, and manual check.)
5. **Editor restarted, pages not streamed in, then Delete/Rebuild** — every page in the old and new
   influence rects is loaded first; nothing is applied to an unloaded page. (Task 9 manual step.)

---

### Task 1: `PathSpline` — shared spline data and evaluation

**Files:**
- Create: `src/shared/math/path_spline.h`, `src/shared/math/path_spline.cpp`
- Test: `src/tests/math_tests/path_spline_tests.cpp`

**Interfaces:**
- Produces:

```cpp
namespace mmo
{
	enum class SplineInterpolation : uint8 { Linear, CatmullRom };

	/// One control point. `width` is a generic per-point scalar (rivers: channel half width).
	struct SplinePoint { float x = 0.0f; float z = 0.0f; float width = 0.0f; };

	struct PathSpline
	{
		std::vector<SplinePoint> points;
		bool closed = false;
		SplineInterpolation interpolation = SplineInterpolation::Linear;
	};

	/// One sample of an evaluated spline; `distance` is the arc length from the first point.
	struct SplineSample { float x; float z; float width; float distance; };

	/// A spline evaluated into a polyline; all queries run on the polyline.
	class SampledPath
	{
	public:
		SampledPath(const PathSpline& spline, float maxStep);
		const std::vector<SplineSample>& GetSamples() const;
		float GetLength() const;
		bool IsClosed() const;
		/// Closed: negative inside, positive outside. Open: unsigned distance.
		float SignedDistance(float x, float z) const;
		bool Contains(float x, float z) const;              // closed only, false for open
		SplineSample SampleAtDistance(float distance) const; // wraps for closed
		/// Outward unit normal (closed, either winding) / left normal (open) at an arc distance.
		void NormalAtDistance(float distance, float& nx, float& nz) const;
		void GetBounds(float& minX, float& minZ, float& maxX, float& maxZ) const;
		/// Index of the control point segment a sample belongs to (for locality checks).
		uint32 SegmentAtDistance(float distance) const;
		bool IsSelfIntersecting() const;
	};
}
```

- [ ] **Step 0: Create the branch**

Run: `git switch -c feature/pcg-oasis-poc` in the worktree (the spec commit `4ba83e5b` is HEAD).

- [ ] **Step 1: Write failing tests** in `path_spline_tests.cpp`, tag `[path_spline]`:
  - `Linear closed square 10x10 at origin`: length == 40 (±1e-4); `Contains(5,5)`; `SignedDistance(5,5) == -5`; `SignedDistance(15,5) == 5`; bounds (0,0,10,10).
  - `Linear open path`: points (0,0),(10,0),(10,10): length 20; `SignedDistance(5,3) == 3`; `Contains` false.
  - `Catmull-Rom passes through control points`: closed 4-point diamond, every control point has a sample within 1e-3.
  - `Catmull-Rom circle area`: 16 points on r=40 circle; `Contains(0,0)`; `|SignedDistance(0,0) + 40| < 0.5`.
  - `Outward normal`: square, normal at distance 5 (bottom edge midpoint, both windings) points to −z.
  - `Width interpolates linearly between points` on the linear open path with widths 2 and 6 → 4 at distance 5.
  - `Self intersection`: bow-tie (0,0),(10,10),(10,0),(0,10) closed → true; square → false.
  - `SampleAtDistance wraps` for closed: distance 45 on the square equals distance 5.

- [ ] **Step 2: Run, expect FAIL**

Run: `cmake --build build --config Debug -t math_tests && bin/Debug/math_tests "[path_spline]"`
Expected: compile error / failures (type not defined).

- [ ] **Step 3: Implement `path_spline.{h,cpp}`**

Uniform Catmull-Rom (tension 0.5), closed wraps neighbours, open duplicates end points. Sampling
subdivides each segment into `ceil(segmentChordLength / maxStep)` steps. Inside test: even-odd on the
polyline. No caching across calls besides the samples. The math library globs its directory, so no
CMake change.

- [ ] **Step 4: Run, expect PASS** — same command, all `[path_spline]` pass.

- [ ] **Step 5: Commit** `feat(math): PathSpline data and sampled path queries`

---

### Task 2: Shared spline editing in the editor, rivers moved onto it

**Files:**
- Modify: `src/shared/terrain_io/water_flow.h/.cpp` (conversion functions)
- Test: `src/tests/terrain_io_tests/water_flow_spline_tests.cpp`
- Create: `src/mmo_edit/editors/world_editor/spline_edit_controller.h/.cpp`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/water_edit_mode.h/.cpp`

**Interfaces:**
- Consumes: `PathSpline`, `SampledPath` (Task 1).
- Produces:

```cpp
// terrain_io::water_flow
PathSpline ToSpline(const FlowPath& path);      // Linear, open, width = halfWidth
FlowPath FromSpline(const PathSpline& spline);  // exact inverse for Linear splines

// mmo_edit
class SplineEditController final
{
public:
	struct Style { uint32 lineColor; uint32 pointColor; uint32 selectedColor; bool drawWidthRibbon = false; uint32 minPoints = 2; };
	using HeightFunction = std::function<float(float x, float z)>;
	SplineEditController(Camera& camera, HeightFunction height);
	/// Draws all splines; the selected one with handles. Remembers the viewport for hit tests.
	void Draw(ImDrawList& drawList, const ImVec2& viewportMin, const ImVec2& viewportSize,
		const std::vector<const PathSpline*>& splines, const std::vector<Style>& styles);
	/// Handles a click: select point, Alt+click segment inserts. Returns true if consumed.
	bool OnMouseDown(float x, float y, std::vector<PathSpline*>& splines, bool alt);
	/// Drags the selected point (snapped to the height function). Returns true if a point moved.
	bool OnMouseMoved(float x, float y, std::vector<PathSpline*>& splines);
	void OnMouseUp();
	/// Deletes the selected point unless the spline would drop below its style's minPoints.
	bool DeleteSelectedPoint(std::vector<PathSpline*>& splines, uint32 minPoints);
	int32 GetSelectedSpline() const; int32 GetSelectedPoint() const;
	void Select(int32 spline, int32 point);
};
```

- [ ] **Step 1: Write failing test** `water_flow_spline_tests.cpp` `[water_flow]`: a 3-point `FlowPath` with widths 4, 8, 12 → `ToSpline` → `FromSpline` equals the original exactly; `ComputePageFlow` over a 1-page settings fixture gives byte-identical UVs/speeds for the original path and the round-tripped path.

- [ ] **Step 2: Run, expect FAIL** — `bin/Debug/terrain_io_tests "[water_flow]"`.

- [ ] **Step 3: Implement `ToSpline`/`FromSpline`**; add `math` to `terrain_io`'s link libs if not already transitively present.

- [ ] **Step 4: Run, expect PASS.**

- [ ] **Step 5: Extract `SplineEditController`** from `WaterEditMode`: move the logic of `ProjectToScreen`, `HitTestFlowPoint`, `HitTestFlowSegment`, point dragging, Alt+click insert (`water_edit_mode.cpp` ~800-860) and the line/handle part of `DrawFlowOverlay` (~1010-1170, ribbon = the `halfWidth` side lines) into the controller. `WaterEditMode::River::path` becomes a `PathSpline` (Linear, open); `CollectFlowPaths()` returns `FromSpline(...)` of each; `.hflow` load/save keep their JSON layout exactly (convert at the boundary). Flow arrows stay in `WaterEditMode`. Height function = existing `GetFlowPointHeight`.

- [ ] **Step 6: Build and verify by hand**

Run: `cmake --build build --config Debug -t mmo_edit`. Open `Development`, Water mode, Flow brush: select/drag/insert/delete a river point, mouse-wheel width, save. Expected: identical behaviour to before; `git -C data/client diff` shows only the edits you made (load+save without edits leaves the `.hflow` byte-identical).

- [ ] **Step 7: Commit** `refactor(editor): shared SplineEditController, river flow paths use PathSpline`

---

### Task 3: `pcg` library scaffold — deterministic hash, random and noise

**Files:**
- Create: `src/shared/pcg/CMakeLists.txt`, `src/shared/pcg/pcg_random.h/.cpp`, `src/shared/pcg/grid_layout.h`
- Modify: `src/shared/CMakeLists.txt` (add `add_subdirectory(pcg)` next to `terrain_io`, unguarded)
- Create: `src/tests/pcg_tests/CMakeLists.txt` (`mmo_add_test(pcg_tests base math pcg)`), `src/tests/pcg_tests/pcg_random_tests.cpp`
- Modify: `src/tests/CMakeLists.txt` (one `add_subdirectory(pcg_tests)`)

**Interfaces:**
- Produces:

```cpp
namespace mmo::pcg
{
	/// SplitMix64 finaliser over the combined inputs. Pure, platform independent.
	uint64 Hash(uint64 seed, int64 a, int64 b = 0, uint64 salt = 0);
	/// [0,1) from the top 24 bits of a hash.
	float HashToUnit(uint64 hash);
	/// [lo,hi) helper: lo + HashToUnit(h) * (hi - lo).
	float HashRange(uint64 hash, float lo, float hi);
	/// Seeded 2D value noise (smoothstep-interpolated lattice of Hash values), output [-1,1].
	float ValueNoise(uint64 seed, float x, float z, float frequency);
	float FractalNoise(uint64 seed, float x, float z, float frequency, uint32 octaves, float persistence);

	/// World layout of the terrain grids, supplied by the editor (tests use small layouts).
	struct GridLayout
	{
		double originX, originZ;       // world position of global vertex (0,0)
		double vertexSpacing;          // outer vertex spacing; inner vertices sit at +0.5
		double pixelSpacing;           // splat pixel spacing (pixel 0 at origin)
		uint32 quadsPerTile;           // water quads per tile side (8)
		uint32 verticesPerTile;        // outer vertex steps per tile (8)
	};
}
```

`pcg/CMakeLists.txt`: `add_lib(pcg)`, `target_link_libraries(pcg base log math)`, `FOLDER "shared"`.

- [ ] **Step 1: Write failing tests** `[pcg_random]`: `Hash(1,2,3,4)` is equal across two calls and differs from `Hash(2,2,3,4)` and `Hash(1,2,3,5)`; pin `Hash(0,0,0,0)` to the value your implementation produces once (regression lock); `HashToUnit` in [0,1) over 100k hashes, mean within 0.5±0.01; `ValueNoise` same input → same output, in [-1,1], differs between seeds 1 and 2 at (10.3, 7.7); continuity: |noise(x) − noise(x+0.001)| < 0.01.

- [ ] **Step 2: Run, expect FAIL** — `cmake --build build --config Debug -t pcg_tests && bin/Debug/pcg_tests "[pcg_random]"`.

- [ ] **Step 3: Implement.**

- [ ] **Step 4: Run, expect PASS.** Also confirm `ctest -C Debug -R pcg_tests` picks the suite up.

- [ ] **Step 5: Commit** `feat(pcg): library scaffold with deterministic hash and noise`

---

### Task 4: Terrain contribution — non-destructive apply / unapply

**Files:**
- Create: `src/shared/pcg/terrain_access.h`, `src/shared/pcg/terrain_contribution.h/.cpp`
- Test: `src/tests/pcg_tests/terrain_contribution_tests.cpp` (with an in-memory `FakeTerrain` in the test file)

**Interfaces:**
- Consumes: `GridLayout` (Task 3).
- Produces:

```cpp
namespace mmo::pcg
{
	struct IntRect { int32 minX, minZ, maxX, maxZ; }; // inclusive
	struct WorldRect { float minX, minZ, maxX, maxZ; };
	bool RectsOverlap(const WorldRect& a, const WorldRect& b);

	/// Global-index access to terrain data. Implemented over terrain::Terrain by the editor.
	class ITerrainAccess
	{
	public:
		virtual ~ITerrainAccess() = default;
		virtual const GridLayout& GetLayout() const = 0;
		virtual float GetOuterHeight(int32 vx, int32 vz) const = 0;
		virtual void SetOuterHeight(int32 vx, int32 vz, float h) = 0;
		virtual float GetInnerHeight(int32 ix, int32 iz) const = 0;
		virtual void SetInnerHeight(int32 ix, int32 iz, float h) = 0;
		virtual uint32 GetSplat(int32 px, int32 pz) const = 0;
		virtual void SetSplat(int32 px, int32 pz, uint32 packed) = 0;
		/// Quad indices are global (tile * quadsPerTile + local). Type 0 = none.
		virtual uint8 GetWaterQuad(int32 qx, int32 qz) const = 0;
		virtual void SetWaterQuad(int32 qx, int32 qz, uint8 waterType) = 0;
		virtual float GetWaterHeight(int32 vx, int32 vz) const = 0;
		virtual void SetWaterHeight(int32 vx, int32 vz, float h) = 0;
		virtual String GetTileMaterial(int32 tx, int32 tz) const = 0;
		virtual void SetTileMaterial(int32 tx, int32 tz, const String& material) = 0;
		/// Called once after a batch of writes with the touched outer vertex rect (normals, meshes).
		virtual void FinishEdits(const IntRect& outerVertexRect) = 0;
	};

	/// What a generator wants at a world position. weight 0 = leave untouched.
	struct TerrainTarget
	{
		float height = 0.0f; float heightWeight = 0.0f;
		std::array<uint8, 4> splat{}; float splatWeight = 0.0f;
		uint8 waterType = 0; float waterHeight = 0.0f; // waterType != 0 = quad gets water
		bool setMaterial = false;
	};
	using TerrainTargetFunction = std::function<TerrainTarget(float x, float z, float baseHeight)>;

	/// Everything a generator wrote, with the value that was there before.
	struct TerrainContribution
	{
		template<typename T> struct Entry { int32 x, z; T base, written; };
		std::vector<Entry<float>> outer, inner, waterHeights;
		std::vector<Entry<uint32>> splat;
		std::vector<Entry<uint8>> waterQuads;
		std::vector<Entry<String>> materials;
		IntRect touchedOuter{};  // for FinishEdits
	};

	/// Restores the base where the current value still equals what was written. Heights the user
	/// changed since keep the user's change: current - (written - base). Other kinds keep current.
	/// Clears the contribution.
	void Unapply(ITerrainAccess& terrain, TerrainContribution& contribution);

	/// Writes the target over `rect` and records base/written for every changed sample. Entries are
	/// stored sorted by (z, x). `material` is the tile material set where target.setMaterial.
	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const TerrainTargetFunction& target,
		const String& material, TerrainContribution& out);
}
```

Apply rules: heights `new = lerp(base, target.height, heightWeight)`; splat
`new = normalize(lerp(baseWeights, target.splat, splatWeight))` to sum 255 with deterministic
rounding (largest remainder, ties to lower layer index); water quad set when `waterType != 0` at the
quad centre, water vertex heights set for every vertex of a watered quad; material for every tile
whose centre has `setMaterial`. Only samples whose written value differs from base are recorded.

- [ ] **Step 1: Write failing tests** `[terrain_contribution]` with `FakeTerrain` (2×2 tiles layout, flat 10 m, splat all layer 0 = 255):
  - `Apply then Unapply restores exactly`: target = bowl (height 5 within r 10, weight 1), grass splat, water; after Unapply every value equals the original bit for bit and the contribution is empty.
  - `Rebuild twice is identical`: Unapply+Apply with the same target twice → identical terrain and identical contribution vectors.
  - `Hand sculpt under the result is kept`: Apply; raise one recorded outer vertex by +2; Unapply → that vertex is original+2, all others original.
  - `Hand paint is kept as new base`: Apply; overwrite one recorded splat pixel; Unapply → that pixel keeps the hand value.
  - `Splat normalisation sums to 255` for weights 0.33/0.33/0.34 blends.
  - `Outside the rect nothing changes` (compare a full snapshot).
  - `RectsOverlap` true for touching-inside rects, false for disjoint.

- [ ] **Step 2: Run, expect FAIL** — `bin/Debug/pcg_tests "[terrain_contribution]"`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(pcg): non-destructive terrain contribution`

---

### Task 5: Oasis rule set — terrain shape

**Files:**
- Create: `src/shared/pcg/oasis.h/.cpp`
- Test: `src/tests/pcg_tests/oasis_terrain_tests.cpp`

**Interfaces:**
- Consumes: `PathSpline`, `SampledPath`, `Hash`, `FractalNoise`, `TerrainTarget`, `WorldRect`.
- Produces:

```cpp
namespace mmo::pcg
{
	struct OasisParams
	{
		float basinDepth = 6.0f;        // floor below rim height
		float rimWidth = 14.0f;         // rim -> floor blend distance inside the loop
		float outerFalloff = 10.0f;     // blend back to base outside the loop
		uint32 terraceSteps = 2;        // 0 = plain smoothstep
		float terraceSharpness = 0.6f;  // 0 soft .. 1 hard steps
		float floorNoiseAmplitude = 0.4f;
		float rimPercentile = 0.2f;     // percentile of loop base heights used as rim height
		float waterInset = 10.0f;       // pool edge distance inside the loop
		float waterDepth = 1.2f;        // water surface above floor
		float grassBand = 6.0f;         // grass (3) -> desert grass (2) band inside rim
		float dirtBand = 4.0f;          // desert grass -> dirt (1) band, then sand (0)
		float bandNoiseAmplitude = 2.0f;
		// placement params: Task 6
	};

	struct OasisShape; // opaque: sampled loop, rim height, floor height, water level, influence rect

	enum class OasisError : uint8 { None, TooFewPoints, ZeroArea, SelfIntersecting };

	/// Validates the loop and samples base heights along it (1 m steps).
	OasisError PrepareOasis(const PathSpline& loop, uint64 seed, const OasisParams& params,
		const std::function<float(float, float)>& baseHeight, OasisShape& out);
	WorldRect GetInfluenceRect(const OasisShape& shape);    // loop bounds + outerFalloff + 2 m
	float GetWaterLevel(const OasisShape& shape);
	TerrainTarget EvaluateTerrain(const OasisShape& shape, float x, float z, float baseHeight);
}
```

Shape: `d = SignedDistance`. Inside (`d<0`): `t = saturate(-d / rimWidth)`, profile `p(t)` =
smoothstep blended with an n-step terrace (`terraceSteps`, `terraceSharpness`), height =
`lerp(rimHeight, floorHeight, p) + floorNoise * t`, heightWeight 1. Outside: weight =
`1 - saturate(d / outerFalloff)` smoothstepped, height = rimHeight. Water where `d < -waterInset`,
level = floor + waterDepth, type Water. Splat bands by `-d` perturbed by
`FractalNoise(seed, x, z, 1/12, 3, 0.5) * bandNoiseAmplitude`; splatWeight 1 inside the loop, fading
to 0 across `outerFalloff`. `setMaterial` true wherever weight > 0.

- [ ] **Step 1: Write failing tests** `[oasis_terrain]` on a 16-point r=40 Catmull-Rom circle over flat base 20 m:
  - Centre: height = 20 − 6 (± floorNoiseAmplitude), water type set, level = height_floor + 1.2 (±0.5 for noise).
  - Point at r=60 (outside falloff): heightWeight 0, splatWeight 0.
  - Point at r=39: splat dominated by layer 2 or 3, no water.
  - Rim height uses the percentile: base = 20 + 30·[x>30] → rim height 20 (dune on one side doesn't lift basin).
  - Same inputs twice → identical `TerrainTarget` at 100 hashed positions; seed change alters splat at some of them.
  - `PrepareOasis` returns `TooFewPoints` (2 points), `ZeroArea` (collinear 3 points), `SelfIntersecting` (bow tie).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(pcg): oasis terrain shaping rules`

---

### Task 6: Oasis rule set — placements

**Files:**
- Modify: `src/shared/pcg/oasis.h/.cpp`
- Create: `src/shared/pcg/placement.h`
- Test: `src/tests/pcg_tests/oasis_placement_tests.cpp`

**Interfaces:**
- Consumes: `OasisShape`, `Hash*`.
- Produces:

```cpp
namespace mmo::pcg
{
	enum class PlacementKind : uint8 { Entity, Foliage };
	struct Placement
	{
		uint64 key;          // stable: Hash(seed, rule, cellX/step, cellZ)
		PlacementKind kind;
		String asset;
		Vector3 position; Quaternion rotation; Vector3 scale;
		bool collides = true;
	};

	struct OasisAssets
	{
		std::vector<String> cliffs, shelves, clumps;   // rim, Entity
		std::vector<String> palms;                      // Foliage, collides
		std::vector<String> bushes, waterPlants;        // Foliage, no collision
		String terrainMaterial, waterMaterial;          // used by the editor adapter (Task 8)
	};

	// added to OasisParams:
	//   rockSpacing = 7.0, rockOffset = 2.0, rockOffsetJitter = 1.5, rockYawJitterDeg = 20,
	//   rockScaleMin = 0.8, rockScaleMax = 1.2, rockSink = 0.5,
	//   familyWeights cliffs 0.5 / shelves 0.25 / clumps 0.25,
	//   palmCell = 6.0, palmDensity = 0.45, palmNearWaterBoost = 1.0, palmScaleMin = 0.8, palmScaleMax = 1.3,
	//   bushCell = 3.0, bushDensity = 0.35, waterPlantBand = 3.0, maxSlopeDeg = 30

	/// `ground` is the final ground (base + this oasis' terrain), `slope` returns degrees.
	void BuildPlacements(const OasisShape& shape, const OasisAssets& assets,
		const std::function<float(float, float)>& ground, const std::function<float(float, float)>& slope,
		std::vector<Placement>& out); // sorted by key
}
```

Rim: walk the loop by arc length in `rockSpacing` steps from distance 0; step `i` uses
`Hash(seed, kRuleRim, i)` for family, asset, offset jitter, yaw and scale; position = loop point +
outward normal × (rockOffset ± jitter), yaw faces the basin, y = ground − rockSink.
Vegetation: world-anchored cells of `palmCell` / `bushCell` over the influence rect; cell (cx,cz)
uses `Hash(seed, kRulePalm|kRuleBush, cx, cz)` for acceptance, jitter inside the cell, asset, yaw,
scale. Accept only inside the loop, between the pool edge and the rim (`-rimWidth*0.5 > d > -waterInset+1`
for palms, bush band same; `waterPlants` only within `waterPlantBand` of the pool edge), not under
water, slope ≤ maxSlopeDeg; palm density rises linearly by `palmNearWaterBoost` toward the pool.

- [ ] **Step 1: Write failing tests** `[oasis_placement]` (circle r=40, flat ground, asset lists of 2 names each):
  - Determinism: two builds → identical vectors (key, asset, transform).
  - Seed change → different key set or transforms.
  - Rim count = `floor(length / rockSpacing)` ± 1; every rock outside the loop by 0.5..3.5 m.
  - No palm/bush under water or outside the loop; all palms `collides`, bushes not.
  - Locality: move control point 4 by 5 m outward; every vegetation placement farther than `rimWidth + outerFalloff + palmCell` from the old and new position of point 4 is unchanged (same key, asset, transform); rim rocks with arc distance before the start of segment 3 unchanged.
  - Output sorted by key, keys unique.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** `feat(pcg): oasis rim rocks and vegetation placement`

---

### Task 7: Oasis record, recipe and placement reconciliation

**Files:**
- Create: `src/shared/pcg/oasis_record.h/.cpp`, `src/shared/pcg/oasis_recipe.h/.cpp`, `src/shared/pcg/placement_reconcile.h/.cpp`
- Test: `src/tests/pcg_tests/oasis_record_tests.cpp`, `src/tests/pcg_tests/placement_reconcile_tests.cpp`
- Modify: `src/shared/pcg/CMakeLists.txt` (link `binary_io_hdrs`, json include as other libs do)

**Interfaces:**
- Produces:

```cpp
namespace mmo::pcg
{
	struct OwnedPlacement { uint64 key; uint64 uniqueId; PlacementKind kind; Placement written; };

	struct OasisRecord
	{
		uint64 id = 0; String name; uint64 seed = 1;
		OasisParams params; PathSpline loop;   // closed, CatmullRom
		WorldRect influence{};                  // of the last build
		TerrainContribution terrain;
		std::vector<OwnedPlacement> placements; // sorted by key
	};

	/// Chunked binary (magic 'PCGO', version 1); writes are deterministic (same record -> same bytes).
	bool WriteOasisRecord(io::Writer& writer, const OasisRecord& record);
	bool ReadOasisRecord(io::Reader& reader, OasisRecord& out);

	/// Recipe: default params + asset lists, JSON at data/client/Editor/Pcg/OasisRecipe.json.
	bool ParseOasisRecipe(const String& jsonText, OasisParams& params, OasisAssets& assets);

	struct ReconcileResult
	{
		std::vector<OwnedPlacement> keep;     // unchanged, nothing to do
		std::vector<OwnedPlacement> update;   // same key and asset, new transform (uniqueId kept)
		std::vector<uint64> remove;           // uniqueIds to delete
		std::vector<Placement> create;        // need new uniqueIds
	};
	/// `exists(uniqueId, kind)` tells whether the owned object still exists in the world.
	ReconcileResult Reconcile(const std::vector<OwnedPlacement>& previous, const std::vector<Placement>& next,
		const std::function<bool(uint64, PlacementKind)>& exists);
}
```

Reconcile rules: key present in both, object exists, same asset → keep (transform equal) or update;
asset changed → remove + create; key only in previous → remove (only if it exists); key only in next,
or the owned object no longer exists → create.

- [ ] **Step 1: Write failing tests:**
  - `[oasis_record]` round trip of a populated record (all contribution kinds, 3 placements) → equal; writing the same record twice → identical bytes.
  - `[oasis_recipe]` parse a minimal JSON with one asset per list and `basinDepth: 8` → params.basinDepth 8, others default; malformed JSON → false.
  - `[reconcile]` keep/update/remove/create cases each; **owned rock deleted by hand** (`exists` false) → appears in `create`, not in `remove`; empty `next` → everything existing removed.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS** (`bin/Debug/pcg_tests`).
- [ ] **Step 5: Create `data/client/Editor/Pcg/OasisRecipe.json`** with the spec defaults and Korash asset lists: cliffs `Models/Desert/.../SM_RockDesert_CliffA01..08, CliffB01..08, CliffHalf01..08`, shelves `Shelf01..10`, clumps `Clump01..10, Rock01..16`, palms `DesertCoconutPalm01..06, DesertPalmTree01..05, DesertPalmSapling01..03`, bushes `DesertBush, DesertBushB, ElephantEars`, waterPlants `Marshtail` (look up exact `.hmsh` paths under `data/client/Models/Desert`). Do not commit it in the submodule yet (Task 11).
- [ ] **Step 6: Commit** `feat(pcg): oasis record, recipe and placement reconciliation`

---

### Task 8: Editor adapters — terrain access and placement applier

**Files:**
- Create: `src/mmo_edit/editors/world_editor/pcg/editor_terrain_access.h/.cpp`
- Create: `src/mmo_edit/editors/world_editor/pcg/oasis_builder.h/.cpp`
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/world_edit_mode.h` (`IWorldEditor` additions)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h/.cpp`, `edit_modes/foliage_edit_mode.h/.cpp`
- Modify: `src/mmo_edit/CMakeLists.txt` (link `pcg`)

**Interfaces:**
- Consumes: everything from Tasks 4-7.
- Produces:

```cpp
// IWorldEditor additions
virtual bool RemoveMapEntityById(uint64 uniqueId) = 0;
virtual bool HasMapEntity(uint64 uniqueId) const = 0;
virtual void SetMapEntityInfo(uint64 uniqueId, const String& name, const String& category) = 0;
virtual uint64 GenerateEntityUniqueId() = 0;
virtual InstancedFoliage& GetInstancedFoliage() = 0;
virtual void MarkFoliagePageDirty(uint16 pageIndex) = 0;   // forwards to FoliageEditMode
virtual uint64 GenerateFoliageUniqueId() = 0;               // same source FoliageEditMode uses
/// Loads (prepare + EnsurePageIsLoaded) every page intersecting the rect, synchronously.
virtual void EnsurePagesLoaded(float minX, float minZ, float maxX, float maxZ) = 0;

// mmo_edit
class EditorTerrainAccess final : public pcg::ITerrainAccess { explicit EditorTerrainAccess(terrain::Terrain&); ... };

struct OasisBuildReport { bool ok; String error; double milliseconds; uint32 terrainSamples; uint32 kept, updated, removed, created; };
/// Unapply old contribution, prepare + apply terrain, build + reconcile placements. Updates the record.
OasisBuildReport RebuildOasis(IWorldEditor& editor, pcg::OasisRecord& record, const pcg::OasisAssets& assets);
/// Unapply terrain and remove all owned placements; leaves the record empty.
OasisBuildReport DeleteOasisContent(IWorldEditor& editor, pcg::OasisRecord& record);
```

`EditorTerrainAccess` maps global indices onto `terrain::Terrain`: outer heights via
`GetHeightAtGridPoint`/`SetHeightAt`, inner via `GetInnerHeightAt`/`SetInnerHeightAt`, splat via
`GetLayersAt` + a new `Terrain::SetLayersAt(uint32 x, uint32 z, uint32 packed)` (add it next to
`SetLayerAt`, mirroring seams the same way), water via the owning `Page`'s
`SetWaterQuadBit`/`SetWaterType`/`SetWaterVertexHeight` (+ `RebuildWaterMesh` in `FinishEdits`), tile
material via `Page::NotifyTileMaterialChanged` path used by the paint brush. `FinishEdits` runs the
same post-edit sequence as `Terrain::Flatten` (`UpdateInnerVertices`, normals, `UpdateTiles`,
`UpdateTileCoverage`) for the rect + 1 vertex border, and marks pages changed. The `GridLayout` comes
from `terrain::constants` and the editor's page offset (+32 pages).

`RebuildOasis` order:
1. Validate the loop first (`PrepareOasis` with any height function — validation does not depend on
   heights). On error return it with the world untouched.
2. `EnsurePagesLoaded(old influence ∪ new loop bounds + outerFalloff + 2)`.
3. `Unapply` the old contribution.
4. `PrepareOasis` with base = current heights, then `Apply` over `GetInfluenceRect`.
5. `BuildPlacements` (ground = `GetSmoothHeightAt`, slope from the terrain normal) → `Reconcile` →
   remove / update / create. Entities via `CreateMapEntity`, then `SetMapEntityInfo(id, "Oasis <id hex> <key hex>", "Pcg")`;
   foliage via `AddInstance` + `MarkFoliagePageDirty` (page index from position, as `FoliageEditMode` computes it).

`OasisAssets` additionally carries `terrainMaterial` (default `Desert_Terrain.hmat` path) and
`waterMaterial`; the adapter sets a page's water material only if that page has none yet (not
recorded, harmless on Delete). Far-terrain `.tlod`: if the editor's save does not already re-bake
LODs of changed pages, log the touched page list in the report so `--bake-terrain-lod` can be run.

- [ ] **Step 1: Implement adapters and IWorldEditor additions.** Validation-before-unapply is required (Review Focus 2).
- [ ] **Step 2: Build** — `cmake --build build --config Debug -t mmo_edit pcg_tests`; `bin/Debug/pcg_tests` still green.
- [ ] **Step 3: Commit** `feat(editor): terrain and placement adapters for PCG`

(Behaviour is verified through the UI in Task 9 and the flag in Task 10.)

---

### Task 9: `ProceduralEditMode` — UI, load and save

**Files:**
- Create: `src/mmo_edit/editors/world_editor/edit_modes/procedural_edit_mode.h/.cpp`
- Modify: `world_editor_instance.h/.cpp` (member, construction, `availableModes[]`, load on open, save in `Save()`)

**Interfaces:**
- Consumes: `SplineEditController` (Task 2), `RebuildOasis`/`DeleteOasisContent` (Task 8), record IO + recipe (Task 7), `RectsOverlap` (Task 4).
- Produces:

```cpp
class ProceduralEditMode final : public WorldEditMode
{
public:
	ProceduralEditMode(IWorldEditor& editor, Camera& camera);
	const char* GetName() const override; // "Procedural"
	void DrawDetails() override; void DrawViewportOverlay(...) override;
	void OnMouseDown/Moved/Up(...) override;
	/// data/client/Editor/Pcg/Worlds/<world>/ ; loads every *.hpcg and OasisRecipe.json.
	void Load(const std::filesystem::path& pcgFolder);
	/// Writes changed records; deletes files of deleted oases.
	bool Save(const std::filesystem::path& pcgFolder);
	bool RebuildAll(String& error);
	bool Rebuild(uint64 oasisId, String& error);
	bool HasUnsavedChanges() const;
};
```

Behaviour (spec "Editor UX"): oasis list; *New Oasis* = 8-point Catmull-Rom loop r=40 at the
viewport centre ground hit, seed 1, recipe defaults, id = `GenerateEntityUniqueId()`; name, seed
(*Randomize* may use `std::random_device` — it picks an input, it is not an output path) and
params as `DragFloat`s; *Rebuild*, *Rebuild All*, *Delete* (with
confirm popup); status line from `OasisBuildReport`; Rebuild refuses when the new influence rect
overlaps another oasis' influence (`RectsOverlap`) and shows which; influence rect drawn faintly; a
details-panel warning when the selected map entity has category `Pcg`. File name per oasis:
`<id as 16 hex digits>.hpcg`. Note: entity/foliage/page saving is unchanged — this mode only adds
its own files to `Save()`.

- [ ] **Step 1: Implement the mode and wire it** (member, constructor, `availableModes[]`, `Load` next to `LoadFlowPaths`, `Save` before pages are written).
- [ ] **Step 2: Build** `mmo_edit`.
- [ ] **Step 3: Manual check in the editor** on a scratch area of `Development` (do not save unless intended):
  - New Oasis → Rebuild → basin, paint, water, rocks, palms appear; status shows counts.
  - Rebuild again → status shows 0 created/updated/removed, all kept.
  - Drag one point → Rebuild → only nearby content changes.
  - Sculpt a hill into the basin with the terrain brush → Rebuild → basin follows, hill shape retained relative to the basin.
  - Delete a generated palm by hand → Rebuild → it is recreated.
  - New second oasis overlapping the first → Rebuild refused with a message, nothing changes (Review Focus 4).
  - Delete oasis → terrain, water, paint and every owned object gone.
  - Save, restart the editor, fly far away so the oasis pages unload, Delete/Rebuild from the list → works, pages loaded first (Review Focus 5).
- [ ] **Step 4: Commit** `feat(editor): Procedural edit mode with oasis control objects`

---

### Task 10: Unattended `mmo_edit --pcg-rebuild`

**Files:**
- Modify: `src/mmo_edit/mmo_edit.cpp` (next to `--capture-view`), `world_editor_instance.h/.cpp` (`bool RebuildProcedural(uint64 idOrZero, String& error)` forwarding to the mode)

**Interfaces:**
- Produces: `mmo_edit --pcg-rebuild <world.hwld> [--oasis <id hex>]` — opens the world like `--capture-view` (`mainWindow.OpenAsset`, `WorldEditorInstance::GetLastCreated()`), rebuilds one or all oases, calls `Save()`, logs the report to `pcg_rebuild.log`, exits 0 on success, 1 on failure.

- [ ] **Step 1: Implement** (copy the logging/assert redirection pattern of `--capture-view`).
- [ ] **Step 2: Verify determinism**

Run (after Task 11 created a saved oasis):
```
bin/Debug/mmo_edit.exe --pcg-rebuild Worlds/Development/Development.hwld
git -C data/client status --porcelain
```
Expected: exit 0; the second consecutive run leaves `git status` empty (no page, `.wobj`, `.hfol` or `.hpcg` changed).
- [ ] **Step 3: Commit** `feat(editor): --pcg-rebuild unattended flag`

---

### Task 11: Test oasis in the Sands of Korash and the quality comparison

**Files:**
- Data only, in the `data/client` submodule on branch `feature/pcg-oasis-poc`: `Editor/Pcg/OasisRecipe.json`, `Editor/Pcg/Worlds/Development/*.hpcg`, touched `Worlds/Development/Development/{Terrain,Entities,Foliage}` files.

- [ ] **Step 1: Verify the splat layer order and water material.** Read `Desert_Terrain.hmat` layer mapping and the water material/type used by the Korash pools in pages 37-39/49-51 (via `Page::GetWaterMaterialName` on a loaded page or the `.tile` MCWQ chunk). Put the water material into the recipe if it isn't the default; fix the layer constants if the order differs.
- [ ] **Step 2: Pick the site.** An open dune/flat stretch in area 22, outside pages x 37-39 / z 49-51, not touching existing placements (`python -m worldkit query --map 0 --at X Z` for a quick read is fine as scratch help).
- [ ] **Step 3: Build and tune** the oasis in the editor until it reads like the demo basin; tune defaults in `OasisRecipe.json` rather than per-oasis where the change is general.
- [ ] **Step 4: Page border check** (Review Focus 1): build a second, temporary oasis straddling a page corner, inspect seams (no cracks, no splat discontinuity), then Delete it; confirm with `git -C data/client status` that its pages returned to their committed bytes, or differ only where the first oasis is.
- [ ] **Step 5: Determinism run** — Task 10 step 2.
- [ ] **Step 6: Captures** — `mmo_edit --capture-view` of the PoC oasis and of the demo basin from comparable viewpoints (same height/pitch/time), PNGs into `docs/superpowers/specs/pcg-oasis-poc/` (create).
- [ ] **Step 7: Commit data on a submodule branch, never master:**
```
git -C data/client switch -c feature/pcg-oasis-poc
git -C data/client add Editor/Pcg Worlds/Development
git -C data/client commit -m "PCG oasis PoC test site"
git add data/client docs/superpowers/specs/pcg-oasis-poc
git commit -m "chore(data): PCG oasis PoC test site and captures"
```
Do not push either repository.
- [ ] **Step 8: Run the suites once more** — `cmake --build build --config Debug -t all_tests && cd build && ctest -C Debug --output-on-failure` → all green.
