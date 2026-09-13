# Ocean Water Phase 1 — Surface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A stylized-realistic ocean surface you can stand on a beach and look at — correct normal mapping, screen-space reflections with sky fallback, chromatic depth tint, animated shore foam, whitecaps — driven by a `WaterType`-to-profile table.

**Architecture:** Fix the `ManualRenderObject` tangent-basis bug that has always broken normal mapping on water. Add a `ScreenSpaceReflection` material-graph node that ray-marches the scene depth already bound at t31 inside the water pixel shader. Add a `water_profiles` client proto binding each `terrain::WaterType` to a surface material. Extend the existing `Water_Base.hmat` graph into `Water_Ocean.hmat`.

**Tech Stack:** C++17, D3D11 (HLSL) + Metal (MSL), Catch2 via `mmo_add_test`, protobuf 2 for client data, CMake.

## Global Constraints

- Copyright header on every new source file: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- Allman braces, every `{` and `}` on its own line. Braces always, even single-line bodies.
- Tabs for indentation.
- Member variables `m_camelCase`. Methods `PascalCase`. Locals and anonymous-namespace free functions `camelCase`. Files `snake_case`.
- `#pragma once` in every header. Doxygen `///` on all public members.
- Root namespace `mmo`.
- No exceptions (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT` / `VERIFY` / `UNREACHABLE` and `DLOG` / `WLOG` / `ELOG` from `base/macros.h`.
- Type aliases from `base/typedefs.h`: `uint8`/`uint32`/`uint64`/`int32`, `ObjectGuid`, `GameTime`.
- **No wire-format changes in this plan.** Nothing here touches opcodes, packet payloads, framing or the cipher, so `mmo::auth::ProtocolVersion` and `mmo::game::ProtocolVersion` must NOT be bumped. If a task finds itself editing a packet handler payload, stop and escalate.
- Everything in `terrain_tests`, `game_client_tests` and `deferred_shading_tests` must keep building on the headless Linux server build. Those suites link only `base` and `math` (plus a few explicitly-listed sources) — do **not** add a dependency on the `terrain`, `scene_graph` or `graphics` libraries to them.
- `data/client` and `data/editor` are git submodules. Asset changes are committed inside the submodule first, then the submodule pointer is committed in the superproject.
- Build: `cmake --build build --config Debug -t <target>`. Tests: `cd build && ctest -C Debug --output-on-failure`.

## File Structure

**Created:**
- `src/shared/terrain/water_lookup.h` — header-only page-local water queries (type, presence, height). No dependency beyond `base/typedefs.h` and `terrain/constants.h`, so `terrain_tests` can include it.
- `src/shared/terrain/water_mesh_build.h` — header-only bucketing of water quads by `WaterType`. Same dependency rule.
- `src/tests/terrain_tests/test_water_lookup.cpp`
- `src/tests/terrain_tests/test_water_mesh_build.cpp`
- `src/shared/client_data/water_profiles.proto`
- `src/tests/client_data_tests/test_water_profiles.cpp`
- `src/tests/scene_graph_tests/test_manual_triangle_basis.cpp`

**Modified:**
- `src/shared/scene_graph/manual_render_object.h` — per-vertex tangent basis on `ManualTriangleListOperation::Triangle`.
- `src/shared/terrain/terrain.h` / `.cpp` — `GetWaterTypeAtWorldPos`, `HasWaterAtWorldPos`.
- `src/shared/terrain/page.cpp` — per-`WaterType` batching, tangent basis, bottom-face tagging.
- `src/shared/terrain/page.h` — profile-resolution hook.
- `src/shared/graphics/material_compiler.h` — `AddScreenSpaceReflection`.
- `src/shared/graphics_d3d11/material_compiler_d3d11.h` / `.cpp` — HLSL implementation.
- `src/shared/graphics_metal/material_compiler_metal.h` / `.mm` — MSL implementation.
- `src/mmo_edit/editors/material_editor/material_node.h` / `.cpp` — `ScreenSpaceReflectionNode`.
- `src/mmo_edit/editors/material_editor/node_editor/node_registry.cpp` — register the node.
- `src/shared/graphics/sky_component.cpp` — publish `SunDirection` / `SunColor` globals.
- `src/shared/client_data/project.h` — register the water profile manager.
- `src/mmo_edit/editors/world_editor/edit_modes/water_edit_mode.h` / `.cpp` — profile-aware material field.
- `src/tests/terrain_tests/CMakeLists.txt`, `src/tests/client_data_tests/CMakeLists.txt` — nothing to change if the macro globs recursively; verify.

**Data (submodule `data/client`):**
- `data/client/Worlds/Water_Ocean.hmat` — copy of `Water_Base.hmat`, extended.
- `data/client/Textures/WaterNoise_01.htex` — tiling noise for whitecaps.
- `data/client/Config/GlobalShaderParameters.hgsp` — gains `SunDirection`, `SunColor`.

---

### Task 1: Per-vertex tangent basis on manual triangles

This is the blocker. `ManualTriangleListOperation::Finish()` writes `Vector3::UnitY` into the normal, binormal *and* tangent of every vertex, so `GetWorldNormal(n, N, T, B)` in every generated pixel shader builds a degenerate basis and any normal map sampled on a manual render object resolves to nonsense. Water is the only shipping consumer that samples a normal map, which is why it has never looked right.

**Files:**
- Modify: `src/shared/scene_graph/manual_render_object.h` (the `Triangle` class and `ManualTriangleListOperation::Finish`)
- Test: `src/tests/scene_graph_tests/test_manual_triangle_basis.cpp` (create)

**Interfaces:**
- Consumes: nothing.
- Produces: `ManualTriangleListOperation::Triangle::SetNormal(uint8 index, const Vector3&)`, `SetTangent(uint8 index, const Vector3&)`, `SetBinormal(uint8 index, const Vector3&)`, and matching `GetNormal(uint8) const`, `GetTangent(uint8) const`, `GetBinormal(uint8) const`. Defaults stay `Vector3::UnitY` for all three so existing callers are byte-identical. Task 5 uses these.

- [ ] **Step 1: Write the failing test**

Create `src/tests/scene_graph_tests/test_manual_triangle_basis.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "scene_graph/manual_render_object.h"

using namespace mmo;

TEST_CASE("ManualTriangle_Basis_Defaults_To_UnitY", "[manual_render_object]")
{
	// Existing callers (debug geometry, the axis display, collision overlays) never set a
	// basis. They must keep the exact vertex data they had before this change.
	ManualTriangleListOperation::Triangle t(
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(1.0f, 0.0f, 0.0f),
		Vector3(0.0f, 0.0f, 1.0f));

	for (uint8 i = 0; i < 3; ++i)
	{
		CHECK(t.GetNormal(i) == Vector3::UnitY);
		CHECK(t.GetTangent(i) == Vector3::UnitY);
		CHECK(t.GetBinormal(i) == Vector3::UnitY);
	}
}

TEST_CASE("ManualTriangle_Basis_Is_Per_Vertex", "[manual_render_object]")
{
	ManualTriangleListOperation::Triangle t(
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(1.0f, 0.0f, 0.0f),
		Vector3(0.0f, 0.0f, 1.0f));

	t.SetNormal(0, Vector3::UnitY);
	t.SetNormal(1, -Vector3::UnitY);

	CHECK(t.GetNormal(0) == Vector3::UnitY);
	CHECK(t.GetNormal(1) == -Vector3::UnitY);
	// Untouched vertices keep the default.
	CHECK(t.GetNormal(2) == Vector3::UnitY);
}

TEST_CASE("ManualTriangle_Horizontal_Plane_Basis_Is_Orthonormal", "[manual_render_object]")
{
	// The water surface case: a horizontal quad wants N=+Y, T=+X, B=+Z. A degenerate basis
	// (all three equal, as before this change) makes GetWorldNormal collapse and normal
	// mapping produce garbage, which is exactly the bug this fixes.
	ManualTriangleListOperation::Triangle t(
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(1.0f, 0.0f, 0.0f),
		Vector3(0.0f, 0.0f, 1.0f));

	for (uint8 i = 0; i < 3; ++i)
	{
		t.SetNormal(i, Vector3::UnitY);
		t.SetTangent(i, Vector3::UnitX);
		t.SetBinormal(i, Vector3::UnitZ);
	}

	for (uint8 i = 0; i < 3; ++i)
	{
		CHECK(t.GetNormal(i).Dot(t.GetTangent(i)) == Approx(0.0f));
		CHECK(t.GetNormal(i).Dot(t.GetBinormal(i)) == Approx(0.0f));
		CHECK(t.GetTangent(i).Dot(t.GetBinormal(i)) == Approx(0.0f));
	}
}
```

- [ ] **Step 2: Run the test and confirm it fails**

```bash
cmake --build build --config Debug -t scene_graph_tests
```

Expected: compile error — `'SetNormal': is not a member of 'mmo::ManualTriangleListOperation::Triangle'`.

If `scene_graph_tests` does not pick the new file up automatically, check `src/tests/scene_graph_tests/CMakeLists.txt` — `mmo_add_test` globs the directory recursively, so no edit should be needed. Re-run CMake configure if the glob is stale.

- [ ] **Step 3: Add the basis to the Triangle class**

In `src/shared/scene_graph/manual_render_object.h`, inside `ManualTriangleListOperation::Triangle`, extend the constructor initialiser list and add the members:

```cpp
			Triangle(const Vector3& v1, const Vector3& v2, const Vector3& v3)
				: m_points{ v1, v2, v3 }
				, m_colors{ 0xffffffff, 0xffffffff, 0xffffffff }
				, m_uvs{ {0.0f,0.0f}, {0.0f,0.0f}, {0.0f,0.0f} }
				, m_normals{ Vector3::UnitY, Vector3::UnitY, Vector3::UnitY }
				, m_tangents{ Vector3::UnitY, Vector3::UnitY, Vector3::UnitY }
				, m_binormals{ Vector3::UnitY, Vector3::UnitY, Vector3::UnitY }
			{
			}
```

Add the accessors next to `SetUV`:

```cpp
			/// Sets the vertex normal used to build the tangent basis for normal mapping.
			/// @param index Vertex index (0-2).
			/// @param normal The vertex normal. Should be normalized.
			void SetNormal(const uint8_t index, const Vector3& normal)
			{
				assert(index < 3 && "Index out of range!");
				m_normals[index] = normal;
			}

			/// Sets the vertex tangent used to build the tangent basis for normal mapping.
			/// @param index Vertex index (0-2).
			/// @param tangent The vertex tangent. Should be normalized and perpendicular to the normal.
			void SetTangent(const uint8_t index, const Vector3& tangent)
			{
				assert(index < 3 && "Index out of range!");
				m_tangents[index] = tangent;
			}

			/// Sets the vertex binormal used to build the tangent basis for normal mapping.
			/// @param index Vertex index (0-2).
			/// @param binormal The vertex binormal. Should be normalized and perpendicular to both
			///        the normal and the tangent.
			void SetBinormal(const uint8_t index, const Vector3& binormal)
			{
				assert(index < 3 && "Index out of range!");
				m_binormals[index] = binormal;
			}

			/// Gets the vertex normal.
			[[nodiscard]] const Vector3& GetNormal(const uint8_t index) const { assert(index < 3); return m_normals[index]; }

			/// Gets the vertex tangent.
			[[nodiscard]] const Vector3& GetTangent(const uint8_t index) const { assert(index < 3); return m_tangents[index]; }

			/// Gets the vertex binormal.
			[[nodiscard]] const Vector3& GetBinormal(const uint8_t index) const { assert(index < 3); return m_binormals[index]; }
```

And the storage next to `m_uvs`:

```cpp
			Vector3 m_normals[3];
			Vector3 m_tangents[3];
			Vector3 m_binormals[3];
```

- [ ] **Step 4: Use the basis in Finish()**

Still in `manual_render_object.h`, in `ManualTriangleListOperation::Finish()`, replace the hardcoded vertex construction:

```cpp
					const POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX v1{ triangle.GetPosition(i), triangle.GetColor(i), Vector3::UnitY, Vector3::UnitY, Vector3::UnitY, uv[0], uv[1] };
```

with:

```cpp
					const POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX v1{ triangle.GetPosition(i), triangle.GetColor(i),
						triangle.GetNormal(i), triangle.GetBinormal(i), triangle.GetTangent(i), uv[0], uv[1] };
```

Note the struct's member order is **normal, binormal, tangent** — confirm against the `POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX` declaration in `src/shared/graphics/vertex_format.h` before committing. Getting binormal and tangent the wrong way round produces a basis that is orthonormal but handed wrong, which looks subtly lit-from-the-wrong-side rather than obviously broken.

- [ ] **Step 5: Run the test and confirm it passes**

```bash
cmake --build build --config Debug -t scene_graph_tests && cd build && ctest -C Debug -R scene_graph_tests --output-on-failure
```

Expected: PASS, all three cases.

- [ ] **Step 6: Confirm nothing else regressed**

```bash
cmake --build build --config Debug -t all_tests && cd build && ctest -C Debug --output-on-failure
```

Expected: the full suite passes. Every existing manual-render-object caller left the basis at the default, so vertex data is unchanged for them.

- [ ] **Step 7: Commit**

```bash
git add src/shared/scene_graph/manual_render_object.h src/tests/scene_graph_tests/test_manual_triangle_basis.cpp
git commit -m "fix(scene_graph): per-vertex tangent basis on manual triangles

ManualTriangleListOperation::Finish wrote Vector3::UnitY into the normal,
binormal and tangent of every vertex, so GetWorldNormal built a degenerate
basis and any normal map sampled on a manual render object resolved to
nonsense. Water is the only shipping consumer that samples one, which is
why the water material has never looked right.

Adds per-vertex SetNormal/SetTangent/SetBinormal. All three still default
to UnitY, so every existing caller produces byte-identical vertex data.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Header-only water lookup helpers

`Terrain::GetWaterHeightAtWorldPos` returns `0.0f` both for "the water surface is at y=0" and for "there is no water here", because `m_waterVertexHeights` is zero-initialised on every page. Any caller that treats a height of 0 as "there is water" will think every character standing below y=0 is swimming. This task adds presence-aware helpers as pure functions so `terrain_tests` can cover them without linking the `terrain` library.

**Files:**
- Create: `src/shared/terrain/water_lookup.h`
- Test: `src/tests/terrain_tests/test_water_lookup.cpp` (create)

**Interfaces:**
- Consumes: `terrain::constants` (`TilesPerPage`, `OuterVerticesPerPageSide`, `TileSize`, `PageSize`), `terrain::WaterType`.
- Produces, all in `namespace mmo::terrain::water_lookup`:
  - `struct PageWaterView { const uint64* quadMasks; const uint8* types; const float* vertexHeights; };` — non-owning view of one page's water arrays, any member may be `nullptr`.
  - `bool HasWaterAtQuad(const PageWaterView&, uint32 localTileX, uint32 localTileZ, uint32 qx, uint32 qz)`
  - `WaterType TypeAtTile(const PageWaterView&, uint32 localTileX, uint32 localTileZ)`
  - `struct QuadCoord { uint32 tileX, tileZ, qx, qz; bool valid; };`
  - `QuadCoord QuadFromPageLocal(float localX, float localZ)` — converts a position in page-local world units to the quad containing it.
  - Task 3 wraps these; Task 4 uses `TypeAtTile`.

- [ ] **Step 1: Write the failing test**

Create `src/tests/terrain_tests/test_water_lookup.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "terrain/water_lookup.h"

#include <vector>

using namespace mmo;
using namespace mmo::terrain;
using namespace mmo::terrain::water_lookup;

namespace
{
	/// Builds a page water view backed by vectors the caller keeps alive.
	struct PageFixture
	{
		std::vector<uint64> masks;
		std::vector<uint8> types;
		std::vector<float> heights;

		PageFixture()
			: masks(constants::TilesPerPage * constants::TilesPerPage, 0ULL)
			, types(constants::TilesPerPage * constants::TilesPerPage, static_cast<uint8>(WaterType::None))
			, heights(constants::OuterVerticesPerPageSide * constants::OuterVerticesPerPageSide, 0.0f)
		{
		}

		PageWaterView View() const
		{
			return PageWaterView{ masks.data(), types.data(), heights.data() };
		}

		void SetQuad(uint32 tileX, uint32 tileZ, uint32 qx, uint32 qz, WaterType type)
		{
			const size_t idx = tileX + tileZ * constants::TilesPerPage;
			masks[idx] |= (1ULL << (qx + qz * 8));
			types[idx] = static_cast<uint8>(type);
		}
	};
}

TEST_CASE("WaterLookup_Empty_Page_Has_No_Water", "[water_lookup]")
{
	const PageFixture page;
	const PageWaterView view = page.View();

	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 15, 15, 7, 7));
	CHECK(TypeAtTile(view, 0, 0) == WaterType::None);
}

TEST_CASE("WaterLookup_Null_View_Is_Water_Free", "[water_lookup]")
{
	// Pages that never had a water chunk pass null pointers. This must not crash and must
	// not report water.
	const PageWaterView view{ nullptr, nullptr, nullptr };

	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, 0));
	CHECK(TypeAtTile(view, 0, 0) == WaterType::None);
}

TEST_CASE("WaterLookup_Finds_Single_Quad", "[water_lookup]")
{
	PageFixture page;
	page.SetQuad(3, 4, 5, 6, WaterType::Ocean);
	const PageWaterView view = page.View();

	CHECK(HasWaterAtQuad(view, 3, 4, 5, 6));
	CHECK(TypeAtTile(view, 3, 4) == WaterType::Ocean);

	// Neighbouring quads in the same tile are still dry.
	CHECK_FALSE(HasWaterAtQuad(view, 3, 4, 4, 6));
	CHECK_FALSE(HasWaterAtQuad(view, 3, 4, 5, 5));
	// A different tile is dry and untyped.
	CHECK_FALSE(HasWaterAtQuad(view, 3, 5, 5, 6));
	CHECK(TypeAtTile(view, 3, 5) == WaterType::None);
}

TEST_CASE("WaterLookup_Out_Of_Range_Is_Water_Free", "[water_lookup]")
{
	PageFixture page;
	page.SetQuad(0, 0, 0, 0, WaterType::Ocean);
	const PageWaterView view = page.View();

	CHECK_FALSE(HasWaterAtQuad(view, constants::TilesPerPage, 0, 0, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, constants::TilesPerPage, 0, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 8, 0));
	CHECK_FALSE(HasWaterAtQuad(view, 0, 0, 0, 8));
	CHECK(TypeAtTile(view, constants::TilesPerPage, 0) == WaterType::None);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Origin", "[water_lookup]")
{
	const QuadCoord q = QuadFromPageLocal(0.0f, 0.0f);
	CHECK(q.valid);
	CHECK(q.tileX == 0);
	CHECK(q.tileZ == 0);
	CHECK(q.qx == 0);
	CHECK(q.qz == 0);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Second_Quad", "[water_lookup]")
{
	// One quad is TileSize / 8 world units across.
	constexpr float quadSize = static_cast<float>(constants::TileSize) / 8.0f;

	const QuadCoord q = QuadFromPageLocal(quadSize * 1.5f, quadSize * 0.5f);
	CHECK(q.valid);
	CHECK(q.tileX == 0);
	CHECK(q.tileZ == 0);
	CHECK(q.qx == 1);
	CHECK(q.qz == 0);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Crosses_Tile", "[water_lookup]")
{
	// Eight quads per tile side, so the ninth quad is tile 1 quad 0.
	constexpr float quadSize = static_cast<float>(constants::TileSize) / 8.0f;

	const QuadCoord q = QuadFromPageLocal(quadSize * 8.5f, quadSize * 16.5f);
	CHECK(q.valid);
	CHECK(q.tileX == 1);
	CHECK(q.qx == 0);
	CHECK(q.tileZ == 2);
	CHECK(q.qz == 0);
}

TEST_CASE("WaterLookup_Quad_From_Page_Local_Rejects_Outside", "[water_lookup]")
{
	CHECK_FALSE(QuadFromPageLocal(-1.0f, 0.0f).valid);
	CHECK_FALSE(QuadFromPageLocal(0.0f, -1.0f).valid);
	CHECK_FALSE(QuadFromPageLocal(static_cast<float>(constants::PageSize) + 1.0f, 0.0f).valid);
	CHECK_FALSE(QuadFromPageLocal(0.0f, static_cast<float>(constants::PageSize) + 1.0f).valid);
}
```

- [ ] **Step 2: Run the test and confirm it fails**

```bash
cmake --build build --config Debug -t terrain_tests
```

Expected: compile error — `cannot open source file "terrain/water_lookup.h"`.

- [ ] **Step 3: Write the header**

Create `src/shared/terrain/water_lookup.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"

namespace mmo
{
	namespace terrain
	{
		/// @brief Pure page-local water queries.
		///
		/// @remark Deliberately free of any dependency on the terrain library so it compiles into
		///         the headless terrain_tests target, which links only base and math.
		///
		/// @remark The central invariant: water presence is carried by the quad mask, never by the
		///         surface height. Page water heights are zero-initialised, so a height of 0 means
		///         "no water here" just as often as it means "the surface is at y=0". Callers that
		///         key off height alone conclude that everything standing below y=0 is submerged.
		namespace water_lookup
		{
			/// @brief Non-owning view of one page's water arrays. Any pointer may be null, which
			///        is how a page that never had a water chunk presents itself.
			struct PageWaterView
			{
				/// @brief TilesPerPage^2 entries. Bit (qx + qz * 8) set means that sub-quad has water.
				const uint64* quadMasks { nullptr };

				/// @brief TilesPerPage^2 entries, each a terrain::WaterType.
				const uint8* types { nullptr };

				/// @brief OuterVerticesPerPageSide^2 entries, the water surface height grid.
				const float* vertexHeights { nullptr };
			};

			/// @brief A quad address within a page, plus whether the source position was inside it.
			struct QuadCoord
			{
				uint32 tileX { 0 };
				uint32 tileZ { 0 };
				uint32 qx { 0 };
				uint32 qz { 0 };
				bool valid { false };
			};

			/// @brief Number of water sub-quads along one tile side.
			constexpr uint32 QuadsPerTileSide = 8;

			/// @brief Side length of one water sub-quad in world units.
			constexpr float QuadSize = static_cast<float>(constants::TileSize) / static_cast<float>(QuadsPerTileSide);

			/// @brief Returns true if the given sub-quad carries water.
			/// @param view The page's water arrays.
			/// @param localTileX Tile X within the page, [0, TilesPerPage).
			/// @param localTileZ Tile Z within the page, [0, TilesPerPage).
			/// @param qx Sub-quad X within the tile, [0, QuadsPerTileSide).
			/// @param qz Sub-quad Z within the tile, [0, QuadsPerTileSide).
			/// @return True when the quad has water. Out-of-range indices and null views return false.
			inline bool HasWaterAtQuad(const PageWaterView& view, const uint32 localTileX, const uint32 localTileZ,
				const uint32 qx, const uint32 qz)
			{
				if (view.quadMasks == nullptr)
				{
					return false;
				}

				if (localTileX >= constants::TilesPerPage || localTileZ >= constants::TilesPerPage)
				{
					return false;
				}

				if (qx >= QuadsPerTileSide || qz >= QuadsPerTileSide)
				{
					return false;
				}

				const uint64 mask = view.quadMasks[localTileX + localTileZ * constants::TilesPerPage];
				return (mask & (1ULL << (qx + qz * QuadsPerTileSide))) != 0ULL;
			}

			/// @brief Returns the liquid type of a tile.
			/// @param view The page's water arrays.
			/// @param localTileX Tile X within the page, [0, TilesPerPage).
			/// @param localTileZ Tile Z within the page, [0, TilesPerPage).
			/// @return The tile's water type. Out-of-range indices and null views return WaterType::None.
			inline WaterType TypeAtTile(const PageWaterView& view, const uint32 localTileX, const uint32 localTileZ)
			{
				if (view.types == nullptr)
				{
					return WaterType::None;
				}

				if (localTileX >= constants::TilesPerPage || localTileZ >= constants::TilesPerPage)
				{
					return WaterType::None;
				}

				return static_cast<WaterType>(view.types[localTileX + localTileZ * constants::TilesPerPage]);
			}

			/// @brief Converts a page-local position in world units to the sub-quad containing it.
			/// @param localX Position along X relative to the page origin, in world units.
			/// @param localZ Position along Z relative to the page origin, in world units.
			/// @return The quad address. `valid` is false when the position lies outside the page.
			inline QuadCoord QuadFromPageLocal(const float localX, const float localZ)
			{
				QuadCoord result;

				if (localX < 0.0f || localZ < 0.0f)
				{
					return result;
				}

				const float quadX = localX / QuadSize;
				const float quadZ = localZ / QuadSize;

				constexpr uint32 quadsPerPageSide = constants::TilesPerPage * QuadsPerTileSide;
				if (quadX >= static_cast<float>(quadsPerPageSide) || quadZ >= static_cast<float>(quadsPerPageSide))
				{
					return result;
				}

				const uint32 gx = static_cast<uint32>(quadX);
				const uint32 gz = static_cast<uint32>(quadZ);

				result.tileX = gx / QuadsPerTileSide;
				result.tileZ = gz / QuadsPerTileSide;
				result.qx = gx % QuadsPerTileSide;
				result.qz = gz % QuadsPerTileSide;
				result.valid = true;
				return result;
			}
		}
	}
}
```

- [ ] **Step 4: Run the test and confirm it passes**

```bash
cmake --build build --config Debug -t terrain_tests && cd build && ctest -C Debug -R terrain_tests --output-on-failure
```

Expected: PASS, eight cases tagged `[water_lookup]`.

- [ ] **Step 5: Confirm the headless build stays clean**

The header must not have pulled in anything from the `terrain` library. Verify:

```bash
grep -n "#include" src/shared/terrain/water_lookup.h
```

Expected: exactly `base/typedefs.h` and `terrain/constants.h`. Anything else breaks the headless Linux build and must be removed.

- [ ] **Step 6: Commit**

```bash
git add src/shared/terrain/water_lookup.h src/tests/terrain_tests/test_water_lookup.cpp
git commit -m "feat(terrain): header-only water presence and type lookups

Water presence lives in the per-tile quad mask, never in the surface height:
page water heights are zero-initialised, so height 0 means 'no water' as
often as it means 'surface at y=0'. Callers keying off height alone conclude
every character below y=0 is swimming.

Header-only and dependency-free so terrain_tests, which links only base and
math, can cover it on the headless build.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Terrain water type and presence queries

**Files:**
- Modify: `src/shared/terrain/terrain.h` (declarations next to `GetWaterHeightAtWorldPos`, around line 551)
- Modify: `src/shared/terrain/terrain.cpp` (implementations next to `GetWaterHeightAtWorldPos`, around line 2137)
- Modify: `src/shared/terrain/page.h` — expose a `water_lookup::PageWaterView` accessor

**Interfaces:**
- Consumes: `water_lookup::PageWaterView`, `HasWaterAtQuad`, `TypeAtTile`, `QuadFromPageLocal` from Task 2.
- Produces:
  - `terrain::Page::GetWaterView() const` returning `water_lookup::PageWaterView`.
  - `terrain::Terrain::GetWaterTypeAtWorldPos(float x, float z) const` returning `WaterType`.
  - `terrain::Terrain::HasWaterAtWorldPos(float x, float z) const` returning `bool`.
  - Phase 2's `WaterVolumeSystem` consumes both through an interface it defines itself.

- [ ] **Step 1: Add the page water view accessor**

In `src/shared/terrain/page.h`, add to the public section near `GetWaterQuadMask` (around line 164):

```cpp
			/// @brief Gets a non-owning view of this page's water arrays for the pure lookup helpers.
			/// @return The view. Its pointers are null while the page is unloaded.
			[[nodiscard]] water_lookup::PageWaterView GetWaterView() const
			{
				if (m_waterQuadMasks.empty() || m_waterTypes.empty() || m_waterVertexHeights.empty())
				{
					return water_lookup::PageWaterView{};
				}

				return water_lookup::PageWaterView{ m_waterQuadMasks.data(), m_waterTypes.data(), m_waterVertexHeights.data() };
			}
```

Add `#include "terrain/water_lookup.h"` to the includes at the top of `page.h`.

- [ ] **Step 2: Declare the terrain queries**

In `src/shared/terrain/terrain.h`, directly below the existing `GetWaterHeightAtWorldPos` declaration (around line 551):

```cpp
			/// @brief Gets the liquid type at a world position.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @return The liquid type of the tile containing the position, or WaterType::None when
			///         the position is outside the terrain, its page is not loaded, or the quad
			///         under it carries no water.
			[[nodiscard]] WaterType GetWaterTypeAtWorldPos(float x, float z) const;

			/// @brief Checks whether there is any water surface at a world position.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @return True when the sub-quad containing the position carries water.
			/// @remark Always prefer this over testing GetWaterHeightAtWorldPos against 0. The water
			///         height grid is zero-initialised, so a height of 0 does not distinguish
			///         "surface at sea level" from "no water at all".
			[[nodiscard]] bool HasWaterAtWorldPos(float x, float z) const;
```

- [ ] **Step 3: Implement both**

In `src/shared/terrain/terrain.cpp`, directly after `GetWaterHeightAtWorldPos`:

```cpp
		namespace
		{
			/// Resolves a world position to its page and the page-local offset within it.
			/// Mirrors the page addressing GetWaterHeightAtWorldPos performs.
			struct PageHit
			{
				const Page* page { nullptr };
				float localX { 0.0f };
				float localZ { 0.0f };
			};
		}

		WaterType Terrain::GetWaterTypeAtWorldPos(const float x, const float z) const
		{
			const float halfW = static_cast<float>(m_width  * constants::PageSize) * 0.5f;
			const float halfH = static_cast<float>(m_height * constants::PageSize) * 0.5f;
			const int32 pageX = static_cast<int32>(std::floor((x + halfW) / constants::PageSize));
			const int32 pageZ = static_cast<int32>(std::floor((z + halfH) / constants::PageSize));
			if (pageX < 0 || pageZ < 0 || pageX >= static_cast<int32>(m_width) || pageZ >= static_cast<int32>(m_height))
			{
				return WaterType::None;
			}

			const Page* page = GetPage(pageX, pageZ);
			if (!page)
			{
				return WaterType::None;
			}

			const float pageOriginX = static_cast<float>((pageX - 32) * constants::PageSize);
			const float pageOriginZ = static_cast<float>((pageZ - 32) * constants::PageSize);

			const water_lookup::QuadCoord quad = water_lookup::QuadFromPageLocal(x - pageOriginX, z - pageOriginZ);
			if (!quad.valid)
			{
				return WaterType::None;
			}

			const water_lookup::PageWaterView view = page->GetWaterView();

			// Presence is authoritative. A tile can carry a type byte while the specific sub-quad
			// under this position has been erased, and the caller must see that as dry.
			if (!water_lookup::HasWaterAtQuad(view, quad.tileX, quad.tileZ, quad.qx, quad.qz))
			{
				return WaterType::None;
			}

			return water_lookup::TypeAtTile(view, quad.tileX, quad.tileZ);
		}

		bool Terrain::HasWaterAtWorldPos(const float x, const float z) const
		{
			return GetWaterTypeAtWorldPos(x, z) != WaterType::None;
		}
```

Remove the unused `PageHit` struct if the final implementation does not need it — it is listed above only to show the addressing is shared; if you do not factor it out, do not leave it behind.

- [ ] **Step 4: Build and confirm the terrain library still compiles**

```bash
cmake --build build --config Debug -t terrain
```

Expected: success.

- [ ] **Step 5: Run the full suite**

```bash
cmake --build build --config Debug -t all_tests && cd build && ctest -C Debug --output-on-failure
```

Expected: PASS. No behaviour visible to existing tests changed.

- [ ] **Step 6: Commit**

```bash
git add src/shared/terrain/terrain.h src/shared/terrain/terrain.cpp src/shared/terrain/page.h
git commit -m "feat(terrain): GetWaterTypeAtWorldPos and HasWaterAtWorldPos

Presence comes from the quad mask, so a tile carrying a type byte whose
sub-quad under the query point has been erased correctly reads as dry.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Header-only water quad bucketing

`Page::RebuildWaterMesh` currently emits one triangle-list operation for the whole page with one material. Per-`WaterType` materials need one operation per distinct type present. The grouping decision is pure arithmetic and belongs in a testable header.

**Files:**
- Create: `src/shared/terrain/water_mesh_build.h`
- Test: `src/tests/terrain_tests/test_water_mesh_build.cpp` (create)

**Interfaces:**
- Consumes: `water_lookup::PageWaterView`, `HasWaterAtQuad`, `TypeAtTile`, `QuadsPerTileSide` from Task 2.
- Produces, in `namespace mmo::terrain::water_mesh`:
  - `struct QuadRef { uint32 tileX, tileZ, qx, qz; };`
  - `struct TypeBatch { WaterType type; std::vector<QuadRef> quads; };`
  - `std::vector<TypeBatch> BucketQuadsByType(const water_lookup::PageWaterView& view)` — batches are returned in ascending `WaterType` order so mesh output is deterministic across rebuilds.
  - `constexpr uint32 BottomFaceVertexAlpha = 0x00;` and `constexpr uint32 TopFaceVertexAlpha = 0xFF;` — the alpha tag the material graph reads to shade the underside.
  - Task 5 consumes all of these.

- [ ] **Step 1: Write the failing test**

Create `src/tests/terrain_tests/test_water_mesh_build.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "terrain/water_mesh_build.h"

#include <vector>

using namespace mmo;
using namespace mmo::terrain;
using namespace mmo::terrain::water_mesh;

namespace
{
	struct PageFixture
	{
		std::vector<uint64> masks;
		std::vector<uint8> types;
		std::vector<float> heights;

		PageFixture()
			: masks(constants::TilesPerPage * constants::TilesPerPage, 0ULL)
			, types(constants::TilesPerPage * constants::TilesPerPage, static_cast<uint8>(WaterType::None))
			, heights(constants::OuterVerticesPerPageSide * constants::OuterVerticesPerPageSide, 0.0f)
		{
		}

		water_lookup::PageWaterView View() const
		{
			return water_lookup::PageWaterView{ masks.data(), types.data(), heights.data() };
		}

		void SetTileFull(uint32 tileX, uint32 tileZ, WaterType type)
		{
			const size_t idx = tileX + tileZ * constants::TilesPerPage;
			masks[idx] = ~0ULL;
			types[idx] = static_cast<uint8>(type);
		}

		void SetQuad(uint32 tileX, uint32 tileZ, uint32 qx, uint32 qz, WaterType type)
		{
			const size_t idx = tileX + tileZ * constants::TilesPerPage;
			masks[idx] |= (1ULL << (qx + qz * 8));
			types[idx] = static_cast<uint8>(type);
		}
	};
}

TEST_CASE("WaterMeshBuild_Empty_Page_Produces_No_Batches", "[water_mesh]")
{
	const PageFixture page;
	CHECK(BucketQuadsByType(page.View()).empty());
}

TEST_CASE("WaterMeshBuild_Null_View_Produces_No_Batches", "[water_mesh]")
{
	CHECK(BucketQuadsByType(water_lookup::PageWaterView{ nullptr, nullptr, nullptr }).empty());
}

TEST_CASE("WaterMeshBuild_Single_Type_Is_One_Batch", "[water_mesh]")
{
	PageFixture page;
	page.SetTileFull(0, 0, WaterType::Ocean);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());
	REQUIRE(batches.size() == 1);
	CHECK(batches[0].type == WaterType::Ocean);
	// A fully flooded tile is 8x8 sub-quads.
	CHECK(batches[0].quads.size() == 64);
}

