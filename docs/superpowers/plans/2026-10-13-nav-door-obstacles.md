# Nav Door Obstacles + Shared Static Map Data Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Closed doors and barriers block server-side pathfinding without any runtime nav rebuild, and nav mesh, static collision and water data are loaded once per map and shared by all instances.

**Architecture:** The nav builder bakes every Door spawn's footprint into the Recast mesh as polys carrying a dedicated `Door` flag. Each world instance owns a small `nav::InstanceQueryFilter` that rejects the door polys of its closed doors. A per-map `MapStaticData` object holds the nav mesh, page streamer, static collision and water map. A weak-pointer registry in `WorldInstanceManager` hands it out, so every instance of a map shares one copy. Only door collision (LOS) and the query filter stay per instance.

**Tech Stack:** C++17, Recast/Detour (`deps/recastnavigation`), Catch2, protobuf (`proto_data`), CMake, Lua E2E harness.

**Spec:** `docs/superpowers/specs/2026-10-13-nav-door-obstacles-design.md`

## Global Constraints

- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `snake_case` files, `#pragma once`, Doxygen on public members.
- Every new source file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions. Use `ASSERT`, `VERIFY`, `DLOG`, `WLOG`, `ELOG` from `base/macros.h` / `log/default_log_levels.h`.
- Door poly flag value: `32` (`1 << 5`). It is defined once as `nav::DoorPolyFlag` and mirrored by `poly_flags::Door` in `nav_build/common.h` with a `static_assert`.
- Detour must be compiled with `DT_VIRTUAL_QUERYFILTER` as a PUBLIC compile definition (via `RECASTNAVIGATION_DT_VIRTUAL_QUERYFILTER`), on every platform.
- Door volume: XZ oriented box from the transformed COLL bounds, inflated by `0.5` (agent radius) on each side. Y range is mesh world Y range ± `0.5` (walkableClimb).
- Door transform: `MakeTransform(position, Vector3(scale), rotation * Quaternion(Degree(90), Vector3::UnitY))`. One shared helper is used by builder and server.
- No protocol change, no DB migration. The nav file `FileVersion` stays `'0001'`.
- The world server is single-threaded. Shared `dtNavMesh` / `dtNavMeshQuery` are touched only on the world thread, and the streamer's reader thread must not log.
- Servers must keep building headless on Linux (gcc-13). Nothing in `game_server` may depend on graphics.

## Review Focus

1. **A door spawned before its nav pages are streamed in.** Spawning happens in the `WorldInstance` constructor while pages are still streaming. Expected: door registration loads the pages under the door first and still finds its polys. Pinned by the Task 6 test "door registered before streaming completes".
2. **Two instances of the same map with different door states.** Expected: closing a door in instance A never blocks paths in instance B. Pinned by the Task 6 test "door state is per instance".
3. **A door whose poly is shared by two overlapping door volumes (double doors), with one opened.** Expected: the poly stays blocked until both are open. Pinned by the Task 1 refcount test.
4. **The last instance of a map is destroyed and a new one is created (dungeon reset).** Expected: the static data is freed, then reloaded cleanly, and the new instance's paths work. Pinned by the Task 5 test "registry reloads after the last instance is gone".
5. **The target stands right next to the barrier on the far side.** Expected: the chaser gets a partial path ending on its own side. It must not get a same-poly shortcut through the door. Pinned by the Task 1 test "partial path stops in front of a blocked door".

---

## File Structure

| File | Responsibility |
|---|---|
| `deps/CMakeLists.txt` | Enables `RECASTNAVIGATION_DT_VIRTUAL_QUERYFILTER` before `add_subdirectory(recastnavigation)` |
| `src/shared/nav_mesh/instance_query_filter.h/.cpp` (new) | `nav::DoorPolyFlag`, `nav::InstanceQueryFilter` (refcounted blocked poly refs) |
| `src/shared/nav_mesh/door_volume.h/.cpp` (new) | `nav::DoorVolume`, `MakeDoorTransform`, `ComputeDoorVolume` — geometry shared by builder and server |
| `src/shared/nav_mesh/map.h/.cpp` | Optional `const dtQueryFilter*` on queries, plus `FindDoorPolys(const DoorVolume&)` |
| `src/tests/nav_mesh_tests/synthetic_nav_map.h` (new) | Test helper extracted from `test_page_streamer.cpp`: synthetic nav map on disk, flat and grid tiles |
| `src/tests/nav_mesh_tests/test_instance_query_filter.cpp` (new) | Filter and path tests on a grid tile with a door strip |
| `src/tests/nav_mesh_tests/test_door_volume.cpp` (new) | Door volume geometry tests |
| `src/shared/nav_build/common.h` | `poly_flags::Door` |
| `src/shared/nav_build/door_marking.h/.cpp` (new) | `MarkDoorAreas(rcContext&, rcCompactHeightfield&, doors)` |
| `src/shared/nav_build/mesh_builder.h/.cpp` | `SetDoorVolumes`, call `MarkDoorAreas` per tile |
| `src/tests/nav_build_tests/door_marking_tests.cpp` (new) | Recast pipeline test: door strip survives into the poly mesh |
| `src/nav_builder/main.cpp`, `CMakeLists.txt` | `-p,project` option; loads door spawns into volumes |
| `src/shared/game_server/world/server_collision_map.h/.cpp` | Shared static part (`shared_ptr<const ServerCollisionMap>`), `AddStaticInstance`, `GetDynamicInstanceLocalBounds` |
| `src/shared/game_server/world/map_static_data.h/.cpp` (new) | `MapStaticData` (nav map + streamer + static collision + water) and `MapStaticDataRegistry` |
| `src/shared/game_server/world/world_instance.h/.cpp` | `NavMapData` on top of `MapStaticData`, with door obstacle API; `RegisterDoorCollision` blocks nav |
| `src/shared/game_server/world/world_instance_manager.h/.cpp` | Owns the registry and updates it once per tick |
| `src/tests/game_server_tests/map_static_data_test.cpp` (new) | Sharing, freeing, per-instance door state |
| `e2e/scenarios/hollow_choir_barrier_blocks_paths.lua` (new) | Repro of the original bug |
| `docs/threading.md`, `CLAUDE.md`, `docs/hollow_choir_bosses.md` | Docs |

---

### Task 1: Virtual Detour filter, `InstanceQueryFilter`, filter-aware `nav::Map` queries

**Files:**
- Modify: `deps/CMakeLists.txt:24-27`
- Create: `src/shared/nav_mesh/instance_query_filter.h`, `src/shared/nav_mesh/instance_query_filter.cpp`
- Modify: `src/shared/nav_mesh/map.h`, `src/shared/nav_mesh/map.cpp` (FindPath ~388-529, FindRandomPointAroundCircle ~543, LineOfSight/LineOfSightEx ~614-660)
- Create: `src/tests/nav_mesh_tests/synthetic_nav_map.h`
- Modify: `src/tests/nav_mesh_tests/test_page_streamer.cpp` (delete the moved helpers, include the header)
- Create: `src/tests/nav_mesh_tests/test_instance_query_filter.cpp`

**Interfaces:**
- Produces:
  - `constexpr uint16 nav::DoorPolyFlag = 1 << 5;`
  - `class nav::InstanceQueryFilter final : public dtQueryFilter` with `void Block(const std::vector<dtPolyRef>&)`, `void Unblock(const std::vector<dtPolyRef>&)`, `bool IsBlocked(dtPolyRef) const`, `size_t GetBlockedCount() const`
  - `bool nav::Map::FindPath(const Vector3&, const Vector3&, std::vector<Vector3>&, bool allowPartial = false, const dtQueryFilter* filter = nullptr) const`
  - `bool nav::Map::FindRandomPointAroundCircle(const Vector3&, float, Vector3&, const dtQueryFilter* filter = nullptr) const`
  - `bool nav::Map::LineOfSight(const Vector3&, const Vector3&, const dtQueryFilter* filter = nullptr) const`
  - `bool nav::Map::LineOfSightEx(const Vector3&, const Vector3&, Vector3&, const dtQueryFilter* filter = nullptr) const`
  - Test helper `synthetic_nav_map.h`: `test_nav::BuildFlatTile(int32, int32)`, `test_nav::BuildGridTile(int32 tileX, int32 tileY, int32 cols, int32 rows, const std::function<uint16(int32 col, int32 row)>& flags, bool portalBorders)`, `test_nav::SyntheticNavMap(pages, missingFiles = {}, tileBuilder = {})`, `test_nav::TileOrigin(int32, int32)`, constants `TileSize`, `PageSize`, `MapOrigin`, `TilesPerPage`

- [ ] **Step 1: Initialize submodules and configure the build in the worktree**

The worktree's submodules are empty.

Run:
```bash
git submodule update --init --recursive
cmake -S . -B build -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```
Expected: configure succeeds. The `data/editor`, `data/client` and `deps/recastnavigation` folders are populated.

- [ ] **Step 2: Enable the virtual query filter in Detour**

In `deps/CMakeLists.txt`, add the define between the existing `RECASTNAVIGATION_EXAMPLES` line and `add_subdirectory(recastnavigation)`:

```cmake
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")
# nav::InstanceQueryFilter overrides dtQueryFilter::passFilter to block the door polys of closed
# doors per world instance. Detour exports the define PUBLIC, so every consumer sees the same layout.
set(RECASTNAVIGATION_DT_VIRTUAL_QUERYFILTER ON CACHE BOOL "" FORCE)
add_subdirectory(recastnavigation)
```

- [ ] **Step 3: Extract the synthetic nav map helper**

Create `src/tests/nav_mesh_tests/synthetic_nav_map.h`. Move `BuildFlatTile`, `WritePod`, `SyntheticNavMap`, the file constants and `PageCenter` out of `test_page_streamer.cpp` into `namespace test_nav`. Keep the code identical except for the additions below. In `test_page_streamer.cpp`, replace the moved code with `#include "synthetic_nav_map.h"` plus `using namespace test_nav;`. Keep `StreamToCompletion` and `RowOfPages` in the .cpp.

Additions:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "nav_mesh/map.h"
#include "terrain/constants.h"

#include "DetourAlloc.h"
#include "DetourNavMeshBuilder.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace test_nav
{
	using namespace mmo;

	// (moved constants: FileSignature, FileVersion, FilePage, FileMap, TilesPerPage, TileSize, PageSize, MapOrigin)

	/// World-space XZ origin (min corner) of a global tile.
	inline Vector3 TileOrigin(const int32 tileX, const int32 tileY)
	{
		return { MapOrigin + static_cast<float>(tileX) * TileSize, 0.f, MapOrigin + static_cast<float>(tileY) * TileSize };
	}

	/// (moved, now `inline`) BuildFlatTile(tileX, tileY)

	/// Detour data for one tile split into cols x rows flat quads at y = 0. Quad (col, row) spans
	/// x in [col, col + 1) * TileSize / cols and z in [row, row + 1) * TileSize / rows, relative to
	/// the tile origin. Poly index is row * cols + col. Inner edges link neighbouring quads. Border
	/// edges are portals when portalBorders is set, otherwise walls.
	inline std::vector<uint8> BuildGridTile(const int32 tileX, const int32 tileY, const int32 cols, const int32 rows,
		const std::function<uint16(int32, int32)>& flags, const bool portalBorders)
	{
		constexpr int nvp = 6;
		constexpr unsigned short nullIndex = 0xffff;
		constexpr unsigned short voxels = 60;
		const unsigned short stepX = static_cast<unsigned short>(voxels / cols);
		const unsigned short stepZ = static_cast<unsigned short>(voxels / rows);

		std::vector<unsigned short> verts;
		for (int32 j = 0; j <= rows; ++j)
		{
			for (int32 i = 0; i <= cols; ++i)
			{
				verts.push_back(static_cast<unsigned short>(i * stepX));
				verts.push_back(0);
				verts.push_back(static_cast<unsigned short>(j * stepZ));
			}
		}

		const auto vertexIndex = [cols](const int32 i, const int32 j)
		{
			return static_cast<unsigned short>(j * (cols + 1) + i);
		};

		const auto border = [portalBorders](const unsigned short dir)
		{
			return portalBorders ? static_cast<unsigned short>(0x8000 | dir) : nullIndex;
		};

		std::vector<unsigned short> polys;
		std::vector<unsigned short> polyFlags;
		std::vector<unsigned char> polyAreas;
		for (int32 r = 0; r < rows; ++r)
		{
			for (int32 c = 0; c < cols; ++c)
			{
				// Same winding as BuildFlatTile: (x0,z0) (x0,z1) (x1,z1) (x1,z0).
				// Edge 0 = x-, 1 = z+, 2 = x+, 3 = z-.
				polys.push_back(vertexIndex(c, r));
				polys.push_back(vertexIndex(c, r + 1));
				polys.push_back(vertexIndex(c + 1, r + 1));
				polys.push_back(vertexIndex(c + 1, r));
				polys.push_back(nullIndex);
				polys.push_back(nullIndex);

				polys.push_back(c > 0 ? static_cast<unsigned short>(r * cols + c - 1) : border(0));
				polys.push_back(r < rows - 1 ? static_cast<unsigned short>((r + 1) * cols + c) : border(1));
				polys.push_back(c < cols - 1 ? static_cast<unsigned short>(r * cols + c + 1) : border(2));
				polys.push_back(r > 0 ? static_cast<unsigned short>((r - 1) * cols + c) : border(3));
				polys.push_back(0);
				polys.push_back(0);

				polyFlags.push_back(flags(c, r));
				polyAreas.push_back(0);
			}
		}

		dtNavMeshCreateParams params;
		std::memset(&params, 0, sizeof(params));
		params.verts = verts.data();
		params.vertCount = static_cast<int>(verts.size() / 3);
		params.polys = polys.data();
		params.polyFlags = polyFlags.data();
		params.polyAreas = polyAreas.data();
		params.polyCount = cols * rows;
		params.nvp = nvp;
		params.tileX = tileX;
		params.tileY = tileY;
		const Vector3 origin = TileOrigin(tileX, tileY);
		params.bmin[0] = origin.x;
		params.bmin[1] = 0.f;
		params.bmin[2] = origin.z;
		params.bmax[0] = origin.x + TileSize;
		params.bmax[1] = 1.f;
		params.bmax[2] = origin.z + TileSize;
		params.cs = TileSize / voxels;
		params.ch = 0.1f;
		params.walkableHeight = 2.f;
		params.walkableRadius = 0.5f;
		params.walkableClimb = 1.f;
		params.buildBvTree = true;

		unsigned char* data = nullptr;
		int dataSize = 0;
		REQUIRE(dtCreateNavMeshData(&params, &data, &dataSize));

		std::vector<uint8> result(data, data + dataSize);
		dtFree(data);
		return result;
	}

	/// Builds the Detour data of one global tile, or nothing to leave the tile out of its page.
	using TileBuilder = std::function<std::optional<std::vector<uint8>>(int32 tileX, int32 tileY)>;
}
```

Change `SyntheticNavMap`:
- The constructor gets a third parameter, `TileBuilder tileBuilder = {}`, stored in `m_tileBuilder`.
- `WritePage` first collects every tile (`m_tileBuilder ? m_tileBuilder(x, y) : BuildFlatTile(x, y)`) and skips those without a value.
- `header.tileCount` becomes the number of collected tiles. The header is written before the tiles.

- [ ] **Step 4: Run the existing nav tests to verify the extraction is behaviour-neutral**

Run:
```bash
cmake --build build --config Debug -t nav_mesh_tests
./bin/Debug/nav_mesh_tests
```
Expected: all existing cases PASS.

- [ ] **Step 5: Write the failing filter tests**

Create `src/tests/nav_mesh_tests/test_instance_query_filter.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "synthetic_nav_map.h"