TEST_CASE("WaterMeshBuild_Two_Types_Are_Two_Batches", "[water_mesh]")
{
	PageFixture page;
	page.SetTileFull(0, 0, WaterType::Ocean);
	page.SetTileFull(1, 0, WaterType::Water);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());
	REQUIRE(batches.size() == 2);

	// Ascending WaterType order: Water (1) before Ocean (2). Deterministic ordering keeps
	// rebuilt meshes stable, which matters because the render objects are recreated on every
	// page stream-in.
	CHECK(batches[0].type == WaterType::Water);
	CHECK(batches[1].type == WaterType::Ocean);
	CHECK(batches[0].quads.size() == 64);
	CHECK(batches[1].quads.size() == 64);
}

TEST_CASE("WaterMeshBuild_Skips_Unset_Quads", "[water_mesh]")
{
	PageFixture page;
	page.SetQuad(2, 3, 1, 1, WaterType::Ocean);
	page.SetQuad(2, 3, 4, 5, WaterType::Ocean);

	const std::vector<TypeBatch> batches = BucketQuadsByType(page.View());
	REQUIRE(batches.size() == 1);
	REQUIRE(batches[0].quads.size() == 2);

	CHECK(batches[0].quads[0].tileX == 2);
	CHECK(batches[0].quads[0].tileZ == 3);
	CHECK(batches[0].quads[0].qx == 1);
	CHECK(batches[0].quads[0].qz == 1);
	CHECK(batches[0].quads[1].qx == 4);
	CHECK(batches[0].quads[1].qz == 5);
}

TEST_CASE("WaterMeshBuild_Tile_With_Type_But_No_Quads_Is_Omitted", "[water_mesh]")
{
	// Erasing every quad of a tile without clearing its type byte must not produce an empty
	// batch, or RebuildWaterMesh would create a render operation with no triangles and trip
	// the assert in Finish().
	PageFixture page;
	const size_t idx = 5 + 5 * constants::TilesPerPage;
	page.types[idx] = static_cast<uint8>(WaterType::Ocean);
	page.masks[idx] = 0ULL;

	CHECK(BucketQuadsByType(page.View()).empty());
}

TEST_CASE("WaterMeshBuild_Face_Alpha_Tags_Differ", "[water_mesh]")
{
	// The material graph keys the underside look off vertex colour alpha, so the two values
	// must not collide.
	CHECK(TopFaceVertexAlpha != BottomFaceVertexAlpha);
}
```

- [ ] **Step 2: Run the test and confirm it fails**

```bash
cmake --build build --config Debug -t terrain_tests
```

Expected: compile error — `cannot open source file "terrain/water_mesh_build.h"`.

- [ ] **Step 3: Write the header**

Create `src/shared/terrain/water_mesh_build.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain/constants.h"
#include "terrain/water_lookup.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		/// @brief Pure grouping of a page's water sub-quads for mesh construction.
		///
		/// @remark Dependency-free for the same reason as water_lookup: terrain_tests links only
		///         base and math.
		namespace water_mesh
		{
			/// @brief Address of one water sub-quad within a page.
			struct QuadRef
			{
				uint32 tileX { 0 };
				uint32 tileZ { 0 };
				uint32 qx { 0 };
				uint32 qz { 0 };
			};

			/// @brief All sub-quads of a page sharing one liquid type. One render operation is
			///        emitted per batch so each type can carry its own material.
			struct TypeBatch
			{
				WaterType type { WaterType::None };
				std::vector<QuadRef> quads;
			};

			/// @brief Vertex colour alpha written to top-face water vertices.
			constexpr uint32 TopFaceVertexAlpha = 0xFFu;

			/// @brief Vertex colour alpha written to bottom-face water vertices.
			/// @remark The material graph compares vertex colour alpha against a midpoint to switch
			///         to the underside look (darker, silvery, inverted Fresnel) when the surface is
			///         seen from below. Keeping the distinction in vertex data avoids plumbing a
			///         front-facing semantic through the material compiler.
			constexpr uint32 BottomFaceVertexAlpha = 0x00u;

			/// @brief Groups every water-carrying sub-quad of a page by its tile's liquid type.
			/// @param view The page's water arrays.
			/// @return One batch per distinct type actually present, in ascending WaterType order.
			///         Types whose quads have all been erased produce no batch at all, so callers
			///         never build an empty render operation.
			inline std::vector<TypeBatch> BucketQuadsByType(const water_lookup::PageWaterView& view)
			{
				std::vector<TypeBatch> batches;

				if (view.quadMasks == nullptr || view.types == nullptr)
				{
					return batches;
				}

				for (uint32 tz = 0; tz < constants::TilesPerPage; ++tz)
				{
					for (uint32 tx = 0; tx < constants::TilesPerPage; ++tx)
					{
						const uint64 mask = view.quadMasks[tx + tz * constants::TilesPerPage];
						if (mask == 0ULL)
						{
							continue;
						}

						const WaterType type = water_lookup::TypeAtTile(view, tx, tz);

						auto it = std::find_if(batches.begin(), batches.end(),
							[type](const TypeBatch& batch) { return batch.type == type; });
						if (it == batches.end())
						{
							batches.push_back(TypeBatch{ type, {} });
							it = batches.end() - 1;
						}

						for (uint32 qz = 0; qz < water_lookup::QuadsPerTileSide; ++qz)
						{
							for (uint32 qx = 0; qx < water_lookup::QuadsPerTileSide; ++qx)
							{
								if ((mask & (1ULL << (qx + qz * water_lookup::QuadsPerTileSide))) == 0ULL)
								{
									continue;
								}

								it->quads.push_back(QuadRef{ tx, tz, qx, qz });
							}
						}
					}
				}

				// Deterministic order: page render objects are destroyed and rebuilt on every
				// stream-in, and unstable batch order would shuffle draw order frame to frame.
				std::sort(batches.begin(), batches.end(),
					[](const TypeBatch& lhs, const TypeBatch& rhs)
					{
						return static_cast<uint8>(lhs.type) < static_cast<uint8>(rhs.type);
					});

				return batches;
			}
		}
	}
}
```

- [ ] **Step 4: Run the test and confirm it passes**

```bash
cmake --build build --config Debug -t terrain_tests && cd build && ctest -C Debug -R terrain_tests --output-on-failure
```

Expected: PASS, seven cases tagged `[water_mesh]`.

- [ ] **Step 5: Commit**

```bash
git add src/shared/terrain/water_mesh_build.h src/tests/terrain_tests/test_water_mesh_build.cpp
git commit -m "feat(terrain): bucket water quads by liquid type for meshing