#include "nav_mesh/instance_query_filter.h"
#include "nav_mesh/map.h"

#include "DetourNavMesh.h"

#include <cmath>

using namespace mmo;
using namespace test_nav;

namespace
{
	// Global tile (512, 512) is the first tile of page (32, 32) and starts at world (0, 0).
	constexpr int32 DoorTileX = 512;
	constexpr int32 DoorTileY = 512;

	/// One walled corridor of three quads along x: [floor | door | floor].
	std::optional<std::vector<uint8>> CorridorTile(const int32 x, const int32 y)
	{
		if (x != DoorTileX || y != DoorTileY)
		{
			return std::nullopt;
		}

		return BuildGridTile(x, y, 3, 1, [](const int32 col, int32)
		{
			return static_cast<uint16>(col == 1 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	}

	/// Two walled corridors of three quads each; only the lower one has a door in the middle.
	std::optional<std::vector<uint8>> TwoCorridorTile(const int32 x, const int32 y)
	{
		if (x != DoorTileX || y != DoorTileY)
		{
			return std::nullopt;
		}

		return BuildGridTile(x, y, 3, 2, [](const int32 col, const int32 row)
		{
			return static_cast<uint16>(col == 1 && row == 0 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	}

	/// Center of quad (col, row) of a cols x rows grid tile.
	Vector3 CellCenter(const int32 col, const int32 row, const int32 cols, const int32 rows)
	{
		const Vector3 origin = TileOrigin(DoorTileX, DoorTileY);
		return { origin.x + (static_cast<float>(col) + 0.5f) * TileSize / static_cast<float>(cols), 0.f,
			origin.z + (static_cast<float>(row) + 0.5f) * TileSize / static_cast<float>(rows) };
	}

	/// The poly ref of the door quad, found by position.
	dtPolyRef DoorRef(const nav::Map& map, const Vector3& doorCenter)
	{
		const float center[3] = { doorCenter.x, doorCenter.y, doorCenter.z };
		const float extents[3] = { 0.1f, 1.f, 0.1f };
		dtQueryFilter filter;
		dtPolyRef ref = 0;
		REQUIRE(dtStatusSucceed(map.GetNavMeshQuery().findNearestPoly(center, extents, &filter, &ref, nullptr)));
		REQUIRE(ref != 0);
		return ref;
	}

	const std::vector<std::pair<int32, int32>> DoorPage = { { 32, 32 } };
}

TEST_CASE("InstanceQueryFilter blocks a door corridor while the door ref is blocked", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, CorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 1);
	const Vector3 door = CellCenter(1, 0, 3, 1);
	const Vector3 east = CellCenter(2, 0, 3, 1);

	nav::InstanceQueryFilter filter;
	std::vector<Vector3> path;
	REQUIRE(map.FindPath(west, east, path, false, &filter));
	CHECK(path.back().GetDistanceTo(east) < 0.5f);

	filter.Block({ DoorRef(map, door) });
	path.clear();
	CHECK_FALSE(map.FindPath(west, east, path, false, &filter));

	filter.Unblock({ DoorRef(map, door) });
	path.clear();
	REQUIRE(map.FindPath(west, east, path, false, &filter));
	CHECK(path.back().GetDistanceTo(east) < 0.5f);
}

TEST_CASE("Partial path stops in front of a blocked door", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, CorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 1);
	const Vector3 door = CellCenter(1, 0, 3, 1);
	const Vector3 east = CellCenter(2, 0, 3, 1);
	const float doorWestEdge = door.x - TileSize / 6.f;

	nav::InstanceQueryFilter filter;
	filter.Block({ DoorRef(map, door) });

	std::vector<Vector3> path;
	REQUIRE(map.FindPath(west, east, path, true, &filter));
	for (const Vector3& point : path)
	{
		CHECK(point.x <= doorWestEdge + 0.01f);
	}

	// A target standing just past the door, and one 1 m into the door poly, must not be reached
	// either: neither may become a same-poly shortcut. (The door quad's center is more than the
	// 5 m search extent away from any open poly, so only a point near its edge resolves at all.)
	const Vector3 justPast(doorWestEdge + TileSize / 3.f + 0.2f, 0.f, door.z);
	path.clear();
	REQUIRE(map.FindPath(west, justPast, path, true, &filter));
	CHECK(path.back().x <= doorWestEdge + 0.01f);

	const Vector3 onDoor(doorWestEdge + 1.f, 0.f, door.z);
	path.clear();
	REQUIRE(map.FindPath(west, onDoor, path, true, &filter));
	CHECK(path.back().x <= doorWestEdge + 0.01f);
}

TEST_CASE("A blocked door is bypassed through another corridor", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, TwoCorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 2);
	const Vector3 door = CellCenter(1, 0, 3, 2);
	const Vector3 east = CellCenter(2, 0, 3, 2);

	nav::InstanceQueryFilter filter;
	filter.Block({ DoorRef(map, door) });

	std::vector<Vector3> path;
	REQUIRE(map.FindPath(west, east, path, false, &filter));
	CHECK(path.back().GetDistanceTo(east) < 0.5f);

	// Every waypoint must stay out of the door quad.
	const float halfW = TileSize / 6.f;
	const float halfH = TileSize / 4.f;
	for (const Vector3& point : path)
	{
		const bool insideDoor = std::abs(point.x - door.x) < halfW - 0.01f && std::abs(point.z - door.z) < halfH - 0.01f;
		CHECK_FALSE(insideDoor);
	}
}

TEST_CASE("InstanceQueryFilter reference counts blocks", "[nav_mesh][door]")
{
	nav::InstanceQueryFilter filter;
	const dtPolyRef ref = 12345;

	filter.Block({ ref });
	filter.Block({ ref });
	filter.Unblock({ ref });
	CHECK(filter.IsBlocked(ref));
	CHECK(filter.GetBlockedCount() == 1);

	filter.Unblock({ ref });
	CHECK_FALSE(filter.IsBlocked(ref));
	CHECK(filter.GetBlockedCount() == 0);

	// Unblocking a ref that is not blocked is a no-op.
	filter.Unblock({ ref });
	CHECK(filter.GetBlockedCount() == 0);
}

TEST_CASE("Random points never land on a blocked door poly", "[nav_mesh][door]")
{
	SyntheticNavMap files(DoorPage, {}, CorridorTile);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 west = CellCenter(0, 0, 3, 1);
	const Vector3 door = CellCenter(1, 0, 3, 1);
	const float doorWestEdge = door.x - TileSize / 6.f;

	nav::InstanceQueryFilter filter;
	filter.Block({ DoorRef(map, door) });

	for (int i = 0; i < 50; ++i)
	{
		Vector3 point;
		if (map.FindRandomPointAroundCircle(west, TileSize, point, &filter))
		{
			CHECK(point.x <= doorWestEdge + 0.01f);
		}
	}
}
```

- [ ] **Step 6: Run the tests to verify they fail**

Run: `cmake --build build --config Debug -t nav_mesh_tests`
Expected: compile error, because `nav_mesh/instance_query_filter.h` does not exist.

- [ ] **Step 7: Implement `InstanceQueryFilter`**

Create `src/shared/nav_mesh/instance_query_filter.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include "DetourNavMeshQuery.h"

#include <utility>
#include <vector>

namespace mmo::nav
{
	/// @brief Poly flag the nav builder sets on polys cut out under a door's footprint.
	/// @details Must match mmo::poly_flags::Door in nav_build/common.h.
	constexpr uint16 DoorPolyFlag = 1 << 5;

	/// @brief Query filter of one world instance: the default flag filter, plus the door polys of
	/// that instance's closed doors.
	/// @details The nav mesh is shared by every instance of a map, so door state cannot live in
	/// the mesh's poly flags. Blocks are reference counted, because overlapping door volumes can
	/// claim the same poly. Not thread safe: owned and used by the world thread.
	class InstanceQueryFilter final : public dtQueryFilter
	{
	public:
		/// @copydoc dtQueryFilter::passFilter
		bool passFilter(dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const override;

		/// @brief Blocks every given poly. A poly blocked n times needs n unblocks.
		void Block(const std::vector<dtPolyRef>& refs);

		/// @brief Releases one block of every given poly. Unknown refs are ignored.
		void Unblock(const std::vector<dtPolyRef>& refs);

		/// @brief Whether the poly is blocked at least once.
		[[nodiscard]] bool IsBlocked(dtPolyRef ref) const;

		/// @brief Number of distinct blocked polys.
		[[nodiscard]] size_t GetBlockedCount() const { return m_blocked.size(); }

	private:
		/// Blocked polys and their block count, sorted by ref.
		std::vector<std::pair<dtPolyRef, uint32>> m_blocked;
	};
}
```

Create `src/shared/nav_mesh/instance_query_filter.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "instance_query_filter.h"

#include "DetourNavMesh.h"

#include <algorithm>

namespace mmo::nav
{
	namespace
	{
		bool lessRef(const std::pair<dtPolyRef, uint32>& entry, const dtPolyRef ref)
		{
			return entry.first < ref;
		}
	}

	bool InstanceQueryFilter::passFilter(const dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const
	{
		if (!dtQueryFilter::passFilter(ref, tile, poly))
		{
			return false;
		}

		// Only door polys can be blocked, so all other polys skip the lookup.
		if ((poly->flags & DoorPolyFlag) == 0 || m_blocked.empty())
		{
			return true;
		}

		return !IsBlocked(ref);
	}

	void InstanceQueryFilter::Block(const std::vector<dtPolyRef>& refs)
	{
		for (const dtPolyRef ref : refs)
		{
			const auto it = std::lower_bound(m_blocked.begin(), m_blocked.end(), ref, lessRef);
			if (it != m_blocked.end() && it->first == ref)
			{
				++it->second;
			}
			else
			{
				m_blocked.insert(it, { ref, 1 });
			}
		}
	}

	void InstanceQueryFilter::Unblock(const std::vector<dtPolyRef>& refs)
	{
		for (const dtPolyRef ref : refs)
		{
			const auto it = std::lower_bound(m_blocked.begin(), m_blocked.end(), ref, lessRef);
			if (it == m_blocked.end() || it->first != ref)
			{
				continue;
			}

			if (--it->second == 0)
			{
				m_blocked.erase(it);
			}
		}
	}

	bool InstanceQueryFilter::IsBlocked(const dtPolyRef ref) const
	{
		const auto it = std::lower_bound(m_blocked.begin(), m_blocked.end(), ref, lessRef);
		return it != m_blocked.end() && it->first == ref;
	}
}
```

- [ ] **Step 8: Add the filter parameter to `nav::Map` queries**

In `map.h`, change the four declarations and add a Doxygen line to each: `/// @param filter Query filter to use; nullptr uses the map's default filter.`

```cpp
bool FindPath(const Vector3& start, const Vector3& end, std::vector<Vector3>& output, bool allowPartial = false, const dtQueryFilter* filter = nullptr) const;
bool LineOfSight(const Vector3& start, const Vector3& end, const dtQueryFilter* filter = nullptr) const;
bool LineOfSightEx(const Vector3& start, const Vector3& end, Vector3& hitPoint, const dtQueryFilter* filter = nullptr) const;
bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint, const dtQueryFilter* filter = nullptr) const;
```

In `map.cpp`, begin each of the four definitions (after the `m_valid` check) with:

```cpp
		const dtQueryFilter* queryFilter = filter ? filter : &m_queryFilter;
```

Then replace every `&m_queryFilter` inside these functions with `queryFilter`. In `FindPath`, the `sampleHeight` lambda captures it: `[this, queryFilter](const Vector3& point) -> Vector3`. `LineOfSight` forwards it: `return LineOfSightEx(start, end, unused, filter);`.

No CMake change is needed for the new files: `add_lib(nav_mesh)` globs the directory. Re-run `cmake -S . -B build` so the glob picks them up.

- [ ] **Step 9: Run the tests to verify they pass**

Run:
```bash
cmake -S . -B build
cmake --build build --config Debug -t nav_mesh_tests
./bin/Debug/nav_mesh_tests "[door]"
./bin/Debug/nav_mesh_tests
```
Expected: all PASS. The `[door]` run reports 5 test cases.

If "Partial path stops in front of a blocked door" fails on the `door` target case: `findNearestPoly` for the end must use the instance filter. Check that `FindPath` passes `queryFilter` to both `findNearestPoly` calls.

- [ ] **Step 10: Build everything that uses Detour to catch layout mismatches**

Run: `cmake --build build --config Debug -t all_tests world_server nav_builder`
Expected: builds without errors.

- [ ] **Step 11: Commit**

```bash
git add deps/CMakeLists.txt src/shared/nav_mesh src/tests/nav_mesh_tests
git commit -m "nav: per-instance query filter that blocks door polys"
```

---

### Task 2: Door volume geometry shared by builder and server

**Files:**
- Create: `src/shared/nav_mesh/door_volume.h`, `src/shared/nav_mesh/door_volume.cpp`
- Modify: `src/shared/nav_mesh/map.h/.cpp` (add `FindDoorPolys`)
- Create: `src/tests/nav_mesh_tests/test_door_volume.cpp`
- Modify: `src/tests/nav_mesh_tests/CMakeLists.txt` (add `math`)

**Interfaces:**
- Consumes: `nav::DoorPolyFlag` (Task 1), `test_nav::SyntheticNavMap`, `BuildGridTile`, `TileOrigin` (Task 1)
- Produces:
  - `struct nav::DoorVolume { Vector3 center; Vector3 axisX; Vector3 axisZ; float halfX; float halfZ; float minY; float maxY; bool ContainsXZ(const Vector3&) const; void GetWorldBounds(Vector3& min, Vector3& max) const; void GetCorners(float (&verts)[12]) const; }`
  - `constexpr float nav::DoorVolumeInflate = 0.5f;`
  - `constexpr float nav::DoorVolumeHeightPadding = 0.5f;`
  - `Matrix4 nav::MakeDoorTransform(const Vector3& position, const Quaternion& rotation, float scale)`
  - `nav::DoorVolume nav::ComputeDoorVolume(const AABB& localBounds, const Matrix4& transform)`
  - `std::vector<dtPolyRef> nav::Map::FindDoorPolys(const DoorVolume& volume) const`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/nav_mesh_tests/test_door_volume.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "synthetic_nav_map.h"

#include "nav_mesh/door_volume.h"
#include "nav_mesh/instance_query_filter.h"
#include "nav_mesh/map.h"
#include "math/aabb.h"
#include "math/degree.h"
#include "math/quaternion.h"

#include <cmath>

using namespace mmo;
using namespace test_nav;

namespace
{
	/// The magic barrier's collision: a 6 m wide, 7 m tall plane, zero thickness. In mesh space
	/// it spans z, so the +90 degree entity yaw turns it to span x.
	const AABB BarrierBounds(Vector3(0.f, 0.f, -3.f), Vector3(0.f, 7.f, 3.f));
}

TEST_CASE("MakeDoorTransform applies the +90 degree entity yaw", "[nav_mesh][door_volume]")
{
	const Matrix4 transform = nav::MakeDoorTransform(Vector3(10.f, 2.f, 20.f), Quaternion::Identity, 1.f);
	const Vector3 worldPoint = transform * Vector3(0.f, 0.f, 3.f);

	// +90 degrees around Y turns local Z onto world X (the sign depends on the engine's
	// quaternion handedness, which the server's LOS transform already relies on).
	CHECK(std::abs(worldPoint.x - 10.f) == Approx(3.f).margin(0.001f));
	CHECK(worldPoint.y == Approx(2.f).margin(0.001f));
	CHECK(worldPoint.z == Approx(20.f).margin(0.001f));
}

TEST_CASE("ComputeDoorVolume inflates a zero-thickness barrier", "[nav_mesh][door_volume]")
{
	const Matrix4 transform = nav::MakeDoorTransform(Vector3(10.f, 2.f, 20.f), Quaternion::Identity, 1.f);
	const nav::DoorVolume volume = nav::ComputeDoorVolume(BarrierBounds, transform);

	// 6 m wide along world x, 0 m thick along world z, both grown by the agent radius.
	Vector3 min, max;
	volume.GetWorldBounds(min, max);
	CHECK(max.x - min.x == Approx(6.f + 2.f * nav::DoorVolumeInflate).margin(0.01f));
	CHECK(max.z - min.z == Approx(2.f * nav::DoorVolumeInflate).margin(0.01f));
	CHECK(volume.minY == Approx(2.f - nav::DoorVolumeHeightPadding).margin(0.01f));
	CHECK(volume.maxY == Approx(9.f + nav::DoorVolumeHeightPadding).margin(0.01f));

	CHECK(volume.ContainsXZ(Vector3(10.f, 0.f, 20.f)));
	CHECK(volume.ContainsXZ(Vector3(13.4f, 0.f, 20.4f)));
	CHECK_FALSE(volume.ContainsXZ(Vector3(10.f, 0.f, 20.6f)));
	CHECK_FALSE(volume.ContainsXZ(Vector3(13.6f, 0.f, 20.f)));
}

TEST_CASE("ComputeDoorVolume follows the spawn yaw and scale", "[nav_mesh][door_volume]")
{
	// A further 90 degree spawn yaw turns the barrier back to span z. Scale 2 doubles it.
	const Quaternion yaw(Degree(90), Vector3::UnitY);
	const Matrix4 transform = nav::MakeDoorTransform(Vector3::Zero, yaw, 2.f);
	const nav::DoorVolume volume = nav::ComputeDoorVolume(BarrierBounds, transform);

	Vector3 min, max;
	volume.GetWorldBounds(min, max);
	CHECK(max.z - min.z == Approx(12.f + 2.f * nav::DoorVolumeInflate).margin(0.01f));
	CHECK(max.x - min.x == Approx(2.f * nav::DoorVolumeInflate).margin(0.01f));
}

TEST_CASE("FindDoorPolys returns only door polys inside the volume", "[nav_mesh][door_volume]")
{
	// Row of three quads: [floor | door | floor]. Both floor quads are also inside the query box.
	const auto tiles = [](const int32 x, const int32 y) -> std::optional<std::vector<uint8>>
	{
		if (x != 512 || y != 512)
		{
			return std::nullopt;
		}

		return BuildGridTile(x, y, 3, 1, [](const int32 col, int32)
		{
			return static_cast<uint16>(col == 1 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	};
	SyntheticNavMap files({ { 32, 32 } }, {}, tiles);
	nav::Map map(SyntheticNavMap::Name);
	REQUIRE(map.LoadPage(32, 32));

	const Vector3 origin = TileOrigin(512, 512);
	nav::DoorVolume volume;
	volume.center = origin + Vector3(TileSize * 0.5f, 0.f, TileSize * 0.5f);
	volume.axisX = Vector3::UnitX;
	volume.axisZ = Vector3::UnitZ;
	volume.halfX = TileSize * 0.5f;
	volume.halfZ = TileSize * 0.5f;
	volume.minY = -1.f;
	volume.maxY = 1.f;

	const std::vector<dtPolyRef> refs = map.FindDoorPolys(volume);
	REQUIRE(refs.size() == 1);

	unsigned short flags = 0;
	REQUIRE(dtStatusSucceed(map.GetNavMesh().getPolyFlags(refs[0], &flags)));
	CHECK((flags & nav::DoorPolyFlag) != 0);

	// A volume that does not reach the door quad's center finds nothing.
	volume.halfX = TileSize / 6.f - 0.5f;
	volume.center = origin + Vector3(TileSize / 6.f, 0.f, TileSize * 0.5f);
	CHECK(map.FindDoorPolys(volume).empty());
}
```

Change `src/tests/nav_mesh_tests/CMakeLists.txt` to:

```cmake
mmo_add_test(nav_mesh_tests
	base
	log
	math
	nav_mesh)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --config Debug -t nav_mesh_tests`
Expected: compile error, because `nav_mesh/door_volume.h` is not found.

- [ ] **Step 3: Implement the door volume**

Create `src/shared/nav_mesh/door_volume.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/aabb.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"

namespace mmo::nav
{
	/// @brief How far a door's footprint is grown on each side, in world units. Barriers are
	/// single planes with zero thickness; growing by the agent radius gives the nav builder a
	/// strip wide enough to survive voxelization.
	constexpr float DoorVolumeInflate = 0.5f;

	/// @brief How far the door's Y range is grown up and down (the walkable climb), so floor
	/// spans slightly below the door's base still count as under the door.
	constexpr float DoorVolumeHeightPadding = 0.5f;

	/// @brief A door's footprint: an oriented box in the XZ plane plus a Y range.
	struct DoorVolume
	{
		/// World-space center of the box (y is ignored, see minY/maxY).
		Vector3 center;
		/// Unit box axes in the XZ plane (y = 0), perpendicular to each other.
		Vector3 axisX { Vector3::UnitX };
		Vector3 axisZ { Vector3::UnitZ };
		/// Half extents along axisX and axisZ, inflation included.
		float halfX = 0.f;
		float halfZ = 0.f;
		/// World Y range, padding included.
		float minY = 0.f;
		float maxY = 0.f;

		/// @brief Whether the XZ projection of a point lies inside the box.
		[[nodiscard]] bool ContainsXZ(const Vector3& point) const;

		/// @brief Axis-aligned world bounds of the box, Y range included.
		void GetWorldBounds(Vector3& min, Vector3& max) const;

		/// @brief The four XZ corners as Recast vertices (x, y, z), counter-clockwise seen from above, y = minY.
		void GetCorners(float (&verts)[12]) const;
	};

	/// @brief Builds a door's local to world transform, matching the client's display entity:
	/// position, uniform scale, and the spawn rotation followed by a fixed +90 degree yaw.
	/// @param position Spawn position.
	/// @param rotation Spawn rotation. A zero quaternion (unset in data) is treated as identity.
	/// @param scale Uniform scale; values <= 0 are treated as 1.
	[[nodiscard]] Matrix4 MakeDoorTransform(const Vector3& position, const Quaternion& rotation, float scale);

	/// @brief Computes the inflated XZ footprint of a door from its collision bounds.
	/// @param localBounds Bounds of the door mesh's collision tree, in mesh space.
	/// @param transform Door transform, see MakeDoorTransform.
	[[nodiscard]] DoorVolume ComputeDoorVolume(const AABB& localBounds, const Matrix4& transform);
}
```

Create `src/shared/nav_mesh/door_volume.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "door_volume.h"

#include "math/degree.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mmo::nav
{
	bool DoorVolume::ContainsXZ(const Vector3& point) const
	{
		const Vector3 offset(point.x - center.x, 0.f, point.z - center.z);
		return std::abs(offset.Dot(axisX)) <= halfX && std::abs(offset.Dot(axisZ)) <= halfZ;
	}

	void DoorVolume::GetWorldBounds(Vector3& min, Vector3& max) const
	{
		const float extentX = std::abs(axisX.x) * halfX + std::abs(axisZ.x) * halfZ;
		const float extentZ = std::abs(axisX.z) * halfX + std::abs(axisZ.z) * halfZ;
		min = Vector3(center.x - extentX, minY, center.z - extentZ);
		max = Vector3(center.x + extentX, maxY, center.z + extentZ);
	}

	void DoorVolume::GetCorners(float (&verts)[12]) const
	{
		const Vector3 ax = axisX * halfX;
		const Vector3 az = axisZ * halfZ;
		const Vector3 corners[4] = { center - ax - az, center - ax + az, center + ax + az, center + ax - az };
		for (int i = 0; i < 4; ++i)
		{
			verts[i * 3 + 0] = corners[i].x;
			verts[i * 3 + 1] = minY;
			verts[i * 3 + 2] = corners[i].z;
		}
	}

	Matrix4 MakeDoorTransform(const Vector3& position, const Quaternion& rotation, float scale)
	{
		if (scale <= 0.f)
		{
			scale = 1.f;
		}

		Quaternion spawnRotation = rotation;
		if (spawnRotation.w == 0.f && spawnRotation.x == 0.f && spawnRotation.y == 0.f && spawnRotation.z == 0.f)
		{
			spawnRotation = Quaternion::Identity;
		}

		// The client hangs the display entity under a child node with a fixed +90 degree yaw
		// (GameObjectC::SetupSceneObjects); the server's LOS collision mirrors it.
		const Quaternion entityYawOffset(Degree(90), Vector3::UnitY);

		Matrix4 transform;
		transform.MakeTransform(position, Vector3(scale, scale, scale), spawnRotation * entityYawOffset);
		return transform;
	}

	DoorVolume ComputeDoorVolume(const AABB& localBounds, const Matrix4& transform)
	{
		const Vector3& lo = localBounds.min;
		const Vector3& hi = localBounds.max;
		const Vector3 localCorners[8] = {
			{ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z },
			{ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { lo.x, hi.y, hi.z }, { hi.x, hi.y, hi.z },
		};

		// Box axes: the door's local X axis projected onto the ground plane, and its perpendicular.
		// A door is upright, so its local X never points straight up. If it does, fall back to world axes.
		const Vector3 origin = transform * Vector3::Zero;
		Vector3 axisX = transform * Vector3::UnitX - origin;
		axisX.y = 0.f;
		if (axisX.GetSquaredLength() < 1.0e-8f)
		{
			axisX = Vector3::UnitX;
		}
		axisX.Normalize();
		const Vector3 axisZ(-axisX.z, 0.f, axisX.x);

		float minX = std::numeric_limits<float>::max(), maxX = -std::numeric_limits<float>::max();
		float minZ = minX, maxZ = maxX;
		float minY = minX, maxY = maxX;
		for (const Vector3& local : localCorners)
		{
			const Vector3 world = transform * local;
			const Vector3 flat(world.x, 0.f, world.z);
			minX = std::min(minX, flat.Dot(axisX));
			maxX = std::max(maxX, flat.Dot(axisX));
			minZ = std::min(minZ, flat.Dot(axisZ));
			maxZ = std::max(maxZ, flat.Dot(axisZ));
			minY = std::min(minY, world.y);
			maxY = std::max(maxY, world.y);
		}

		DoorVolume volume;
		volume.axisX = axisX;
		volume.axisZ = axisZ;
		volume.center = axisX * ((minX + maxX) * 0.5f) + axisZ * ((minZ + maxZ) * 0.5f);
		volume.halfX = (maxX - minX) * 0.5f + DoorVolumeInflate;
		volume.halfZ = (maxZ - minZ) * 0.5f + DoorVolumeInflate;
		volume.minY = minY - DoorVolumeHeightPadding;
		volume.maxY = maxY + DoorVolumeHeightPadding;
		return volume;
	}
}
```

Check before use: `AABB` exposes `min`/`max` members, and `Vector3` has `Dot`, `Normalize`, `GetSquaredLength`, `UnitX`, `UnitZ` and `Zero`. If a name differs in `src/shared/math/aabb.h` / `vector3.h`, use the existing one. Also check that `Quaternion::Identity` and `Degree` live in `math/quaternion.h` / `math/degree.h`, as used by `world_instance.cpp:841`.

- [ ] **Step 4: Implement `Map::FindDoorPolys`**

In `map.h`, add `#include "door_volume.h"` and declare in the public section:

```cpp
		/// @brief Finds the door polys (DoorPolyFlag) whose center lies inside a door volume.
		/// @details Only searches installed pages: the caller must make sure the pages under the
		/// volume are loaded. Returns an empty list on maps built without door data.
		[[nodiscard]] std::vector<dtPolyRef> FindDoorPolys(const DoorVolume& volume) const;
```

In `map.cpp` (add `#include "instance_query_filter.h"` and `#include "DetourNavMesh.h"`):

```cpp
	std::vector<dtPolyRef> Map::FindDoorPolys(const DoorVolume& volume) const
	{
		std::vector<dtPolyRef> result;
		if (!m_valid)
		{
			return result;
		}

		Vector3 min, max;
		volume.GetWorldBounds(min, max);
		const float center[3] = { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f };
		const float halfExtents[3] = { (max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f };

		dtQueryFilter doorsOnly;
		doorsOnly.setIncludeFlags(DoorPolyFlag);

		dtPolyRef polys[MaxStackedPolys];
		int polyCount = 0;
		if (dtStatusFailed(m_navQuery.queryPolygons(center, halfExtents, &doorsOnly, polys, &polyCount, MaxStackedPolys)))
		{
			return result;
		}

		for (int i = 0; i < polyCount; ++i)
		{
			const dtMeshTile* tile = nullptr;
			const dtPoly* poly = nullptr;
			if (dtStatusFailed(m_navMesh.getTileAndPolyByRef(polys[i], &tile, &poly)) || poly->vertCount == 0)
			{
				continue;
			}

			Vector3 polyCenter = Vector3::Zero;
			for (unsigned char v = 0; v < poly->vertCount; ++v)
			{
				const float* vert = &tile->verts[poly->verts[v] * 3];
				polyCenter += Vector3(vert[0], vert[1], vert[2]);
			}
			polyCenter /= static_cast<float>(poly->vertCount);

			// The box overlap above also catches door polys of a neighbouring door; the strip a
			// door's own volume carved has its center inside that volume.
			if (volume.ContainsXZ(polyCenter))
			{
				result.push_back(polys[i]);
			}
		}

		return result;
	}
```

`MaxStackedPolys` is 128 (map.h:155). A door strip is a handful of polys, so 128 is ample.

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```bash
cmake --build build --config Debug -t nav_mesh_tests
./bin/Debug/nav_mesh_tests "[door_volume]"
```
Expected: 4 test cases PASS.

- [ ] **Step 6: Commit**

```bash
git add src/shared/nav_mesh src/tests/nav_mesh_tests
git commit -m "nav: door volume geometry shared by builder and server"
```

---

### Task 3: Mark door areas in the nav builder pipeline

**Files:**
- Modify: `src/shared/nav_build/common.h`
- Create: `src/shared/nav_build/door_marking.h`, `src/shared/nav_build/door_marking.cpp`
- Modify: `src/shared/nav_build/mesh_builder.h` (add `SetDoorVolumes`, member `m_doorVolumes`), `src/shared/nav_build/mesh_builder.cpp` (`SerializeMeshTile` signature and call site ~509)
- Modify: `src/shared/nav_build/CMakeLists.txt` (link `nav_mesh`)
- Create: `src/tests/nav_build_tests/door_marking_tests.cpp`
- Modify: `src/tests/nav_build_tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `nav::DoorVolume`, `nav::DoorPolyFlag` (Tasks 1-2)
- Produces:
  - `poly_flags::Door = 1 << 5`
  - `void mmo::MarkDoorAreas(rcContext& ctx, rcCompactHeightfield& chf, const std::vector<nav::DoorVolume>& doors)`
  - `void MeshBuilder::SetDoorVolumes(std::vector<nav::DoorVolume> doors)`. Must be called before the worker threads start.

- [ ] **Step 1: Write the failing pipeline test**

Create `src/tests/nav_build_tests/door_marking_tests.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_build/common.h"
#include "nav_build/door_marking.h"
#include "nav_mesh/door_volume.h"

#include "Recast.h"

#include <memory>

using namespace mmo;

namespace
{
	/// Runs Recast on a flat 30 x 30 m floor at y = 0, with the given doors marked after erosion,
	/// using the nav builder's cell size and region settings. Returns the poly mesh.
	std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> BuildFloor(const std::vector<nav::DoorVolume>& doors)
	{
		rcContext ctx(false);

		rcConfig config {};
		config.cs = 33.3333f / 128.f;
		config.ch = 0.25f;
		config.walkableSlopeAngle = 75.f;
		config.walkableHeight = 7;
		config.walkableClimb = 2;
		config.walkableRadius = 2;
		config.maxEdgeLen = 46;
		config.maxSimplificationError = 0.8f;
		config.minRegionArea = 400;
		config.mergeRegionArea = 200;
		config.maxVertsPerPoly = 6;
		config.bmin[0] = 0.f; config.bmin[1] = -1.f; config.bmin[2] = 0.f;
		config.bmax[0] = 30.f; config.bmax[1] = 5.f; config.bmax[2] = 30.f;
		rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

		std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(), rcFreeHeightField);
		REQUIRE(rcCreateHeightfield(&ctx, *solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch));

		const float verts[] = { 0.f, 0.f, 0.f,  0.f, 0.f, 30.f,  30.f, 0.f, 30.f,  30.f, 0.f, 0.f };
		const int tris[] = { 0, 1, 2,  0, 2, 3 };
		const unsigned char areas[] = { poly_flags::Ground, poly_flags::Ground };
		REQUIRE(rcRasterizeTriangles(&ctx, verts, 4, tris, areas, 2, *solid, config.walkableClimb));

		std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> chf(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
		REQUIRE(rcBuildCompactHeightfield(&ctx, config.walkableHeight, config.walkableClimb, *solid, *chf));
		REQUIRE(rcErodeWalkableArea(&ctx, config.walkableRadius, *chf));

		MarkDoorAreas(ctx, *chf, doors);

		REQUIRE(rcBuildDistanceField(&ctx, *chf));
		REQUIRE(rcBuildRegions(&ctx, *chf, 0, config.minRegionArea, config.mergeRegionArea));

		std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(), rcFreeContourSet);
		REQUIRE(rcBuildContours(&ctx, *chf, config.maxSimplificationError, config.maxEdgeLen, *contours, 0));

		std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> mesh(rcAllocPolyMesh(), rcFreePolyMesh);
		REQUIRE(rcBuildPolyMesh(&ctx, *contours, config.maxVertsPerPoly, *mesh));
		return mesh;
	}

	/// A 6 m wide barrier across the floor at z = 15, as ComputeDoorVolume inflates it.
	nav::DoorVolume Barrier()
	{
		nav::DoorVolume volume;
		volume.center = Vector3(15.f, 0.f, 15.f);
		volume.axisX = Vector3::UnitX;
		volume.axisZ = Vector3::UnitZ;
		volume.halfX = 3.f + nav::DoorVolumeInflate;
		volume.halfZ = nav::DoorVolumeInflate;
		volume.minY = -nav::DoorVolumeHeightPadding;
		volume.maxY = 7.f + nav::DoorVolumeHeightPadding;
		return volume;
	}

	/// World XZ center of a poly of a Recast poly mesh.
	Vector3 PolyCenter(const rcPolyMesh& mesh, const int poly)
	{
		Vector3 sum = Vector3::Zero;
		int count = 0;
		for (int v = 0; v < mesh.nvp; ++v)
		{
			const unsigned short index = mesh.polys[poly * mesh.nvp * 2 + v];
			if (index == RC_MESH_NULL_IDX)
			{
				break;
			}

			const unsigned short* vert = &mesh.verts[index * 3];
			sum += Vector3(mesh.bmin[0] + vert[0] * mesh.cs, 0.f, mesh.bmin[2] + vert[2] * mesh.cs);
			++count;
		}
		return sum / static_cast<float>(count);
	}
}

TEST_CASE("Door poly flag matches the nav mesh's door flag", "[nav_build][door]")
{
	STATIC_REQUIRE(static_cast<uint16>(poly_flags::Door) == nav::DoorPolyFlag);
}

TEST_CASE("A marked door survives as a separate door-area poly strip", "[nav_build][door]")
{
	const auto mesh = BuildFloor({ Barrier() });

	int doorPolys = 0;
	for (int i = 0; i < mesh->npolys; ++i)
	{
		const Vector3 center = PolyCenter(*mesh, i);
		if ((mesh->areas[i] & poly_flags::Door) != 0)
		{
			++doorPolys;
			CHECK(Barrier().ContainsXZ(center));
		}
		else
		{
			// Floor polys keep their ground area.
			CHECK((mesh->areas[i] & poly_flags::Ground) != 0);
		}
	}

	CHECK(doorPolys > 0);
}

TEST_CASE("Without doors no door polys are produced", "[nav_build][door]")
{
	const auto mesh = BuildFloor({});
	for (int i = 0; i < mesh->npolys; ++i)
	{
		CHECK((mesh->areas[i] & poly_flags::Door) == 0);
	}
}
```

Change `src/tests/nav_build_tests/CMakeLists.txt` to:

```cmake
mmo_add_test(nav_build_tests
	base
	math
	nav_mesh
	Recast)

# The rasterizer and door marking are compiled in directly: linking nav_build would pull in its
# scene graph and graphics dependencies, which do not exist in a headless build.
target_sources(nav_build_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/nav_build/rasterize.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/nav_build/door_marking.cpp
)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build --config Debug -t nav_build_tests`
Expected: compile error, because `nav_build/door_marking.h` is not found and `poly_flags::Door` is unknown.

- [ ] **Step 3: Implement the marking**

In `src/shared/nav_build/common.h`, add after `WorldModel`:

```cpp
            /// Cut out under a door's footprint, so world instances can block it while the door is
            /// closed. Must equal nav::DoorPolyFlag.
            Door = 1 << 5,
```

Create `src/shared/nav_build/door_marking.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "nav_mesh/door_volume.h"

#include <vector>

class rcContext;
struct rcCompactHeightfield;

namespace mmo
{
	/// @brief Marks the walkable spans under every door volume with the door area, so Recast
	/// cuts a separate poly strip there. Call after erosion and before building regions.
	/// @details The area keeps its other bits (Ground, Entity, ...) and gains poly_flags::Door.
	/// Doors outside the heightfield are ignored.
	void MarkDoorAreas(rcContext& ctx, rcCompactHeightfield& chf, const std::vector<nav::DoorVolume>& doors);
}
```

Create `src/shared/nav_build/door_marking.cpp`. `rcMarkConvexPolyArea` overwrites the area. Mark a temporary id, then OR it back into the original flags:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "door_marking.h"

#include "common.h"

#include "Recast.h"

namespace mmo
{
	namespace
	{
		/// Area id Recast writes under a door. Not a valid flag combination of its own: it is
		/// replaced by the span's original area plus the door bit right after marking.
		constexpr unsigned char TemporaryDoorArea = 0xFE;
	}

	void MarkDoorAreas(rcContext& ctx, rcCompactHeightfield& chf, const std::vector<nav::DoorVolume>& doors)
	{
		if (doors.empty())
		{
			return;
		}

		const int spanCount = chf.spanCount;
		std::vector<unsigned char> original(chf.areas, chf.areas + spanCount);

		for (const nav::DoorVolume& door : doors)
		{
			Vector3 min, max;
			door.GetWorldBounds(min, max);
			if (max.x < chf.bmin[0] || min.x > chf.bmax[0] || max.z < chf.bmin[2] || min.z > chf.bmax[2])
			{
				continue;
			}

			float verts[12];
			door.GetCorners(verts);
			rcMarkConvexPolyArea(&ctx, verts, 4, door.minY, door.maxY, TemporaryDoorArea, chf);
		}

		for (int i = 0; i < spanCount; ++i)
		{
			if (chf.areas[i] != TemporaryDoorArea)
			{
				continue;
			}

			// Unwalkable spans stay unwalkable; a door only marks floor that exists.
			chf.areas[i] = original[i] == RC_NULL_AREA
				? RC_NULL_AREA
				: static_cast<unsigned char>(original[i] | poly_flags::Door);
		}
	}
}
```

`rcMarkConvexPolyArea` only marks spans whose area is not `RC_NULL_AREA`. The restore step keeps that guarantee even if this changes. Check the declared parameter order in `Recast.h:1150` (`context, verts, numVerts, minY, maxY, areaId, compactHeightfield`). If the local copy differs, adjust the call.

In `src/shared/nav_build/CMakeLists.txt`, add `nav_mesh` to the `target_link_libraries(nav_build ...)` list.

- [ ] **Step 4: Run the tests to verify they pass**

Run:
```bash
cmake --build build --config Debug -t nav_build_tests
./bin/Debug/nav_build_tests "[door]"
```
Expected: 3 test cases PASS.

If "survives as a separate door-area poly strip" fails with `doorPolys == 0`: either `minRegionArea` removed the strip or the Y range missed the floor span. Print `chf.areas` counts after `MarkDoorAreas` to tell them apart. Do not lower `minRegionArea`. Recast keeps a region that is connected to the floor (deps/recastnavigation/Recast/Source/RecastRegion.cpp, the small-region filter counts connected regions across areas). If the Y range is the cause, fix it in `DoorVolumeHeightPadding`.

- [ ] **Step 5: Wire it into `MeshBuilder`**

In `mesh_builder.h`, add `#include "nav_mesh/door_volume.h"`, a public method and a member:

```cpp
		/// @brief Sets the door footprints to cut out as door polys. Must be called before the
		/// first BuildAndSerializeTerrainTile; the list is read concurrently by the worker threads.
		void SetDoorVolumes(std::vector<nav::DoorVolume> doors) { m_doorVolumes = std::move(doors); }
```

```cpp
		/// Door footprints, see SetDoorVolumes. Read-only once building starts.
		std::vector<nav::DoorVolume> m_doorVolumes;
```

In `mesh_builder.cpp`:
- Add `#include "door_marking.h"`.
- Change `SerializeMeshTile` to take `const std::vector<nav::DoorVolume>& doors` as the last parameter.
- Insert the marking between `rcErodeWalkableArea` and `rcBuildDistanceField`:

```cpp
            // Doors get their own poly strip so each world instance can block it while the door
            // is closed (nav::InstanceQueryFilter). Marked after erosion so the strip keeps the
            // eroded clearance, and before regions so Recast does not merge it into the floor.
            MarkDoorAreas(ctx, *chf, doors);
```

At the call site (~509), pass `m_doorVolumes`: `SerializeMeshTile(ctx, config, tile.x, tile.y, *solid, heightFieldWriter, m_doorVolumes)`.

The existing area→flags copy (mesh_builder.cpp ~225-234) then turns `Ground | Door` into poly flags. Nothing else changes.

- [ ] **Step 6: Build the tool**

Run: `cmake --build build --config Release -t nav_builder`
Expected: builds.

- [ ] **Step 7: Commit**

```bash
git add src/shared/nav_build src/tests/nav_build_tests
git commit -m "nav_build: cut door footprints out as door polys"
```

---

### Task 4: nav_builder reads door spawns from the editor project

**Files:**
- Modify: `src/nav_builder/main.cpp`
- Modify: `src/nav_builder/CMakeLists.txt`

**Interfaces:**
- Consumes: `MeshBuilder::SetDoorVolumes` (Task 3), `nav::MakeDoorTransform`, `nav::ComputeDoorVolume` (Task 2), `proto::Project::load`, `MapEntity` (`nav_build/map.h`, has `Bounds` of the COLL tree)
- Produces: CLI `-p,project <path>`, the editor data folder (e.g. `data/editor/data`). Without it, no doors are baked and a warning is logged.

- [ ] **Step 1: Link the project data**

`src/nav_builder/CMakeLists.txt`:

```cmake
add_exe(nav_builder)
target_link_libraries(nav_builder log base nav_build proto_data)
```

- [ ] **Step 2: Collect door volumes**

In `main.cpp`, add includes:

```cpp
#include "game/object_type_id.h"
#include "nav_build/map.h"
#include "nav_mesh/door_volume.h"
#include "proto_data/project.h"
```

Check the include path the world server uses for `project.h`. If it differs (e.g. `shared/proto_data/project.h`), use that one.

Add in `namespace mmo`, above `run`:

```cpp
	/// Collects the footprint of every door spawned on the map whose directory is the world name.
	/// @param project The loaded editor project.
	/// @param worldName The world being built (MapEntry::directory()).
	std::vector<nav::DoorVolume> CollectDoorVolumes(const proto::Project& project, const std::string& worldName)
	{
		std::vector<nav::DoorVolume> doors;

		const proto::MapEntry* map = nullptr;
		for (const auto& entry : project.maps.getTemplates().entry())
		{
			if (entry.directory() == worldName)
			{
				map = &entry;
				break;
			}
		}

		if (!map)
		{
			WLOG("No map in the project uses directory '" << worldName << "' - no doors are baked into the nav mesh");
			return doors;
		}

		std::map<std::string, std::unique_ptr<MapEntity>> meshes;
		for (const auto& spawn : map->objectspawns())
		{
			const auto* object = project.objects.getById(spawn.objectentry());
			if (!object || object->type() != game_world_object_type::Door)
			{
				continue;
			}

			const auto* display = project.objectDisplays.getById(object->displayid());
			if (!display || display->filename().empty())
			{
				WLOG("Door spawn '" << spawn.name() << "' (object " << spawn.objectentry() << ") has no display mesh - skipped");
				continue;
			}

			auto& mesh = meshes[display->filename()];
			if (!mesh)
			{
				mesh = std::make_unique<MapEntity>(display->filename());
			}

			if (mesh->Vertices.empty())
			{
				WLOG("Door mesh '" << display->filename() << "' has no collision - door spawn '" << spawn.name() << "' skipped");
				continue;
			}

			const auto& location = spawn.location();
			const Matrix4 transform = nav::MakeDoorTransform(
				Vector3(location.positionx(), location.positiony(), location.positionz()),
				Quaternion(location.rotationw(), location.rotationx(), location.rotationy(), location.rotationz()),
				object->scale());
			doors.push_back(nav::ComputeDoorVolume(mesh->Bounds, transform));
		}

		ILOG("Baking " << doors.size() << " door(s) into the nav mesh of " << worldName);
		return doors;
	}
```

Check how `ObjectManager` iterates (`getTemplates().entry()` is the pattern of the generated template managers in `proto_data/project.h`). Use the accessor the other managers in that header use if it differs. `MapEntity` loads a full mesh, which is why `main` already creates the null graphics device.

Change `run` to take the doors and hand them to the builder:

```cpp
	int32 run(const std::string& worldName, const std::string& directoryPath, size_t concurrentThreads, std::vector<nav::DoorVolume> doors)
	{
		ASSERT(concurrentThreads > 0);

		auto builder = std::make_unique<MeshBuilder>(directoryPath, worldName);
		builder->SetDoorVolumes(std::move(doors));
		...
```

Update the Doxygen of `run` with `@param doors Door footprints to cut out as door polys.`

In `main`:
- Add `mmo::String projectPath;`.
- Add the option `("p,project", "editor data folder (e.g. data/editor/data) to read door spawns from", cxxopts::value<std::string>(projectPath))`.
- After the asset registry is initialized and before `run`:

```cpp
		std::vector<mmo::nav::DoorVolume> doors;
		if (projectPath.empty())
		{
			WLOG("No --project given: doors are not baked into the nav mesh, so closed doors will not block paths");
		}
		else
		{
			mmo::proto::Project project;
			if (!project.load(projectPath))
			{
				ELOG("Failed to load the project from " << projectPath);
				return 1;
			}

			doors = mmo::CollectDoorVolumes(project, worldName);
		}
```

Pass `std::move(doors)` into `run`.

- [ ] **Step 3: Build and smoke-test on the Hollow Choir map**

Run:
```bash
cmake --build build --config Release -t nav_builder
./bin/Release/nav_builder -d data/client -w Test -o "$SCRATCH/nav_test_out" -p data/editor/data
```
`$SCRATCH` is the session scratchpad. Do not write into `data/editor` yet; that happens in Task 8.

Expected log: `Baking N door(s) into the nav mesh of Test` with N ≥ 5 (G2_North, G2_South, G2_NorthArch, G2_SouthArch, G2_Corridor, plus any other Door spawns on the map), no `has no collision` warning for `MagicBarrier_01.hmsh`, then `Finished`, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/nav_builder
git commit -m "nav_builder: bake door spawns from the editor project"
```

---

### Task 5: `MapStaticData` and its registry, static collision shared per map

**Files:**
- Modify: `src/shared/game_server/world/server_collision_map.h/.cpp`
- Create: `src/shared/game_server/world/map_static_data.h`, `src/shared/game_server/world/map_static_data.cpp`
- Modify: `src/shared/game_server/world/world_instance.h` (NavMapData members and constructor), `src/shared/game_server/world/world_instance.cpp` (NavMapData ~72-243, constructor ~284)
- Modify: `src/shared/game_server/world/world_instance_manager.h/.cpp`
- Modify: `src/tests/game_server_tests/server_collision_map_test.cpp`
- Create: `src/tests/game_server_tests/map_static_data_test.cpp`

**Interfaces:**
- Consumes: `nav::Map`, `nav::PageStreamer`, `nav::InstanceQueryFilter` (Task 1)
- Produces:
  - `ServerCollisionMap::ServerCollisionMap(std::shared_ptr<const ServerCollisionMap> sharedStatic)`: a per-instance view over shared static geometry
  - `bool ServerCollisionMap::AddStaticInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, const std::string& debugName)`: for building a static map before it is shared
  - `bool ServerCollisionMap::GetDynamicInstanceLocalBounds(uint64 handle, AABB& out) const`
  - `class MapStaticData` with `explicit MapStaticData(const std::string& directory)`, `const std::string& GetDirectory() const`, `nav::Map& GetNavMap()`, `void EnsureNavLoaded(float minX, float minZ, float maxX, float maxZ)`, `bool IsNavStreaming() const`, `void Update()`, `std::shared_ptr<const ServerCollisionMap> GetStaticCollision() const`, `const ServerWaterMap* GetWaterMap() const`
  - `class MapStaticDataRegistry` with `std::shared_ptr<MapStaticData> Acquire(const std::string& directory)`, `void Update()`, `size_t GetLiveCount()`
  - `NavMapData::NavMapData(std::shared_ptr<MapStaticData> staticData)`; NavMapData also gets `const MapStaticData& GetStaticData() const`
  - `MapStaticDataRegistry& WorldInstanceManager::GetMapStaticData()`

- [ ] **Step 1: Write the failing collision sharing test**

Append to `src/tests/game_server_tests/server_collision_map_test.cpp`:

```cpp
TEST_CASE("ServerCollisionMap - shared static geometry blocks every instance view", "[server_collision_map]")
{
	auto shared = std::make_shared<ServerCollisionMap>();
	REQUIRE(shared->AddStaticInstance(buildWallTree(), Matrix4::Identity, "wall"));
	const std::shared_ptr<const ServerCollisionMap> sharedStatic = shared;

	ServerCollisionMap instanceA(sharedStatic);
	ServerCollisionMap instanceB(sharedStatic);

	CHECK(instanceA.IsLoaded());
	CHECK_FALSE(instanceA.LineOfSight(frontPos, backPos));
	CHECK_FALSE(instanceB.LineOfSight(frontPos, backPos));

	Vector3 hit;
	CHECK_FALSE(instanceA.LineOfSightEx(frontPos, backPos, hit));
	CHECK(hit.z == Approx(0.0f).margin(0.01f));
}

TEST_CASE("ServerCollisionMap - dynamic instances stay with their instance view", "[server_collision_map]")
{
	const std::shared_ptr<const ServerCollisionMap> sharedStatic = std::make_shared<ServerCollisionMap>();
	ServerCollisionMap instanceA(sharedStatic);
	ServerCollisionMap instanceB(sharedStatic);

	const uint64 handle = instanceA.AddDynamicInstance(buildWallTree(), Matrix4::Identity, true);
	REQUIRE(handle != 0);

	CHECK_FALSE(instanceA.LineOfSight(frontPos, backPos));
	CHECK(instanceB.LineOfSight(frontPos, backPos));

	AABB bounds;
	REQUIRE(instanceA.GetDynamicInstanceLocalBounds(handle, bounds));
	CHECK(bounds.max.x == Approx(1.0f));
	CHECK_FALSE(instanceB.GetDynamicInstanceLocalBounds(handle, bounds));
}
```

Add `#include "math/aabb.h"` to the includes.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_server_tests`
Expected: compile error, because there is no `AddStaticInstance`, no constructor taking `shared_ptr<const ServerCollisionMap>`, and no `GetDynamicInstanceLocalBounds`.

- [ ] **Step 3: Implement the shared static view**

In `server_collision_map.h`:
- Add the constructor and the two methods with Doxygen.
- Add the member `std::shared_ptr<const ServerCollisionMap> m_sharedStatic;`.
- `IsLoaded()` becomes `return !m_instances.empty() || (m_sharedStatic && m_sharedStatic->IsLoaded());`.

```cpp
		/// @brief Creates a per-instance view over static geometry shared by every instance of a map.
		/// @details Line of sight tests the shared static geometry and this view's own dynamic
		/// instances. Dynamic instances added here are never visible to other views.
		/// @param sharedStatic Static geometry, typically loaded once per map. May be null.
		explicit ServerCollisionMap(std::shared_ptr<const ServerCollisionMap> sharedStatic);

		/// @brief Adds a static collision instance. Only for building a map's static geometry
		/// before it is shared (and for tests); shared maps are const.
		/// @return False when the tree is null or empty, or the transform is rejected.
		bool AddStaticInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, const std::string& debugName);

		/// @brief Gets the mesh-space bounds of a dynamic instance's collision tree.
		/// @return False for unknown handles.
		[[nodiscard]] bool GetDynamicInstanceLocalBounds(uint64 handle, AABB& out) const;
```

In `server_collision_map.cpp`:

```cpp
	ServerCollisionMap::ServerCollisionMap(std::shared_ptr<const ServerCollisionMap> sharedStatic)
		: m_sharedStatic(std::move(sharedStatic))
	{
	}

	bool ServerCollisionMap::AddStaticInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, const std::string& debugName)
	{
		return AddInstance(std::move(tree), transform, debugName);
	}

	bool ServerCollisionMap::GetDynamicInstanceLocalBounds(const uint64 handle, AABB& out) const
	{
		const auto it = m_dynamicInstances.find(handle);
		if (it == m_dynamicInstances.end() || !it->second.instance.tree)
		{
			return false;
		}

		out = it->second.instance.tree->GetBoundingBox();
		return true;
	}
```

In `LineOfSight`:
- The early-out becomes `if (m_instances.empty() && m_dynamicInstances.empty() && !m_sharedStatic)`.
- Before the `m_instances` loop, add:

```cpp
		if (m_sharedStatic && !m_sharedStatic->LineOfSight(from, to))
		{
			return false; // blocked by shared static geometry
		}
```

In `LineOfSightEx`:
- Apply the same early-out change.
- Replace the start of the static test with a merge of the shared result:

```cpp
		if (m_sharedStatic)
		{
			Vector3 sharedHit;
			if (!m_sharedStatic->LineOfSightEx(from, to, sharedHit))
			{
				const float sharedT = worldLen > 0.f ? (sharedHit - from).GetLength() / worldLen : 0.f;
				if (sharedT < closestWorldT)
				{
					closestWorldT = sharedT;
					hitPoint = sharedHit;
					blocked = true;
				}
			}
		}
```

Place this after `float closestWorldT = 1.0f; bool blocked = false;` and before the `m_instances` loop.

- [ ] **Step 4: Run the collision tests**

Run:
```bash
cmake --build build --config Debug -t game_server_tests
./bin/Debug/game_server_tests "[server_collision_map]"
```
Expected: all PASS, old and new.

- [ ] **Step 5: Write the failing registry tests**

Create `src/tests/game_server_tests/map_static_data_test.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/map_static_data.h"

#include <memory>

using namespace mmo;

namespace
{
	// No files exist for this directory: the nav map is invalid, the collision map empty.
	constexpr const char* MissingMap = "Worlds/__static_data_test__/__static_data_test__";
}

TEST_CASE("MapStaticDataRegistry shares one MapStaticData per directory", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	const auto a = registry.Acquire(MissingMap);
	const auto b = registry.Acquire(MissingMap);
	REQUIRE(a);
	CHECK(a == b);
	CHECK(registry.GetLiveCount() == 1);

	const auto other = registry.Acquire("Worlds/__other__/__other__");
	CHECK(other != a);
	CHECK(registry.GetLiveCount() == 2);
}

TEST_CASE("MapStaticDataRegistry frees data once the last user is gone", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	std::weak_ptr<MapStaticData> weak;
	{
		const auto a = registry.Acquire(MissingMap);
		weak = a;
		CHECK(registry.GetLiveCount() == 1);
	}

	CHECK(weak.expired());
	CHECK(registry.GetLiveCount() == 0);
}

TEST_CASE("MapStaticDataRegistry reloads after the last instance is gone", "[map_static_data]")
{
	MapStaticDataRegistry registry;

	{
		const auto first = registry.Acquire(MissingMap);
		REQUIRE(first);
	}

	const auto second = registry.Acquire(MissingMap);
	REQUIRE(second);
	CHECK(second->GetDirectory() == MissingMap);
	CHECK(registry.GetLiveCount() == 1);

	// Update on data without nav pages must be a harmless no-op.
	registry.Update();
}
```

- [ ] **Step 6: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_server_tests`
Expected: compile error, because `game_server/world/map_static_data.h` is not found.

- [ ] **Step 7: Implement `MapStaticData` and the registry**

Create `src/shared/game_server/world/map_static_data.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

namespace mmo
{
	namespace nav
	{
		class Map;
		class PageStreamer;
	}

	class ServerCollisionMap;
	class ServerWaterMap;

	/// @brief The read-only data of one map, loaded once and shared by every world instance of it:
	/// nav mesh (streamed in), static collision geometry and water.
	/// @details Per-instance state (door collision, the nav query filter) lives in NavMapData.
	/// World thread only: Detour is not thread safe and the world server runs single threaded.
	class MapStaticData final : public NonCopyable
	{
	public:
		/// @brief Loads the map's collision and water data and starts streaming its nav mesh.
		/// @param directory The map directory (proto::MapEntry::directory()).
		explicit MapStaticData(const std::string& directory);
		~MapStaticData() override;

	public:
		/// @brief The map directory this data was loaded from.
		[[nodiscard]] const std::string& GetDirectory() const { return m_directory; }

		/// @brief The shared nav mesh. Pages may still be streaming in, see EnsureNavLoaded.
		[[nodiscard]] nav::Map& GetNavMap() { return *m_navMap; }

		/// @copydoc GetNavMap
		[[nodiscard]] const nav::Map& GetNavMap() const { return *m_navMap; }

		/// @brief Installs every nav page overlapping the XZ rectangle. No-op once streaming is complete.
		void EnsureNavLoaded(float minX, float minZ, float maxX, float maxZ);

		/// @brief Whether nav pages are still streaming in.
		[[nodiscard]] bool IsNavStreaming() const { return m_pageStreamer != nullptr; }

		/// @brief Installs streamed nav pages within a time budget. Called once per world tick.
		void Update();

		/// @brief Static collision geometry, for per-instance ServerCollisionMap views.
		[[nodiscard]] std::shared_ptr<const ServerCollisionMap> GetStaticCollision() const { return m_staticCollision; }

		/// @brief Water data, or null when the map has no water.
		[[nodiscard]] const ServerWaterMap* GetWaterMap() const { return m_waterMap.get(); }

	private:
		const std::string m_directory;
		std::unique_ptr<nav::Map> m_navMap;
		/// Declared after m_navMap, which it fills, so it is destroyed first. Reset once complete.
		std::unique_ptr<nav::PageStreamer> m_pageStreamer;
		std::chrono::steady_clock::time_point m_navStreamStart;
		std::shared_ptr<const ServerCollisionMap> m_staticCollision;
		std::unique_ptr<ServerWaterMap> m_waterMap;
	};

	/// @brief Hands out one MapStaticData per map directory, shared by all instances of that map.
	/// @details Holds only weak references: the data is freed when the last instance using it is
	/// destroyed, and loaded again by the next Acquire. World thread only.
	class MapStaticDataRegistry final : public NonCopyable
	{
	public:
		/// @brief Returns the live data of a map, loading it if no instance holds it.
		[[nodiscard]] std::shared_ptr<MapStaticData> Acquire(const std::string& directory);

		/// @brief Updates every live map's data once (nav page streaming) and forgets freed maps.
		void Update();

		/// @brief Number of maps whose data is currently alive.
		[[nodiscard]] size_t GetLiveCount();

	private:
		void Prune();

	private:
		std::unordered_map<std::string, std::weak_ptr<MapStaticData>> m_maps;
	};
}
```

Create `src/shared/game_server/world/map_static_data.cpp`. Move the loading and streaming code verbatim from `NavMapData`'s constructor and `NavMapData::Update` (world_instance.cpp:72-103, 164-183). Keep the log lines, with the prefix renamed to `MapStaticData:`.

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "map_static_data.h"

#include "server_collision_map.h"
#include "server_water_map.h"

#include "log/default_log_levels.h"
#include "nav_mesh/map.h"
#include "nav_mesh/page_streamer.h"

namespace mmo
{
	MapStaticData::MapStaticData(const std::string& directory)
		: m_directory(directory)
	{
		m_navMap = std::make_unique<nav::Map>(directory);

		// Stream the nav mesh pages in instead of loading them all here: on a large map that
		// held up the world thread. Queries load what they touch on demand until streaming
		// completes (see EnsureNavLoaded).
		m_pageStreamer = std::make_unique<nav::PageStreamer>(*m_navMap);
		m_navStreamStart = std::chrono::steady_clock::now();
		DLOG("MapStaticData: streaming " << m_pageStreamer->GetPageCount() << " nav page(s) for map '" << directory << "'");

		// Load world geometry for accurate 3D LOS. Kept even when empty, so per-instance views
		// still work for dynamic collision (doors); the empty-map early-out keeps queries cheap.
		auto collision = std::make_shared<ServerCollisionMap>(directory);
		if (collision->IsLoaded())
		{
			DLOG("MapStaticData: using geometry-based LOS for map '" << directory << "'");
		}
		else
		{
			WLOG("MapStaticData: no static collision geometry for map '"
				<< directory << "' — only dynamic objects (e.g. doors) will block IsInLineOfSight");
		}
		m_staticCollision = std::move(collision);

		// Load terrain water data for swim validation. Optional — null when the map has no water.
		auto waterMap = std::make_unique<ServerWaterMap>(directory);
		if (waterMap->IsLoaded())
		{
			m_waterMap = std::move(waterMap);
		}
	}

	MapStaticData::~MapStaticData() = default;

	void MapStaticData::EnsureNavLoaded(const float minX, const float minZ, const float maxX, const float maxZ)
	{
		if (m_pageStreamer)
		{
			m_pageStreamer->EnsureLoaded(minX, minZ, maxX, maxZ);
		}
	}

	void MapStaticData::Update()
	{
		if (!m_pageStreamer)
		{
			return;
		}

		// A slice of each 30 ms world tick, paid once per map no matter how many instances share it.
		// Reading happens on the streamer's thread; this only adds the read tiles to Detour.
		constexpr auto installBudget = std::chrono::milliseconds(4);
		if (!m_pageStreamer->InstallReadyPages(installBudget))
		{
			return;
		}

		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_navStreamStart);
		DLOG("MapStaticData: nav map '" << m_navMap->GetName() << "' complete, " << m_pageStreamer->GetPageCount() << " page(s) streamed in " << elapsed.count() << " ms"
			<< (m_pageStreamer->GetFailedPageCount() > 0 ? " (" + std::to_string(m_pageStreamer->GetFailedPageCount()) + " failed)" : std::string()));
		m_pageStreamer.reset();
	}

	std::shared_ptr<MapStaticData> MapStaticDataRegistry::Acquire(const std::string& directory)
	{
		auto& slot = m_maps[directory];
		if (auto existing = slot.lock())
		{
			return existing;
		}

		auto created = std::make_shared<MapStaticData>(directory);
		slot = created;
		return created;
	}

	void MapStaticDataRegistry::Update()
	{
		Prune();
		for (auto& [directory, weak] : m_maps)
		{
			if (const auto data = weak.lock())
			{
				data->Update();
			}
		}
	}

	size_t MapStaticDataRegistry::GetLiveCount()
	{
		Prune();
		return m_maps.size();
	}

	void MapStaticDataRegistry::Prune()
	{
		for (auto it = m_maps.begin(); it != m_maps.end(); )
		{
			if (it->second.expired())
			{
				it = m_maps.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
```

Check: the header name of `ServerWaterMap` (`server_water_map.h` in the same folder). `game_server` uses `add_lib_recurse`, so the new file is globbed. Re-run `cmake -S . -B build` after adding files.

- [ ] **Step 8: Move `NavMapData` onto `MapStaticData`**

In `world_instance.h`:
- Forward-declare `class MapStaticData;` next to `ServerCollisionMap`.
- Add `#include "nav_mesh/instance_query_filter.h"`.
- Replace NavMapData's constructor and members:

```cpp
		/// @param staticData The map's shared static data (nav mesh, static collision, water).
		explicit NavMapData(std::shared_ptr<MapStaticData> staticData);
```

```cpp
		/// @brief The map's shared static data.
		[[nodiscard]] const MapStaticData& GetStaticData() const { return *m_staticData; }

	private:
		/// @brief Makes sure the nav mesh has every page overlapping the XZ rectangle spanned by
		/// a and b, grown by margin. No-op once streaming has completed.
		void EnsureNavLoaded(const Vector3& a, const Vector3& b, float margin) const;

	private:
		/// Shared by every instance of the map. Streaming is driven by MapStaticDataRegistry.
		std::shared_ptr<MapStaticData> m_staticData;
		/// This instance's LOS view: the shared static geometry plus this instance's doors.
		std::unique_ptr<ServerCollisionMap> m_collisionMap;
		/// This instance's door state on the shared nav mesh.
		nav::InstanceQueryFilter m_queryFilter;
```

Remove `m_map`, `m_pageStreamer`, `m_navStreamStart` and `m_waterMap`. Remove the `Update()` override from NavMapData. The `MapData::Update` default stays for other implementations. Also drop `#include "nav_mesh/page_streamer.h"` from the header if nothing else there needs it.

In `world_instance.cpp`, add `#include "map_static_data.h"` and rewrite the NavMapData functions:

```cpp
	NavMapData::NavMapData(std::shared_ptr<MapStaticData> staticData)
		: m_staticData(std::move(staticData))
		, m_collisionMap(std::make_unique<ServerCollisionMap>(m_staticData->GetStaticCollision()))
	{
	}

	void NavMapData::EnsureNavLoaded(const Vector3& a, const Vector3& b, const float margin) const
	{
		m_staticData->EnsureNavLoaded(
			std::min(a.x, b.x) - margin, std::min(a.z, b.z) - margin,
			std::max(a.x, b.x) + margin, std::max(a.z, b.z) + margin);
	}
```

In `CalculatePath`:
- Replace `m_map->FindPath(start, destination, out_path, true)` (both calls) with `m_staticData->GetNavMap().FindPath(start, destination, out_path, true, &m_queryFilter)`.
- Replace the `!m_pageStreamer` check with `!m_staticData->IsNavStreaming()`.

In `FindRandomPointAroundCircle`, use `m_staticData->GetNavMap().FindRandomPointAroundCircle(centerPosition, radius, randomPoint, &m_queryFilter)`.

`GetWaterSurface` / `IsWaterDataAvailable` use `m_staticData->GetWaterMap()`.

`EnsureNavLoaded` is `const` but calls a non-const method on the pointee, which is fine through `shared_ptr`.

Delete `NavMapData::Update`.

In `WorldInstance`'s constructor (~284), replace `m_mapData = std::make_unique<NavMapData>(*m_mapEntry);` with:

```cpp
		m_mapData = std::make_unique<NavMapData>(m_manager.GetMapStaticData().Acquire(m_mapEntry->directory()));
```

The `m_mapData->Update()` call in `WorldInstance::Update` (~355) stays. NavMapData no longer overrides it, so it is a no-op for nav maps.

In `world_instance_manager.h`:
- Add `#include "map_static_data.h"` and the public accessor:

```cpp
		/// Gets the registry of per-map static data shared by all instances of a map.
		MapStaticDataRegistry& GetMapStaticData() { return m_mapStaticData; }
```

- Add the member, declared **before** `m_worldInstances` so it is destroyed after the instances:

```cpp
		/// Per-map nav mesh, static collision and water, shared by every instance of a map.
		MapStaticDataRegistry m_mapStaticData;
```

In `world_instance_manager.cpp` `Update`, before the instance loop:

```cpp
		// Stream nav pages once per map, not once per instance sharing it.
		m_mapStaticData.Update();
```

- [ ] **Step 9: Run all game server and nav tests**

Run:
```bash
cmake -S . -B build
cmake --build build --config Debug -t all_tests world_server
./bin/Debug/game_server_tests "[map_static_data]"
cd build && ctest -C Debug --output-on-failure
```
Expected: all PASS, and the world server builds.

- [ ] **Step 10: Check shutdown**

Run: `python tools/shutdown_check.py`
Expected: passes. The streamer thread is joined when the last instance releases the `MapStaticData`, and the registry owns no timers.

- [ ] **Step 11: Commit**

```bash
git add src/shared/game_server src/tests/game_server_tests
git commit -m "world: share nav mesh, static collision and water per map"
```

---

### Task 6: Closed doors block nav per instance

**Files:**
- Modify: `src/shared/game_server/world/world_instance.h` (MapData interface, NavMapData, `DoorCollision` struct ~493)
- Modify: `src/shared/game_server/world/world_instance.cpp` (NavMapData, `RegisterDoorCollision` ~812-865, `UnregisterDoorCollision` ~867)
- Modify: `src/tests/game_server_tests/map_static_data_test.cpp`
- Modify: `src/tests/game_server_tests/CMakeLists.txt` (no new libs are expected; `nav_mesh` comes through `game_server`)

**Interfaces:**
- Consumes: `nav::Map::FindDoorPolys`, `nav::DoorVolume`, `nav::MakeDoorTransform`, `nav::ComputeDoorVolume` (Task 2), `nav::InstanceQueryFilter` (Task 1), `ServerCollisionMap::GetDynamicInstanceLocalBounds`, `NavMapData(std::shared_ptr<MapStaticData>)` (Task 5), test helpers from `src/tests/nav_mesh_tests/synthetic_nav_map.h` (Task 1)
- Produces, on `MapData` (virtual, default no-op) and overridden by NavMapData:
  - `virtual uint64 AddNavObstacle(const nav::DoorVolume& volume, bool blocking) { return 0; }`: returns 0 when no door polys were found
  - `virtual void SetNavObstacleBlocking(uint64 handle, bool blocking) {}`
  - `virtual void RemoveNavObstacle(uint64 handle) {}`
  - `virtual bool GetDynamicCollisionLocalBounds(uint64 handle, AABB& out) const { return false; }`

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/game_server_tests/map_static_data_test.cpp`:

```cpp
#include "../nav_mesh_tests/synthetic_nav_map.h"

#include "game_server/world/world_instance.h"
#include "nav_mesh/door_volume.h"
#include "nav_mesh/instance_query_filter.h"

namespace
{
	/// Walled corridor [floor | door | floor] on global tile (512, 512), world [0, TileSize]^2.
	std::optional<std::vector<uint8>> CorridorTile(const int32 x, const int32 y)
	{
		if (x != 512 || y != 512)
		{
			return std::nullopt;
		}

		return test_nav::BuildGridTile(x, y, 3, 1, [](const int32 col, int32)
		{
			return static_cast<uint16>(col == 1 ? (1 | nav::DoorPolyFlag) : 1);
		}, false);
	}

	/// The door volume covering the corridor's middle quad.
	nav::DoorVolume MiddleDoor()
	{
		nav::DoorVolume volume;
		volume.center = Vector3(test_nav::TileSize * 0.5f, 0.f, test_nav::TileSize * 0.5f);
		volume.halfX = test_nav::TileSize / 6.f;
		volume.halfZ = test_nav::TileSize * 0.5f;
		volume.minY = -1.f;
		volume.maxY = 1.f;
		return volume;
	}

	const Vector3 West(test_nav::TileSize / 6.f, 0.f, test_nav::TileSize * 0.5f);
	const Vector3 East(test_nav::TileSize * 5.f / 6.f, 0.f, test_nav::TileSize * 0.5f);

	bool Reaches(const MapData& map, const Vector3& from, const Vector3& to)
	{
		std::vector<Vector3> path;
		return map.CalculatePath(from, to, path) && !path.empty() && path.back().GetDistanceTo(to) < 0.5f;
	}
}

TEST_CASE("A closed door blocks paths and opening it frees them", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } }, {}, CorridorTile);
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	CHECK(Reaches(map, West, East));

	const uint64 obstacle = map.AddNavObstacle(MiddleDoor(), true);
	REQUIRE(obstacle != 0);
	CHECK_FALSE(Reaches(map, West, East));

	map.SetNavObstacleBlocking(obstacle, false);
	CHECK(Reaches(map, West, East));

	map.SetNavObstacleBlocking(obstacle, true);
	CHECK_FALSE(Reaches(map, West, East));

	map.RemoveNavObstacle(obstacle);
	CHECK(Reaches(map, West, East));
}

TEST_CASE("Door state is per instance", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } }, {}, CorridorTile);
	MapStaticDataRegistry registry;
	NavMapData instanceA(registry.Acquire(test_nav::SyntheticNavMap::Name));
	NavMapData instanceB(registry.Acquire(test_nav::SyntheticNavMap::Name));
	REQUIRE(&instanceA.GetStaticData() == &instanceB.GetStaticData());

	REQUIRE(instanceA.AddNavObstacle(MiddleDoor(), true) != 0);
	CHECK_FALSE(Reaches(instanceA, West, East));
	CHECK(Reaches(instanceB, West, East));
}

TEST_CASE("Door registered before streaming completes", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } }, {}, CorridorTile);
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	// No registry.Update() has run: the door page is installed on demand by AddNavObstacle.
	const uint64 obstacle = map.AddNavObstacle(MiddleDoor(), true);
	REQUIRE(obstacle != 0);
	CHECK_FALSE(Reaches(map, West, East));
}