One render operation per distinct WaterType present so each type can carry
its own material. Batches sort by type for stable draw order across the
page rebuilds that happen on every stream-in, and a tile whose quads were
all erased produces no batch rather than an empty operation.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Per-type water mesh with a correct tangent basis

**Files:**
- Modify: `src/shared/terrain/page.cpp` — `RebuildWaterMesh` (around line 1607)
- Modify: `src/shared/terrain/page.h` — add the profile-resolution callback

**Interfaces:**
- Consumes: `Triangle::SetNormal/SetTangent/SetBinormal` (Task 1), `BucketQuadsByType`, `TypeBatch`, `QuadRef`, `TopFaceVertexAlpha`, `BottomFaceVertexAlpha` (Task 4).
- Produces: `terrain::Page::SetWaterMaterialResolver(std::function<String(WaterType)>)` — a static/terrain-level hook the client installs in Task 7 to map a type to its profile's material. When unset, behaviour is exactly as today.

- [ ] **Step 1: Add the resolver hook**

In `src/shared/terrain/page.h`, add to the public section near `SetWaterMaterialName`:

```cpp
			/// @brief Sets the callback resolving a liquid type to its surface material asset name.
			/// @param resolver The callback. Pass an empty function to fall back to the page's own
			///        water material name for every type.
			/// @remark Installed once by the client from the water profile table. The terrain library
			///         deliberately does not depend on client_data, so the mapping is injected.
			static void SetWaterMaterialResolver(std::function<String(WaterType)> resolver);
```

and a private static:

```cpp
			/// @brief Resolves a liquid type to a material asset name. May be empty.
			static std::function<String(WaterType)> ms_waterMaterialResolver;
```

Add `#include <functional>` and `#include "terrain/water_mesh_build.h"` to `page.h`.

- [ ] **Step 2: Rewrite RebuildWaterMesh**

In `src/shared/terrain/page.cpp`, define the static and replace the body of `RebuildWaterMesh`:

```cpp
		std::function<String(WaterType)> Page::ms_waterMaterialResolver;

		void Page::SetWaterMaterialResolver(std::function<String(WaterType)> resolver)
		{
			ms_waterMaterialResolver = std::move(resolver);
		}

		void Page::RebuildWaterMesh()
		{
			if (!m_waterRenderObject)
			{
				return;
			}

			m_waterRenderObject->Clear();

			const std::vector<water_mesh::TypeBatch> batches = water_mesh::BucketQuadsByType(GetWaterView());
			if (batches.empty())
			{
				return;
			}

			// Each outer vertex step in local page space.
			constexpr float quadSize = water_lookup::QuadSize;
			constexpr uint32 pvSide = constants::OuterVerticesPerPageSide;

			// World offset used only for UV tiling so the texture is continuous across pages.
			const float worldOffsetX = static_cast<float>((m_x - 32) * constants::PageSize);
			const float worldOffsetZ = static_cast<float>((m_z - 32) * constants::PageSize);
			constexpr float uvScale = 1.0f / 16.0f;

			for (const water_mesh::TypeBatch& batch : batches)
			{
				// In minimap mode we deliberately ignore the assigned (translucent) water material:
				// it samples the scene depth/refraction textures which are not bound during minimap
				// generation and would render as garbage or fully transparent. Instead we use an
				// opaque, unlit vertex-colour material so water shows up as a solid blue area.
				MaterialPtr material;
				if (m_minimapWaterMode)
				{
					material = MaterialManager::Get().Load("Editor/MinimapWater.hmat");
				}
				else if (!m_waterMaterialName.empty())
				{
					// An explicit per-page override wins over the profile table, so existing
					// authored pages keep rendering exactly as before.
					material = MaterialManager::Get().Load(m_waterMaterialName);
				}
				else if (ms_waterMaterialResolver)
				{
					const String resolved = ms_waterMaterialResolver(batch.type);
					if (!resolved.empty())
					{
						material = MaterialManager::Get().Load(resolved);
					}
				}

				if (!material)
				{
					material = MaterialManager::Get().Load("Editor/Wireframe.hmat");
				}

				if (!material)
				{
					continue;
				}

				auto op = m_waterRenderObject->AddTriangleListOperation(material);

				for (const water_mesh::QuadRef& quad : batch.quads)
				{
					const uint32 pvx0 = quad.tileX * water_lookup::QuadsPerTileSide + quad.qx;
					const uint32 pvz0 = quad.tileZ * water_lookup::QuadsPerTileSide + quad.qz;

					const float yTL = m_waterVertexHeights[ pvx0      + pvz0      * pvSide];
					const float yTR = m_waterVertexHeights[(pvx0 + 1) + pvz0      * pvSide];
					const float yBL = m_waterVertexHeights[ pvx0      + (pvz0+1)  * pvSide];
					const float yBR = m_waterVertexHeights[(pvx0 + 1) + (pvz0+1)  * pvSide];

					const float lx1 = pvx0       * quadSize;
					const float lz1 = pvz0       * quadSize;
					const float lx2 = (pvx0 + 1) * quadSize;
					const float lz2 = (pvz0 + 1) * quadSize;

					const float u1 = (worldOffsetX + lx1) * uvScale;
					const float u2 = (worldOffsetX + lx2) * uvScale;
					const float v1 = (worldOffsetZ + lz1) * uvScale;
					const float v2 = (worldOffsetZ + lz2) * uvScale;

					const Vector3 vTL(lx1, yTL, lz1);
					const Vector3 vTR(lx2, yTR, lz1);
					const Vector3 vBR(lx2, yBR, lz2);
					const Vector3 vBL(lx1, yBL, lz2);

					// Vertex colour carries the face tag in alpha; RGB is white for the normal
					// material and an opaque blue for the minimap material, which renders it directly.
					const uint32 rgb = m_minimapWaterMode ? 0x3A6EA5u : 0xFFFFFFu;
					const uint32 topColor = (water_mesh::TopFaceVertexAlpha << 24) | rgb;
					const uint32 bottomColor = (water_mesh::BottomFaceVertexAlpha << 24) | rgb;

					// Top face (CCW winding = front-facing from above). N=+Y, T=+X, B=+Z gives a
					// right-handed orthonormal basis for the horizontal surface, which is what the
					// normal map in the water material is authored against.
					{
						auto& t1 = op->AddTriangle(vTL, vBL, vTR);
						t1.SetUV(0, u1, v1); t1.SetUV(1, u1, v2); t1.SetUV(2, u2, v1);
						t1.SetColor(topColor);
						SetTopFaceBasis(t1);

						auto& t2 = op->AddTriangle(vTR, vBL, vBR);
						t2.SetUV(0, u2, v1); t2.SetUV(1, u1, v2); t2.SetUV(2, u2, v2);
						t2.SetColor(topColor);
						SetTopFaceBasis(t2);
					}

					// Bottom face (reversed winding so water is visible from below). The normal
					// flips to -Y and the binormal to -Z so the basis stays right-handed.
					{
						auto& t3 = op->AddTriangle(vTR, vBL, vTL);
						t3.SetUV(0, u2, v1); t3.SetUV(1, u1, v2); t3.SetUV(2, u1, v1);
						t3.SetColor(bottomColor);
						SetBottomFaceBasis(t3);

						auto& t4 = op->AddTriangle(vBR, vBL, vTR);
						t4.SetUV(0, u2, v2); t4.SetUV(1, u1, v2); t4.SetUV(2, u2, v1);
						t4.SetColor(bottomColor);
						SetBottomFaceBasis(t4);
					}
				}
			}
		}
```

Add the two helpers in the anonymous namespace at the top of `page.cpp`:

```cpp
	namespace
	{
		/// Applies the tangent basis for an upward-facing horizontal water quad.
		void SetTopFaceBasis(ManualTriangleListOperation::Triangle& triangle)
		{
			for (uint8 i = 0; i < 3; ++i)
			{
				triangle.SetNormal(i, Vector3::UnitY);
				triangle.SetTangent(i, Vector3::UnitX);
				triangle.SetBinormal(i, Vector3::UnitZ);
			}
		}

		/// Applies the tangent basis for a downward-facing horizontal water quad.
		void SetBottomFaceBasis(ManualTriangleListOperation::Triangle& triangle)
		{
			for (uint8 i = 0; i < 3; ++i)
			{
				triangle.SetNormal(i, -Vector3::UnitY);
				triangle.SetTangent(i, Vector3::UnitX);
				triangle.SetBinormal(i, -Vector3::UnitZ);
			}
		}
	}
```

- [ ] **Step 3: Build**

```bash
cmake --build build --config Debug -t terrain
```

Expected: success. If `AddTriangle` returns a type other than `ManualTriangleListOperation::Triangle&`, adjust the helper signatures to match — do not cast.

- [ ] **Step 4: Run the full suite**

```bash
cmake --build build --config Debug -t all_tests && cd build && ctest -C Debug --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Verify minimap water still renders**

Build and launch `mmo_edit`, open a world with water painted, and regenerate minimap tiles. Water must still appear as solid blue. The minimap material reads vertex colour directly, and this task changed the alpha byte it receives from `0xAA` to `0xFF`/`0x00`.

If the minimap water now renders with wrong opacity, that confirms `Editor/MinimapWater.hmat` consumes vertex alpha. In that case keep alpha at `0xAA` for both faces while `m_minimapWaterMode` is set, and only apply the face tags in normal mode. Record which way it went in the commit message.

- [ ] **Step 6: Commit**

```bash
git add src/shared/terrain/page.cpp src/shared/terrain/page.h
git commit -m "feat(terrain): per-liquid-type water batches with a real tangent basis

Water quads now group by WaterType, one render operation each, so a page
spanning ocean and lake renders both with their own material. Material
resolution order is: minimap override, then the page's explicit material
name, then the injected profile resolver, then wireframe.

Top faces get N=+Y T=+X B=+Z and bottom faces N=-Y T=+X B=-Z, both
right-handed, so the water normal map finally resolves correctly. Face
identity is carried in vertex colour alpha for the underside shading path.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Sun direction and colour globals

**Files:**
- Modify: `src/shared/graphics/sky_component.cpp` — `UpdateLighting`, next to the existing sky colour publishes (around line 337)
- Modify: `data/client/Config/GlobalShaderParameters.hgsp` — via `mmo_edit`, see step 3

**Interfaces:**
- Consumes: nothing.
- Produces: global shader parameters `SunDirection` (vector, xyz = unit vector pointing *toward* the sun, w unused) and `SunColor` (vector, rgb = blended sun/moon colour, a = intensity). Task 9's material graph reads both through `GlobalVectorParameterNode`.

- [ ] **Step 1: Publish the globals**

In `src/shared/graphics/sky_component.cpp`, in `UpdateLighting`, directly after the two existing publishes:

```cpp
        GlobalShaderParameters::Get().SetVector("SkyHorizonColor", horizonColor);
        GlobalShaderParameters::Get().SetVector("SkyZenithColor", zenithColor);

        // Direction pointing TOWARD the light, matching the convention the forward camera
        // constant buffer uses (Scene::UpdatePsCameraBuffer negates the raw light direction for
        // the same reason). Materials reading this must not negate it again.
        Vector3 towardSun = -lightDir;
        towardSun.Normalize();
        GlobalShaderParameters::Get().SetVector("SunDirection",
            Vector4(towardSun.x, towardSun.y, towardSun.z, 0.0f));

        // rgb is the blended sun/moon colour, a carries intensity so a material can reconstruct
        // the full contribution from one parameter.
        GlobalShaderParameters::Get().SetVector("SunColor",
            Vector4(blendedColor.x, blendedColor.y, blendedColor.z, blendedIntensity));
```

- [ ] **Step 2: Build**

```bash
cmake --build build --config Debug -t graphics
```

Expected: success.

- [ ] **Step 3: Define the parameters in the registry asset**

`SetVector` only assigns to an already-*defined* parameter; it returns false for an unknown name. The definitions live in `data/client/Config/GlobalShaderParameters.hgsp`, which currently holds only `SkyHorizonColor` and `SkyZenithColor`.

Open `mmo_edit`, open the global shader parameter editor, and add two **vector** parameters:

| Name | Default |
|---|---|
| `SunDirection` | `(0, 1, 0, 0)` |
| `SunColor` | `(1, 0.95, 0.9, 1)` |

Save. Confirm the asset changed:

```bash
cd data/client && git status --short Config/GlobalShaderParameters.hgsp
```

Expected: the file shows as modified.

If the editor has no UI for this registry, add the definitions from code instead — find where `GlobalShaderParameters::Get().DefineVector("SkyHorizonColor", ...)` is called at startup and add the two new `DefineVector` calls beside it. Do not hand-edit the binary `.hgsp`.

- [ ] **Step 4: Verify at runtime**

Launch the client or `mmo_edit` with a sky, and confirm via the console or a watch that `SunDirection` changes over the day/night cycle and that its Y component is positive at noon.

- [ ] **Step 5: Commit both repos**

```bash
cd data/client
git add Config/GlobalShaderParameters.hgsp
git commit -m "data: define SunDirection and SunColor global shader parameters

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
cd ../..
git add src/shared/graphics/sky_component.cpp data/client
git commit -m "feat(graphics): publish SunDirection and SunColor globals

Lets material graphs compute a sun glint without a material compiler change.
SunDirection points toward the sun, matching the forward camera constant
buffer convention, so materials must not negate it again.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Water profile client data

**Files:**
- Create: `src/shared/client_data/water_profiles.proto`
- Modify: `src/shared/client_data/project.h` (lines 34, 64, 108, 178, 232 carry the `surface_types` pattern to mirror)
- Test: `src/tests/client_data_tests/test_water_profiles.cpp` (create)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `mmo::proto_client::WaterProfile` / `WaterProfiles` messages.
  - `mmo::proto_client::Project::waterProfiles`, a `WaterProfileManager`.
  - Task 8 (editor) and Phase 2 (`WaterVolumeSystem`) both read it.

- [ ] **Step 1: Write the proto**

Create `src/shared/client_data/water_profiles.proto`:

```proto
syntax = "proto2";
package mmo.proto_client;