TEST_CASE("A door on a map without door polys registers no nav obstacle", "[map_static_data][door]")
{
	test_nav::SyntheticNavMap files({ { 32, 32 } });
	MapStaticDataRegistry registry;
	NavMapData map(registry.Acquire(test_nav::SyntheticNavMap::Name));

	CHECK(map.AddNavObstacle(MiddleDoor(), true) == 0);
	CHECK(Reaches(map, West, East));
}
```

If `synthetic_nav_map.h` fails to resolve `catch.hpp` or `nav_mesh/...` from the game_server_tests target, add `${CMAKE_SOURCE_DIR}/src/tests/nav_mesh_tests` via `target_include_directories(game_server_tests PRIVATE ...)` in `src/tests/game_server_tests/CMakeLists.txt` and include it as `"synthetic_nav_map.h"`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_server_tests`
Expected: compile error, because `AddNavObstacle` and the other obstacle methods are not members of `NavMapData`.

- [ ] **Step 3: Implement the obstacle API**

In `world_instance.h`:
- Add `#include "nav_mesh/door_volume.h"` and `#include "math/aabb.h"`.
- Add to `MapData`, after `SetDynamicCollisionEnabled`:

```cpp
		/// @brief Blocks pathfinding through a door's baked nav polys while the obstacle is blocking.
		/// @param volume The door footprint (nav::ComputeDoorVolume), matching what the nav builder baked.
		/// @param blocking Whether the obstacle initially blocks (door closed).
		/// @return Handle for toggling/removal, or 0 when the map has no door polys inside the volume.
		virtual uint64 AddNavObstacle(const nav::DoorVolume& volume, bool blocking) { return 0; }

		/// @brief Makes a nav obstacle block or let paths pass. Handle 0 and unknown handles are a safe no-op.
		virtual void SetNavObstacleBlocking(uint64 handle, bool blocking) {}

		/// @brief Removes a nav obstacle, unblocking its polys. Handle 0 and unknown handles are a safe no-op.
		virtual void RemoveNavObstacle(uint64 handle) {}

		/// @brief Gets the mesh-space collision bounds of dynamic collision geometry.
		/// @return False when unsupported or for unknown handles.
		virtual bool GetDynamicCollisionLocalBounds(uint64 handle, AABB& out) const { return false; }
```