// Per-liquid-type presentation settings. Client-only: the server resolves swimming from
// its own water map and needs none of this.
//
// The id matches terrain::WaterType exactly (1 = Water, 2 = Ocean, 3 = Lava, 4 = Slime).
// Field numbers are part of the ClientDB contract and must never be reused or renumbered.
message WaterProfile
{
	required uint32 id = 1;
	required string name = 2;

	// Surface material asset used for this liquid, e.g. "Worlds/Water_Ocean.hmat".
	// A page's own water material name still overrides this when set.
	optional string surface_material = 3;

	// Underwater fog colour as packed 0xAARRGGBB.
	optional uint32 fog_color = 4;

	// Underwater fog density per world unit. Higher is murkier.
	optional float fog_density = 5;

	// Beer-Lambert absorption colour as packed 0xAARRGGBB, driving the depth tint.
	optional uint32 absorption_color = 6;

	// Strength of the projected caustics while submerged, 0 disables them.
	optional float caustics_strength = 7;

	// Caustics texture asset, e.g. "Textures/Caustics_01.htex".
	optional string caustics_texture = 8;

	// Low-pass filter cutoff in Hz applied to audio while submerged. 0 leaves audio dry.
	optional float audio_lowpass_hz = 9;

	// Screen distortion strength while submerged, 0 disables it.
	optional float distortion_strength = 10;
}

message WaterProfiles
{
	repeated WaterProfile entry = 1;
}
```

- [ ] **Step 2: Register the manager**

In `src/shared/client_data/project.h`, mirroring the five `surface_types` sites exactly:

Near line 34, beside the other includes:
```cpp
#include "shared/client_data/proto_client/water_profiles.pb.h"
```

Near line 64, beside `SurfaceTypeManager`:
```cpp
		typedef TemplateManager<mmo::proto_client::WaterProfiles, mmo::proto_client::WaterProfile> WaterProfileManager;
```

Near line 108, beside `surfaceTypes`:
```cpp
			WaterProfileManager waterProfiles;
```

Near line 178, beside the `surface_types` manager entry:
```cpp
				managers.push_back(ManagerEntry("water_profiles", waterProfiles, true));
```

Near line 232, beside the second `surface_types` entry:
```cpp
				managers.emplace_back("water_profiles", "water_profiles", waterProfiles);
```

- [ ] **Step 3: Write the failing test**

Create `src/tests/client_data_tests/test_water_profiles.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "shared/client_data/proto_client/water_profiles.pb.h"

#include "terrain/constants.h"

using namespace mmo;

TEST_CASE("WaterProfile_Ids_Match_WaterType_Enum", "[water_profiles]")
{
	// The profile id IS the terrain water type. If these ever drift, every liquid renders
	// with the wrong material and the wrong underwater fog, with nothing to point at.
	CHECK(static_cast<uint32>(terrain::WaterType::Water) == 1u);
	CHECK(static_cast<uint32>(terrain::WaterType::Ocean) == 2u);
	CHECK(static_cast<uint32>(terrain::WaterType::Lava) == 3u);
	CHECK(static_cast<uint32>(terrain::WaterType::Slime) == 4u);
}

TEST_CASE("WaterProfile_Round_Trips_Through_Serialization", "[water_profiles]")
{
	proto_client::WaterProfiles profiles;

	proto_client::WaterProfile* ocean = profiles.add_entry();
	ocean->set_id(static_cast<uint32>(terrain::WaterType::Ocean));
	ocean->set_name("Ocean");
	ocean->set_surface_material("Worlds/Water_Ocean.hmat");
	ocean->set_fog_color(0xFF1E4D5Au);
	ocean->set_fog_density(0.035f);
	ocean->set_absorption_color(0xFF2E6B78u);
	ocean->set_caustics_strength(0.6f);
	ocean->set_audio_lowpass_hz(900.0f);
	ocean->set_distortion_strength(0.4f);

	std::string bytes;
	REQUIRE(profiles.SerializeToString(&bytes));

	proto_client::WaterProfiles parsed;
	REQUIRE(parsed.ParseFromString(bytes));
	REQUIRE(parsed.entry_size() == 1);

	const proto_client::WaterProfile& got = parsed.entry(0);
	CHECK(got.id() == static_cast<uint32>(terrain::WaterType::Ocean));
	CHECK(got.name() == "Ocean");
	CHECK(got.surface_material() == "Worlds/Water_Ocean.hmat");
	CHECK(got.fog_color() == 0xFF1E4D5Au);
	CHECK(got.fog_density() == Approx(0.035f));
	CHECK(got.audio_lowpass_hz() == Approx(900.0f));
}

TEST_CASE("WaterProfile_Optional_Fields_Report_Absence", "[water_profiles]")
{
	// A profile that leaves caustics unset must be distinguishable from one that sets them
	// to zero, so consumers can fall back to a default rather than silently disabling them.
	proto_client::WaterProfile bare;
	bare.set_id(static_cast<uint32>(terrain::WaterType::Water));
	bare.set_name("Water");

	CHECK_FALSE(bare.has_caustics_strength());
	CHECK_FALSE(bare.has_surface_material());

	bare.set_caustics_strength(0.0f);
	CHECK(bare.has_caustics_strength());
	CHECK(bare.caustics_strength() == Approx(0.0f));
}
```

- [ ] **Step 4: Run the test and confirm it fails**

```bash
cmake --build build --config Debug -t client_data_tests
```

Expected: compile error — the generated `water_profiles.pb.h` does not exist yet. Re-run the CMake configure step so the proto is picked up and generated:

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```

Then rebuild. If `client_data_tests` cannot see `terrain/constants.h`, add `terrain` to its include directories only — **not** to its link libraries, since `constants.h` is header-only.

- [ ] **Step 5: Run the test and confirm it passes**

```bash
cmake --build build --config Debug -t client_data_tests && cd build && ctest -C Debug -R client_data_tests --output-on-failure
```

Expected: PASS, three cases tagged `[water_profiles]`.

- [ ] **Step 6: Author the Ocean profile**

In `mmo_edit`, open the water profile data editor and add one entry:

| Field | Value |
|---|---|
| id | 2 |
| name | Ocean |
| surface_material | `Worlds/Water_Ocean.hmat` |
| fog_color | `0xFF1E4D5A` |
| fog_density | 0.035 |
| absorption_color | `0xFF2E6B78` |
| caustics_strength | 0.6 |
| caustics_texture | `Textures/Caustics_01.htex` |
| audio_lowpass_hz | 900 |
| distortion_strength | 0.4 |

`Textures/Caustics_01.htex` and `Worlds/Water_Ocean.hmat` do not exist yet — they arrive in Tasks 9 and Phase 2. A dangling asset name here is harmless; the consumer falls back.

Save and export to ClientDB.

- [ ] **Step 7: Commit**

```bash
git add src/shared/client_data/water_profiles.proto src/shared/client_data/project.h src/tests/client_data_tests/test_water_profiles.cpp
git commit -m "feat(client_data): water_profiles proto binding WaterType to presentation

Profile ids are terrain::WaterType values, asserted by a test so the two
cannot silently drift. Client-only: the server resolves swimming from its
own water map.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
cd data/editor && git add -A && git commit -m "data: Ocean water profile

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>" && cd ../..
git add data/editor && git commit -m "data: bump editor submodule for the Ocean water profile

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: ScreenSpaceReflection material node

The one genuinely new shader capability. Ray-marches the linear scene depth already bound at t31 and samples the scene colour copy at t30, both inside the water pixel shader during the forward pass.

**Files:**
- Modify: `src/shared/graphics/material_compiler.h` — declare the virtual beside `AddSceneColor`
- Modify: `src/shared/graphics_d3d11/material_compiler_d3d11.h` / `.cpp` — HLSL
- Modify: `src/shared/graphics_metal/material_compiler_metal.h` / `.mm` — MSL
- Modify: `src/mmo_edit/editors/material_editor/material_node.h` / `.cpp` — the node
- Modify: `src/mmo_edit/editors/material_editor/node_editor/node_registry.cpp` — register it (the list around line 61)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `MaterialCompiler::AddScreenSpaceReflection(ExpressionIndex worldNormal, ExpressionIndex maxDistance, ExpressionIndex stepCount)` returning a `Float_4` expression — `rgb` is the reflected colour, `a` is the hit mask in `[0,1]`.
  - `ScreenSpaceReflectionNode` with input pins `Normal`, `Max Distance`, `Steps` and output pins `Color` (float3) and `Hit Mask` (float1).
  - Task 9's material graph consumes the node.

- [ ] **Step 1: Declare the virtual**

In `src/shared/graphics/material_compiler.h`, directly after `AddSceneColor`:

```cpp
		/// @brief Adds a screen-space reflection expression that ray-marches the opaque scene.
		/// @details Requires the engine to bind the opaque scene's linear depth and the captured
		///          scene color, which the deferred renderer already does for the forward pass.
		///          Rays that leave the screen or find no intersection report a zero hit mask so
		///          the graph can blend to a sky fallback; the node never invents a colour.
		/// @param worldNormal The reflecting surface normal in world space (float3). IndexNone uses
		///        the interpolated vertex normal.
		/// @param maxDistance Maximum ray length in world units (float1). IndexNone uses 256.
		/// @param stepCount Number of march steps (float1, rounded and clamped to [4, 64]).
		///        IndexNone uses 24.
		/// @return Index of the reflection expression (float4: rgb = colour, a = hit mask) or
		///         IndexNone in case of an error.
		virtual ExpressionIndex AddScreenSpaceReflection(ExpressionIndex worldNormal,
			ExpressionIndex maxDistance, ExpressionIndex stepCount) = 0;
```

- [ ] **Step 2: Build and confirm both backends fail to compile**

```bash
cmake --build build --config Debug -t graphics_d3d11
```

Expected: error — `MaterialCompilerD3D11` is abstract, `AddScreenSpaceReflection` not implemented. This is the check that the Metal backend cannot be forgotten: a pure virtual makes omission a build failure rather than a silent macOS-only bug.

- [ ] **Step 3: Implement the D3D11 backend**

In `src/shared/graphics_d3d11/material_compiler_d3d11.h`, add the override declaration and a gate flag:

```cpp
		ExpressionIndex AddScreenSpaceReflection(ExpressionIndex worldNormal,
			ExpressionIndex maxDistance, ExpressionIndex stepCount) override;
```

```cpp
		/// @brief Whether any expression needs the ComputeSSR helper emitted into the shader.
		bool m_needsScreenSpaceReflection { false };
```

In `src/shared/graphics_d3d11/material_compiler_d3d11.cpp`, after `AddSceneColor`:

```cpp
	ExpressionIndex MaterialCompilerD3D11::AddScreenSpaceReflection(const ExpressionIndex worldNormal,
		const ExpressionIndex maxDistance, const ExpressionIndex stepCount)
	{
		m_needsScreenSpaceReflection = true;
		m_needsSceneColor = true;
		m_needsSceneDepth = true;

		std::ostringstream outputStream;
		outputStream << "ComputeSSR(input.pos.xy, ";

		if (worldNormal != IndexNone)
		{
			outputStream << "normalize(expr_" << worldNormal << ".xyz)";
		}
		else
		{
			outputStream << "normalize(input.normal.xyz)";
		}

		outputStream << ", ";
		if (maxDistance != IndexNone)
		{
			outputStream << "expr_" << maxDistance;
		}
		else
		{
			outputStream << "256.0";
		}

		outputStream << ", ";
		if (stepCount != IndexNone)
		{
			outputStream << "expr_" << stepCount;
		}
		else
		{
			outputStream << "24.0";
		}

		outputStream << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
	}
```

Then, in `GeneratePixelShaderCode`, directly after the `m_needsSceneColor` texture declaration block, emit the helper:

```cpp
		if (m_needsScreenSpaceReflection)
		{
			// March the reflected ray through the opaque scene's linear depth buffer. The scene
			// depth texture stores linear view-space distance in its alpha channel, so the test is
			// a direct comparison rather than a projection-matrix inversion.
			//
			// Returns a zero hit mask rather than a fallback colour when the ray leaves the screen
			// or finds nothing. At grazing angles - looking out to sea, which is most of the time
			// for an ocean - the reflected ray exits the top of the screen almost immediately, so
			// the miss path is the common path and the graph must supply the sky itself.
			m_pixelShaderStream
				<< "float4 ComputeSSR(float2 screenPos, float3 worldNormal, float maxDistance, float steps)\n"
				<< "{\n"
				<< "\tfloat3 toPixel = normalize(GetWorldPosition() - cameraPosition.xyz);\n"
				<< "\tfloat3 reflectDir = reflect(toPixel, worldNormal);\n\n"
				<< "\t// Rays heading into the surface can never hit anything in front of it.\n"
				<< "\tif (reflectDir.y < 0.001f)\n"
				<< "\t{\n"
				<< "\t\treturn float4(0.0f, 0.0f, 0.0f, 0.0f);\n"
				<< "\t}\n\n"
				<< "\tint stepCount = (int)clamp(steps, 4.0f, 64.0f);\n"
				<< "\tfloat3 rayOrigin = GetWorldPosition();\n"
				<< "\tfloat stepLength = maxDistance / (float)stepCount;\n\n"
				<< "\tfor (int i = 1; i <= stepCount; ++i)\n"
				<< "\t{\n"
				<< "\t\tfloat3 samplePos = rayOrigin + reflectDir * (stepLength * (float)i);\n"
				<< "\t\tfloat4 clipPos = mul(float4(samplePos, 1.0f), viewProjectionMatrix);\n"
				<< "\t\tif (clipPos.w <= 0.0f)\n"
				<< "\t\t{\n"
				<< "\t\t\tbreak;\n"
				<< "\t\t}\n\n"
				<< "\t\tfloat2 ndc = clipPos.xy / clipPos.w;\n"
				<< "\t\tif (abs(ndc.x) > 1.0f || abs(ndc.y) > 1.0f)\n"
				<< "\t\t{\n"
				<< "\t\t\tbreak;\n"
				<< "\t\t}\n\n"
				<< "\t\tfloat2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);\n"
				<< "\t\tint2 pixel = (int2)(uv * targetSize.xy);\n"
				<< "\t\tfloat sceneLinearDepth = sceneDepthTex.Load(int3(pixel, 0)).a;\n"
				<< "\t\tfloat rayLinearDepth = clipPos.w;\n\n"
				<< "\t\t// A hit is the ray passing behind recorded geometry. The thickness window\n"
				<< "\t\t// stops a ray from matching something far behind the first surface, which\n"
				<< "\t\t// is what produces smeared reflections under thin geometry.\n"
				<< "\t\tfloat delta = rayLinearDepth - sceneLinearDepth;\n"
				<< "\t\tif (delta > 0.0f && delta < stepLength * 2.0f)\n"
				<< "\t\t{\n"
				<< "\t\t\tfloat3 hitColor = sceneColorTex.Load(int3(pixel, 0)).rgb;\n"
				<< "\t\t\t// Fade at the screen edges so reflections do not pop as geometry\n"
				<< "\t\t\t// crosses the viewport boundary.\n"
				<< "\t\t\tfloat2 edgeFade = smoothstep(0.0f, 0.15f, 1.0f - abs(ndc));\n"
				<< "\t\t\tfloat mask = saturate(edgeFade.x * edgeFade.y);\n"
				<< "\t\t\treturn float4(hitColor, mask);\n"
				<< "\t\t}\n"
				<< "\t}\n\n"
				<< "\treturn float4(0.0f, 0.0f, 0.0f, 0.0f);\n"
				<< "}\n\n";
		}
```

**Before writing this, verify the three engine-provided names it uses actually exist in the generated shader:** `GetWorldPosition()`, `cameraPosition`, `viewProjectionMatrix` and `targetSize`. Grep the generator:

```bash
grep -n "GetWorldPosition\|cameraPosition\|viewProjectionMatrix\|targetSize" src/shared/graphics_d3d11/material_compiler_d3d11.cpp src/shared/graphics_d3d11/shaders/Matrices.hlsli src/shared/graphics_d3d11/shaders/VS_InOut.hlsli
```

Substitute whatever the generator actually emits. If no view-projection matrix is available in the pixel shader, the cleanest fix is to add it to the existing matrix constant buffer rather than reconstructing it — but check `Matrices.hlsli` first, since the b0/b12 matrix split already carries several matrices.

- [ ] **Step 4: Implement the Metal backend**

In `src/shared/graphics_metal/material_compiler_metal.h` / `.mm`, add the same override. Mirror the D3D11 structure exactly, translating to MSL:

- `mul(v, m)` becomes `m * v`
- `sceneDepthTex.Load(int3(pixel, 0)).a` becomes `sceneDepthTex.read(uint2(pixel)).a`
- `saturate(x)` becomes `clamp(x, 0.0f, 1.0f)`
- `float4`/`float3`/`float2` are spelled the same

Keep the emitted helper's name and signature identical (`ComputeSSR`) so the expression string produced by both backends is the same. If the Metal backend has no equivalent of some engine-provided name, implement the node as a constant zero there and `WLOG` once at compile time rather than emitting broken MSL — macOS is a secondary target and a non-compiling shader is worse than a missing reflection. Record that decision in the commit message.

- [ ] **Step 5: Add the editor node**

In `src/mmo_edit/editors/material_editor/material_node.h`, following the `SceneDepthNode` shape at line 2683:

```cpp
	/// @brief A node that ray-marches the opaque scene to produce a screen-space reflection.
	class ScreenSpaceReflectionNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(ScreenSpaceReflectionNode, "Screen Space Reflection")

		ScreenSpaceReflectionNode(MaterialGraph& material)
			: GraphNode(material)
		{
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return m_outputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
		/// @brief Reflecting surface normal in world space. Unconnected uses the vertex normal.
		MaterialPin m_normal = { this, "Normal" };

		/// @brief Maximum ray length in world units. Unconnected uses 256.
		MaterialPin m_maxDistance = { this, "Max Distance" };

		/// @brief Number of march steps, clamped to [4, 64]. Unconnected uses 24.
		MaterialPin m_steps = { this, "Steps" };

		/// @brief Reflected scene colour (float3). Black where the ray missed.
		MaterialPin m_color = { this, "Color" };

		/// @brief Hit confidence in [0,1] (float1). Zero where the ray missed or left the screen.
		MaterialPin m_hitMask = { this, "Hit Mask" };

		Pin* m_inputPins[3] = { &m_normal, &m_maxDistance, &m_steps };
		Pin* m_outputPins[2] = { &m_color, &m_hitMask };
	};
```

In `src/mmo_edit/editors/material_editor/material_node.cpp`, beside the `SceneDepthNode` definitions around line 2351:

```cpp
	const uint32 ScreenSpaceReflectionNode::Color = ImColor(0.0f, 0.55f, 0.88f, 0.25f);

	ExpressionIndex ScreenSpaceReflectionNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
		const ExpressionIndex normal = m_normal.IsLinked()
			? m_normal.GetLink()->GetNode()->Compile(compiler, m_normal.GetLink()) : IndexNone;
		const ExpressionIndex maxDistance = m_maxDistance.IsLinked()
			? m_maxDistance.GetLink()->GetNode()->Compile(compiler, m_maxDistance.GetLink()) : IndexNone;
		const ExpressionIndex steps = m_steps.IsLinked()
			? m_steps.GetLink()->GetNode()->Compile(compiler, m_steps.GetLink()) : IndexNone;

		const ExpressionIndex reflection = compiler.AddScreenSpaceReflection(normal, maxDistance, steps);
		if (reflection == IndexNone)
		{
			return IndexNone;
		}

		// One march, two outputs: rgb is the colour, a is the hit mask. Masking here rather than
		// calling AddScreenSpaceReflection twice keeps the cost at a single ray march even when
		// the graph consumes both pins.
		if (outputPin == &m_hitMask)
		{
			return compiler.AddMask(reflection, false, false, false, true);
		}

		return compiler.AddMask(reflection, true, true, true, false);
	}
```

Check the actual signature of `AddMask` before writing this — grep `AddMask` in `material_compiler.h` and match its parameter order and count.

- [ ] **Step 6: Register the node**

In `src/mmo_edit/editors/material_editor/node_editor/node_registry.cpp`, add to the list around line 61, beside `SceneDepthNode::GetStaticTypeInfo()`:

```cpp
			ScreenSpaceReflectionNode::GetStaticTypeInfo(),
```

- [ ] **Step 7: Build everything**

```bash
cmake --build build --config Debug -t mmo_edit
```

Expected: success.

- [ ] **Step 8: Verify the node in the editor**

Launch `mmo_edit`, open any material, and confirm "Screen Space Reflection" appears in the node palette with three inputs and two outputs. Drop one into a scratch material, wire `Color` to Emissive, save, and confirm shaders compile without errors in the log.

- [ ] **Step 9: Confirm no existing material regressed**

The helper is gated behind `m_needsScreenSpaceReflection`, so materials that do not use the node must generate byte-identical shaders. Verify by rebuilding one unrelated material in the editor and confirming its shader chunk sizes are unchanged:

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py inspect data/client/Models/Grass.hmat
```

Compare the vertex/pixel shader counts against the same command run before this task.

- [ ] **Step 10: Commit**