- Add the four `override` declarations to `NavMapData`, plus the members:

```cpp
		struct NavObstacle
		{
			std::vector<dtPolyRef> polys;
			bool blocking = false;
		};

		/// This instance's door obstacles, by handle.
		std::unordered_map<uint64, NavObstacle> m_navObstacles;
		uint64 m_nextNavObstacle = 1;
```

In `world_instance.cpp`:

```cpp
	uint64 NavMapData::AddNavObstacle(const nav::DoorVolume& volume, const bool blocking)
	{
		// Doors spawn while pages may still be streaming: install the pages under the door first,
		// or its polys are not there to be found.
		Vector3 min, max;
		volume.GetWorldBounds(min, max);
		m_staticData->EnsureNavLoaded(min.x, min.z, max.x, max.z);

		NavObstacle obstacle;
		obstacle.polys = m_staticData->GetNavMap().FindDoorPolys(volume);
		if (obstacle.polys.empty())
		{
			return 0;
		}

		obstacle.blocking = blocking;
		if (blocking)
		{
			m_queryFilter.Block(obstacle.polys);
		}

		const uint64 handle = m_nextNavObstacle++;
		m_navObstacles.emplace(handle, std::move(obstacle));
		return handle;
	}

	void NavMapData::SetNavObstacleBlocking(const uint64 handle, const bool blocking)
	{
		const auto it = m_navObstacles.find(handle);
		if (it == m_navObstacles.end() || it->second.blocking == blocking)
		{
			return;
		}

		it->second.blocking = blocking;
		if (blocking)
		{
			m_queryFilter.Block(it->second.polys);
		}
		else
		{
			m_queryFilter.Unblock(it->second.polys);
		}
	}

	void NavMapData::RemoveNavObstacle(const uint64 handle)
	{
		const auto it = m_navObstacles.find(handle);
		if (it == m_navObstacles.end())
		{
			return;
		}

		if (it->second.blocking)
		{
			m_queryFilter.Unblock(it->second.polys);
		}
		m_navObstacles.erase(it);
	}

	bool NavMapData::GetDynamicCollisionLocalBounds(const uint64 handle, AABB& out) const
	{
		return m_collisionMap && handle != 0 && m_collisionMap->GetDynamicInstanceLocalBounds(handle, out);
	}
```