```bash
git add src/shared/graphics/material_compiler.h src/shared/graphics_d3d11 src/shared/graphics_metal src/mmo_edit/editors/material_editor
git commit -m "feat(graphics): ScreenSpaceReflection material node

Ray-marches the opaque scene's linear depth (already bound at t31 for the
forward pass) and samples the scene colour copy at t30, inside the water
pixel shader. No new render pass: water is drawn during the forward pass,
so a pre-pass could not know where the water is without a depth prepass.

Returns a hit mask rather than a fallback colour. At grazing angles - most
of the time for an ocean - the reflected ray leaves the screen immediately,
so the miss path is the common path and the graph supplies the sky itself.

Declared pure virtual on MaterialCompiler so the Metal backend cannot be
silently forgotten. Helper emission is gated, so materials not using the
node generate identical shaders.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Water_Ocean material

**This task has a mandatory manual step.** `material_tool.py` rewrites only the `GRPH` chunk and cannot fabricate shader bytecode, so the material must be opened and saved in `mmo_edit` before it renders. That step cannot be automated from an agent session.

**Files:**
- Create: `data/client/Worlds/Water_Ocean.hmat` (copy of `Water_Base.hmat`, extended)
- Create: `data/client/Textures/WaterNoise_01.htex`
- Scratch: graph JSON under the session scratchpad

**Interfaces:**
- Consumes: `ScreenSpaceReflectionNode` (Task 8), `SunDirection` / `SunColor` globals (Task 6), vertex colour alpha face tag (Task 5).
- Produces: `Worlds/Water_Ocean.hmat`, referenced by the Ocean water profile from Task 7.

- [ ] **Step 1: Generate the whitecap noise texture**

Write a tiling value-noise PNG and import it. Use the existing offline texture path — check `tools/particle_gen` and `tools/sfx_gen` for the established pattern of generating an asset and importing it with the editor's importer. Requirements: 512x512, seamless tiling, single channel replicated to RGB, mid-grey mean.

Import as `data/client/Textures/WaterNoise_01.htex`. Verify:

```bash
python .claude/skills/mmo-material-editor/scripts/htex_tool.py inspect data/client/Textures/WaterNoise_01.htex --json
python .claude/skills/mmo-material-editor/scripts/htex_tool.py preview data/client/Textures/WaterNoise_01.htex --output "$SCRATCH/waternoise.png"
```

Look at the preview and confirm it tiles without a visible seam.

- [ ] **Step 2: Copy the base material**

```bash
cd data/client && cp Worlds/Water_Base.hmat Worlds/Water_Ocean.hmat && cd ../..
python .claude/skills/mmo-material-editor/scripts/material_tool.py inspect data/client/Worlds/Water_Ocean.hmat
```

Expected: `graph: 72 nodes, root=1`, type Translucent, two-sided, depth-write off.

- [ ] **Step 3: Open and save in mmo_edit to rename**

Open `Worlds/Water_Ocean.hmat` in `mmo_edit` and save it. This regenerates the internal material name (which still reads `Worlds/Water_Base.hmat` from the copy) and recompiles shaders. Confirm:

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py inspect data/client/Worlds/Water_Ocean.hmat
```

Expected: `name: Worlds/Water_Ocean.hmat`.

- [ ] **Step 4: Regenerate the node catalog**

The catalog is generated from the live C++ header, so it only contains `ScreenSpaceReflectionNode` after Task 8 is built.

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py node-catalog --output "$SCRATCH/node-catalog.json"
grep -c "ScreenSpaceReflection" "$SCRATCH/node-catalog.json"
```

Expected: at least 1. If 0, Task 8 is incomplete — stop and fix it rather than working around it.

- [ ] **Step 5: Export the graph and make the seven changes**

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py export-json data/client/Worlds/Water_Ocean.hmat --output "$SCRATCH/water_ocean.json"
```

Edit `$SCRATCH/water_ocean.json`, preserving every existing node and pin id and allocating new ids from `graph.next_id` upward. The seven changes, in the order they should be made and tested:

1. **Screen-space reflection.** Add a `ScreenSpaceReflectionNode`. Wire its `Normal` input from the same expression that feeds the root `Normal` pin (the existing blended panned normals). `Lerp` the existing sky-colour reflection result with the SSR `Color` using the SSR `Hit Mask` as alpha — sky when the mask is 0, SSR when it is 1.
2. **Sun glint.** Add two `GlobalVectorParameterNode`s named `SunDirection` and `SunColor`. Compute a Blinn-Phong style lobe from the half-vector between `SunDirection.xyz` and the camera vector against the surface normal, raise it with a `PowerNode` (exponent from a new `GlintSharpness` scalar parameter, default 64), multiply by `SunColor.rgb * SunColor.a` and a new `GlintIntensity` scalar (default 2.0, deliberately conservative — there is no tonemapping stage, so the R16G16B16A16 target clips hard above 1.0).
3. **Chromatic extinction.** The existing shallow-to-deep `Lerp` keys off the depth column. Multiply its output by `exp(-thickness * absorptionPerChannel)` using a new `AbsorptionRGB` vector parameter (default `(0.45, 0.15, 0.09, 0)` — red extinguishes fastest).
4. **Shore swash.** Multiply the existing foam mask by a swash envelope: `saturate(sin(Time * SwashSpeed) * 0.5 + 0.5)` offset by the depth thickness so the band advances up the beach. New scalars `SwashSpeed` (default 0.6) and `SwashWidth` (default 2.5).
5. **Whitecaps.** Add a `TextureParameterNode` named `Noise` bound to `Textures/WaterNoise_01.htex`, panned slowly. `SmoothStep` it against a new `WhitecapThreshold` scalar (default 0.72) and add the result into the foam path.
6. **Underside.** Add a `VertexColorNode`. `If` its alpha is below 0.5, use a darker silvery colour (new `UndersideColor` vector parameter, default `(0.10, 0.18, 0.22, 1)`) and invert the Fresnel term; otherwise the normal path.
7. **Emissive routing.** Move the refraction, SSR and sky contributions off `Base Color` and onto the currently-unconnected `Emissive Color`. Leave foam and the water body colour on `Base Color` so they catch the sun.

Validate after every single change — not once at the end:

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py validate-json "$SCRATCH/water_ocean.json"
```

- [ ] **Step 6: Apply the graph**

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py apply-json "$SCRATCH/water_ocean.json" --output data/client/Worlds/Water_Ocean.hmat --overwrite --allow-stale-shaders
python .claude/skills/mmo-material-editor/scripts/material_tool.py inspect data/client/Worlds/Water_Ocean.hmat
```

Expected: a higher node count and the new scalar/vector/texture parameter names listed.

- [ ] **Step 7: HAND OFF — recompile in mmo_edit**

**Stop here and ask the user to open `Worlds/Water_Ocean.hmat` in `mmo_edit` and save it.** The shader chunks on disk are now stale relative to the graph; until that save happens the material renders with the old shaders and nothing in this task can be verified. Do not attempt to verify the look before this step completes.

- [ ] **Step 8: Verify the material compiled**

```bash
python .claude/skills/mmo-material-editor/scripts/material_tool.py inspect data/client/Worlds/Water_Ocean.hmat
```

Expected: `vertex shaders: 6`, `pixel shaders: 4` (matching `Water_Base.hmat`), and the new parameters present. Check the editor log for shader compilation errors.

- [ ] **Step 9: Commit**

```bash
cd data/client
git add Worlds/Water_Ocean.hmat Textures/WaterNoise_01.htex
git commit -m "data: Water_Ocean material with SSR, glint, swash foam and whitecaps

Extends Water_Base rather than rebuilding: the base graph already carried
depth fade, refraction, Fresnel, sky reflection and panned normals with the
correct root flags. Adds screen-space reflection blended over the sky
fallback by its hit mask, a sun glint from the new SunDirection/SunColor
globals, Beer-Lambert chromatic extinction, a shore swash envelope,
whitecaps, and an underside look keyed off the vertex colour face tag.
View-dependent optics moved to the previously unconnected Emissive pin so
already-lit scene colour is not lit twice.

Glint intensity is deliberately conservative: there is no tonemapping stage,
so the R16G16B16A16 target clips hard above 1.0.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
cd ../..
git add data/client
git commit -m "data: bump client submodule for the Water_Ocean material

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: Editor profile awareness and a test coastline

**Files:**
- Modify: `src/mmo_edit/editors/world_editor/edit_modes/water_edit_mode.h` / `.cpp`
- Data: a painted test bay in an existing world

**Interfaces:**
- Consumes: `Project::waterProfiles` (Task 7), `Page::SetWaterMaterialResolver` (Task 5).
- Produces: nothing consumed by later tasks. Phase 2 reuses the painted coastline.

- [ ] **Step 1: Install the resolver**

Wherever the client and the editor build their terrain (search for `SetWaterVisible` or the `Terrain` construction site in both `world_state.cpp` and the world editor), install the profile resolver once:

```cpp
	terrain::Page::SetWaterMaterialResolver([](const terrain::WaterType type) -> String
	{
		const auto* profile = project.waterProfiles.getById(static_cast<uint32>(type));
		if (!profile || !profile->has_surface_material())
		{
			return String();
		}

		return profile->surface_material();
	});
```

Match the actual accessor name on `TemplateManager` — grep how `surfaceTypes` is queried elsewhere and mirror it exactly.

- [ ] **Step 2: Show the resolved material in the water edit mode UI**

In `water_edit_mode.cpp`, next to the existing `m_materialName` text field, display the material the currently selected `m_waterType` resolves to, and label the text field as an override:

```cpp
		// Resolved-from-profile material, shown read-only so the author can see what will be
		// used before painting. The text field below overrides it per page.
		const String resolved = ResolveProfileMaterial(m_waterType);
		ImGui::TextDisabled("Profile material: %s", resolved.empty() ? "<none>" : resolved.c_str());
		ImGui::InputText("Material override", m_materialName, sizeof(m_materialName));
```

- [ ] **Step 3: Build**

```bash
cmake --build build --config Debug -t mmo_edit
```

Expected: success.

- [ ] **Step 4: Paint a test bay**

Open a world in `mmo_edit`. Sculpt or find a shoreline and paint ocean water across it so that the result has, in one view:

- a beach shelving gradually from dry sand to about 1m depth (shore foam and the shallow colour),
- a mid zone around 5m (the depth tint doing visible work),
- deep water beyond 20m (the deep colour and reflections),
- a cliff or tall geometry adjacent to the water (something for SSR to actually reflect).

Set the water type to Ocean and leave the per-page material override empty so the profile resolves.

- [ ] **Step 5: Visual verification in the editor**

In the world editor viewport, confirm each of the seven material changes individually:

| Check | What you should see |
|---|---|
| Normal mapping | Ripples respond to the sun. Before Task 1 this was flat/garbage — this is the headline fix. |
| Depth tint | A visible gradient from beach to deep water, reddening lost first. |
| Refraction | The seafloor visible and wobbling through shallow water, **not** bleeding at the waterline. |
| SSR | The cliff reflected in the water near it; the reflection distorted by the ripples. |
| Sky fallback | Looking out to sea at a grazing angle gives a smooth sky gradient, never black. |
| Shore foam | A white band at the waterline that advances and retreats rather than sitting still. |
| Whitecaps | Sparse white flecks in open water. |
| Underside | Fly the camera below the surface: it reads darker and silvery, not identical to the top. |

Take screenshots of each and attach them to the task report.

- [ ] **Step 6: Verify in the real client**

Launch the client with auto-login, walk to the bay, and confirm the same eight checks. The editor viewport and the client use the same `DeferredRenderer`, but only the client has the full streaming terrain, so this catches page-boundary seams in the water UVs.

- [ ] **Step 7: Run the gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: green. This is the first point where the whole of Phase 1 is exercised together.

- [ ] **Step 8: Commit**

```bash
git add src/mmo_edit/editors/world_editor/edit_modes/water_edit_mode.h src/mmo_edit/editors/world_editor/edit_modes/water_edit_mode.cpp
git commit -m "feat(editor): resolve water material from the profile table

The water edit mode shows which material the selected liquid type resolves
to, and relabels the per-page material field as the override it has always
been.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
cd data/client && git add -A && git commit -m "data: test bay coastline for water development

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>" && cd ../..
git add data/client && git commit -m "data: bump client submodule for the test bay

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Phase 1 done when

- `ctest -C Debug` is green, including the new `[water_lookup]`, `[water_mesh]`, `[water_profiles]` and `[manual_render_object]` cases.
- `tools/gate/verify.ps1` is green.
- The eight visual checks in Task 10 Step 5 pass in both the editor and the real client.
- No `ProtocolVersion` was bumped and `python tools/protocol_version_check.py` reports no change.

Phase 2 (underwater post-process, `WaterVolumeSystem`, audio low-pass) is planned separately in `2026-09-12-ocean-water-phase2-underwater.md`.