Poly refs stay valid for the lifetime of the `MapStaticData`: pages are never unloaded from a shared map. Add exactly that sentence as a comment above `m_navObstacles`.

- [ ] **Step 4: Run the tests**

Run:
```bash
cmake --build build --config Debug -t game_server_tests
./bin/Debug/game_server_tests "[door]"
```
Expected: the 4 new cases PASS, and the existing `[door]` world object tests still PASS.

- [ ] **Step 5: Hook doors in `WorldInstance`**

In `world_instance.h`, extend `DoorCollision` (~493) with the nav handle:

```cpp
		struct DoorCollision
		{
			uint64 handle = 0;
			/// Nav obstacle of the door's baked polys, 0 when the nav mesh has none for it.
			uint64 navObstacle = 0;
			scoped_connection stateChanged;
		};
```

Keep the existing member names. If the struct already names its fields differently, match them.

In `RegisterDoorCollision` (world_instance.cpp ~812-865):
- Replace the hand-built transform with `const Matrix4 transform = nav::MakeDoorTransform(object.GetPosition(), rotation, object.Get<float>(object_fields::Scale));`. Delete the local `entityYawOffset`/`scale` code it replaces, and add `#include "nav_mesh/door_volume.h"`.
- After the LOS handle is checked and before the `stateChanged` connection:

```cpp
		DoorCollision& door = m_doorCollisions[object.GetGuid()];
		door.handle = handle;

		// Closed doors also block pathfinding, through the polys the nav builder cut out under them.
		AABB localBounds;
		if (m_mapData->GetDynamicCollisionLocalBounds(handle, localBounds))
		{
			door.navObstacle = m_mapData->AddNavObstacle(nav::ComputeDoorVolume(localBounds, transform), !object.IsOpen());
		}

		if (door.navObstacle == 0 && m_warnedDoorsWithoutNav.insert(object.Get<uint32>(object_fields::Entry)).second)
		{
			WLOG("Door object entry " << object.Get<uint32>(object_fields::Entry) << " on map " << m_mapId
				<< " has no door polys in the nav mesh — creatures can path through it while it is closed. "
				"Rebuild the map's nav mesh with nav_builder --project");
		}

		const uint64 navObstacle = door.navObstacle;
		door.stateChanged = object.stateChanged.connect([this, handle, navObstacle](GameWorldObjectS&, const uint32 newState)
		{
			// Closed (state 0) doors block line of sight and paths.
			m_mapData->SetDynamicCollisionEnabled(handle, newState == 0);
			m_mapData->SetNavObstacleBlocking(navObstacle, newState == 0);
		});
```

This replaces the existing `DoorCollision& door = ...` / `stateChanged` block. Check the entry field name in `object_fields`; it is the one `GameObjectS::GetEntry()` reads. Use `object.GetEntry()` if it exists.

Add a member to `WorldInstance`, next to `m_doorCollisions`:

```cpp
		/// Door entries already warned about for missing nav polys, so respawns do not repeat it.
		std::unordered_set<uint32> m_warnedDoorsWithoutNav;
```

In `UnregisterDoorCollision`, next to `RemoveDynamicCollision`:

```cpp
			m_mapData->RemoveNavObstacle(it->second.navObstacle);
```

- [ ] **Step 6: Build and run everything**

Run:
```bash
cmake --build build --config Debug -t all_tests world_server
cd build && ctest -C Debug --output-on-failure
```
Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_server src/tests/game_server_tests
git commit -m "world: closed doors block pathfinding per instance"
```

---

### Task 7: Rebuild nav data with doors

**Files:**
- Modify: `data/editor/nav/Test/*.nav`, `data/editor/nav/Test.map` (submodule `data/editor`), plus any other map with Door spawns
- Modify: `docs/hollow_choir_bosses.md:221` (the rebuild command)
- Modify: `.agents/skills/terrain-author/references/pitfalls.md:32-34` (the rebuild command)

- [ ] **Step 1: Rebuild the Hollow Choir map into the real data folder**

Run:
```bash
./bin/Release/nav_builder -d data/client -w Test -o data/editor -p data/editor/data
```
Expected: `Baking N door(s)` with N ≥ 5, then `Finished`, exit 0.

- [ ] **Step 2: Find the other maps with doors**

Run the builder for each remaining map with nav data into the scratchpad, and read the door count:
```bash
for w in Collision Demo Development; do ./bin/Release/nav_builder -d data/client -w $w -o "$SCRATCH/navcheck" -p data/editor/data 2>&1 | grep "Baking"; done
```
For each map with a non-zero count, rebuild it into `data/editor` as in Step 1. Maps with 0 doors stay untouched.

- [ ] **Step 3: Verify in a dev world server**

Start the dev stack and enter Hollow Choir (map 1). In the world server log, check that no `has no door polys in the nav mesh` warning appears for the G2 seals.

- [ ] **Step 4: Update the rebuild commands in the docs**

In both doc files, change the command to `bin/Release/nav_builder.exe -d data/client -w <World> -o data/editor -p data/editor/data`. Add one sentence: "`-p` bakes door spawns into the nav mesh; without it closed doors do not block creature paths."

- [ ] **Step 5: Commit the data submodule, then the pointer**

```bash
git -C data/editor checkout -b feature/nav-door-obstacles
git -C data/editor add nav
git -C data/editor commit -m "nav: rebuild with door polys"
git add data/editor docs/hollow_choir_bosses.md .agents/skills/terrain-author/references/pitfalls.md
git commit -m "data: nav meshes with baked door polys"
```

Do not push the submodule. `/ship` handles data submodules per the repo's workflow; flag it to the user if it does not.

---

### Task 8: E2E — the barrier holds against a dragged mob

**Files:**
- Create: `e2e/scenarios/hollow_choir_barrier_blocks_paths.lua`

**Interfaces:**
- Consumes: E2E API `GM.Worldport`, `GM.Godmode`, `GM.CreateMonster(entry)` (spawns at the player and returns its guid string), `MoveTo`, `TargetUnit`, `StartAttack`, `StopAttack`, `GetPosX`, `GetHealth`, `GetMaxHealth`, `FindUnitByEntry`, `WaitUntil`, `Sleep`, `Assert`, `Log`, `GM.DestroyMonster`

- [ ] **Step 1: Write the scenario**

```lua
-- e2e-own-character: hcb
-- e2e-timeout: 240
--
-- Hollow Choir G2 barrier (bug repro): while Sister Mereth lives, the magic barriers at x -85.5
-- must keep creatures on the crypt side. Dragging a hostile mob to the barrier used to let it path
-- through (the nav mesh did not know about doors), where Cantor Veyr assisted it and could be
-- pulled without Mereth ever dying.

local VEYR, HOLLOW_CHORISTER = 88, 91
local BARRIER_X = -85.5
local CRYPT_SIDE_X, BARRIER_Y, BARRIER_Z = -98.0, 5.7, 0.0

GM.Godmode(true)
GM.Worldport(1, CRYPT_SIDE_X, BARRIER_Y, BARRIER_Z, 0)
Assert(WaitUntil(function() return GetPosX(Me()) < -90 end, 30000, "at the G2 barrier"),
	"the worldport next to the G2 barrier should land")
Sleep(3000)
GM.Godmode(true)

local veyr = nil
Assert(WaitUntil(function() veyr = FindUnitByEntry(VEYR); return veyr ~= nil end, 30000, "Veyr visible"),
	"Cantor Veyr should be spawned behind the barrier")
local veyrMax = GetMaxHealth(veyr)
Assert(WaitUntil(function() return GetHealth(veyr) >= veyrMax end, 30000, "Veyr at full health"),
	"Veyr should start untouched")

-- A hostile Veyr ally, engaged on the crypt side.
local mob = GM.CreateMonster(HOLLOW_CHORISTER)
TargetUnit(mob)
StartAttack(mob)
Assert(WaitUntil(function() return GetHealth(mob) < GetMaxHealth(mob) end, 15000, "mob engaged"),
	"auto attacks should put the chorister in combat with us")

-- Stand right at the barrier, so the mob's chase slots fall on the far side, and hold there.
MoveTo(BARRIER_X - 1.0, BARRIER_Y, BARRIER_Z, 15000)
local crossed = false
local deadline = 20
for _ = 1, deadline do
	Sleep(1000)
	if GetPosX(mob) > BARRIER_X + 0.3 then
		crossed = true
		break
	end
end
StopAttack()

Assert(not crossed, "the chorister must not path through the closed G2 barrier (x " .. GetPosX(mob) .. ")")
Assert(GetHealth(veyr) >= veyrMax, "Veyr must not be pulled through the closed barrier")
Log("G2 barrier held for " .. deadline .. " s")

GM.DestroyMonster(mob)
```

Check `GM.CreateMonster` in `src/e2e_client/scenario_engine.cpp:1147`. It returns the guid as a string, which is what the other scenarios pass to `TargetUnit`. If Veyr sits out of view range from x -98, take `FindUnitByEntry` out of the hard asserts and assert `not crossed` only. Note this in the report.

- [ ] **Step 2: Prove the scenario catches the bug**

Before relying on the scenario, show that it fails without the fix. Temporarily replace `&m_queryFilter` with `nullptr` in `NavMapData::CalculatePath` (both calls), rebuild `world_server`, and run:
```powershell
powershell -File tools/e2e/e2e_run.ps1 -Scenario hollow_choir_barrier_blocks_paths
```
Expected: FAIL on "must not path through". Check `e2e/README.md` for the exact single-scenario flag of `e2e_run.ps1`. If the bug does not reproduce within 20 s, raise `deadline` to 40 and move the player along the barrier (`MoveTo` to z ±3) during the loop, as the manual repro does.

Revert the temporary change and confirm `git diff src/shared/game_server` shows nothing from it.

- [ ] **Step 3: Run with the fix**

Run:
```powershell
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
powershell -File tools/e2e/e2e_run.ps1 -Scenario hollow_choir_barrier_blocks_paths
powershell -File tools/e2e/e2e_run.ps1 -Scenario hollow_choir_mereth_veyr_encounter
```
Expected: both PASS. The second shows the barriers still open after Mereth dies.

- [ ] **Step 4: Commit**

```bash
git add e2e/scenarios/hollow_choir_barrier_blocks_paths.lua
git commit -m "e2e: G2 barrier keeps dragged mobs on the crypt side"
```

---

### Task 9: Docs and gate

**Files:**
- Modify: `docs/threading.md`
- Modify: `CLAUDE.md` (Server Threading → "Realm and world servers" bullet, and the nav mesh streaming bullet)
- Modify: `docs/hollow_choir_bosses.md:262-271`

- [ ] **Step 1: Update the threading docs**

In `CLAUDE.md`, replace the "Nav mesh streaming (world server)" bullet's first sentence with:

```markdown
- **Nav mesh streaming (world server)** — nav mesh, static collision and water are loaded once per
  map (`MapStaticData`, handed out by `WorldInstanceManager`'s `MapStaticDataRegistry`) and shared by
  every instance of it; the first instance of a map starts a short-lived `nav::PageStreamer` thread
  that only reads page files through the asset registry, and the registry installs pages in 4 ms
  slices per tick, once per map. All instances share one `dtNavMeshQuery`, so nav queries are world
  thread only. Door state lives per instance in a `nav::InstanceQueryFilter`.
```

Keep the rest of the bullet (EnsureNavLoaded, the reader thread must not log). In the "Realm and world servers" bullet, append: "Raising the world tier's thread count also needs a `dtNavMeshQuery` per thread, because instances share one per map."

In `docs/threading.md`, find the server/nav section and add the same two facts: one query per map, shared; and the filter per instance.

- [ ] **Step 2: Update the Hollow Choir doc**

In `docs/hollow_choir_bosses.md` (the G2 seal paragraph ~262-271), add: "Closed seals also block creature pathfinding: `nav_builder -p` cuts their footprint out as door polys, and each instance blocks them while the seal is closed. Moving a seal means rebuilding the map's nav mesh."

- [ ] **Step 3: Run the gate**

Run: `/gate` (fast tier: protocol check, build, unit tests, tool tests, Linux build in WSL).
Expected: green. If the Linux build fails on the Detour define, check that `deps/CMakeLists.txt` sets it with `FORCE` before `add_subdirectory`, and that no target compiles Detour sources directly.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs
git commit -m "docs: shared static map data and door nav obstacles"
```
