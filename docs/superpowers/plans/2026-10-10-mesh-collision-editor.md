# Mesh Collision Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give mmo_edit's mesh editor a collision mode with five shape types: Box, Wedge, Cylinder, Helix ramp and Plane. It shows the collision as a walkability-coloured overlay. You can add, cut and transform collision shapes with the gizmo, bake them into the mesh's existing `AABBTree`, and persist them in an editor-only chunk.

**Architecture:** The pure shape, recipe and bake logic lives in `src/shared/math/`. It is headless, so it builds on Linux and is tested in `math_tests`. The `.hmsh` format moves to 0x0302, gains an editor-only `CSRC` chunk that runtime readers skip, and from that version on unknown chunks are tolerated. The editor side is a self-contained `MeshCollisionEditor` component plus a reusable `CollisionOverlay`. `MeshEditorInstance` only forwards to them. The baked `COLL` tree format is unchanged, so the client, server LOS and nav_build need no change.

**Tech Stack:** C++17, Catch2, ImGui, the engine's `scene_graph` (`ManualRenderObject`), mmo_edit `TransformWidget`/`Selection`.

**Spec:** `docs/superpowers/specs/2026-10-10-mesh-collision-editor-design.md`

## Global Constraints

- Code style is Allman braces, braces on every `if`, tabs, `m_camelCase` members, PascalCase methods, `snake_case` files and `#pragma once`.
- Every source file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- Public members get Doxygen comments.
- No exceptions. Use `ASSERT`/`WLOG`/`ELOG` from `base/macros.h` / `log/default_log_levels.h`.
- Enum pseudo-namespaces follow the project pattern, e.g. `namespace collision_shape_type { enum Type : uint8 { ... }; }`.
- Mesh version `Version_0_3_2 = 0x0302`, and `mesh_version::Latest` maps to it.
- The editor-only chunk magic is `CSRC`, written only when a recipe exists and the version is ≥ 0x0302.
- The recipe has its own `uint32` version, `CollisionRecipeVersion = 1`. Readers fail on any other value.
- Walkable means a face normal with `|normal.y| ≥ 0.71` (`CollisionWalkableFloorY`).
  - This matches the client's `SetWalkableFloorY(0.71f)`.
  - Use the absolute value because the client capsule (`CapsuleTriangleIntersection`) and nav_build treat collision as two-sided.
- Shape triangles are outward-wound: the outward normal is `(b - a).Cross(c - a)`, the same expression every consumer uses.
- No wire-protocol change and no `ProtocolVersion` bump. `COLL`/BVH2 stays byte-identical.
- Engine and content tooling stays in C++ in mmo_edit. No Python tools.
- New `.cpp` files are globbed (`add_lib`, `add_gui_exe_recurse`, `mmo_add_test`). Re-run the CMake configure after adding files, or they are not built.
- Do not touch the hollow-choir worktree or its uncommitted `stair_ramp` files. Never push.

## Review Focus

1. **Degenerate numeric input** in the shape panel: zero or negative scale, `segments` 0/1, helix sweep 0 or 7200°, inner radius ≥ 1. Expect it to be clamped by `SanitizeCollisionShape`, never NaN, never inverted winding. Tests: Task 1 Step 1 (`sanitize`) and Task 2 Step 1 (helix sweep and inner-radius clamps).
2. **A mesh re-imported with fewer submeshes than its recipe references** (included ids or `surfaceSubMesh` ≥ count). Expect the invalid ids to be dropped or clamped by `SanitizeCollisionRecipe`, not an out-of-range read. Test: Task 3 Step 1.
3. **A corrupt or truncated `CSRC` chunk**:
   - the runtime still loads the mesh with the same tree;
   - `ReadMeshCollisionRecipe` reports `Corrupt`, not `Absent`, and does not crash.

   Tests: Task 5 Step 1.
4. **Nothing to bake**: no render geometry ticked and no Add shapes, or every render face cut. Expect an empty result and a *cleared* tree, not `Build` on empty input, which would serialize bogus nodes. Tests: Task 4 Step 1 (empty bake), plus the guard in `RebuildMeshCollision` (Task 6).
5. **A legacy mesh with existing collision**, opened and saved without touching collision. Expect no `CSRC` written. The first edit infers the included submeshes from the BVH2 face ids, so the tree does not change. Tests: Task 3 Step 1 (`InferIncludedSubMeshes`); Task 6's save path returns `nullptr` until the recipe is touched.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/shared/math/collision_shape.h/.cpp` (new) | Shape types, sanitising, transform, tessellation, point-inside test, walkability test |
| `src/shared/math/collision_recipe.h/.cpp` (new) | `CollisionRecipe`, versioned serialization, `InferIncludedSubMeshes`, `SanitizeCollisionRecipe` |
| `src/shared/math/collision_bake.h/.cpp` (new) | `BakeCollision`: render faces − Cut volumes + Add shapes |
| `src/shared/scene_graph/mesh_serializer.h/.cpp` (modify) | Version 0x0302, `CSRC` write and skip, `ReadMeshCollisionRecipe` |
| `src/tests/math_tests/test_collision_shape.cpp`, `test_collision_recipe.cpp`, `test_collision_bake.cpp` (new) | Pure logic tests |
| `src/tests/scene_graph_tests/test_mesh_collision_recipe_chunk.cpp` (new) | Mesh file format tests |
| `src/mmo_edit/editors/mesh_editor/mesh_collision_geometry.h/.cpp` (new) | Gather render triangles from a `Mesh`, `RebuildMeshCollision`, `RebakeMeshCollisionFile` (the CLI job) |
| `src/mmo_edit/editors/mesh_editor/collision_overlay.h/.cpp` (new) | `ManualRenderObject` overlay: coloured translucent faces plus wireframe; reusable by the world model editor later |
| `src/mmo_edit/editors/mesh_editor/mesh_collision_editor.h/.cpp` (new) | Recipe state, panel UI, picking, gizmo, `SelectedCollisionShape` |
| `src/mmo_edit/editors/mesh_editor/mesh_editor_instance.h/.cpp` (modify) | Own a `MeshCollisionEditor`, forward render, mouse, keys and save; drop the inline gather code |
| `src/mmo_edit/mmo_edit.cpp` (modify) | `--rebake-collision` job |

---

### Task 0: Worktree setup (no commit)

The worktree has no `build/` directory, and its submodules are not initialised.

- [ ] **Step 1: Init submodules**

```bash
cd /h/mmo/.claude/worktrees/nostalgic-kalam-126533
git -c protocol.file.allow=always submodule update --init
```
Expected: `deps/*`, `data/client` and `data/editor` are checked out.

- [ ] **Step 2: Configure, mirroring the main checkout's options**

```bash
grep -E "^MMO_|CMAKE_GENERATOR:" /h/mmo/build/CMakeCache.txt
```
Then run, adding every `MMO_*` value printed above as `-D<name>=<value>`:
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```
Expected: `-- Generating done`.

- [ ] **Step 3: Baseline build of the test suites**

```powershell
cmake --build build --config Debug -t math_tests scene_graph_tests --parallel
```
Expected: the build succeeds.

---

### Task 1: Collision shapes — Box, Wedge, Plane, and the shared machinery

**Files:**
- Create: `src/shared/math/collision_shape.h`, `src/shared/math/collision_shape.cpp`
- Test: `src/tests/math_tests/test_collision_shape.cpp`

**Interfaces:**
- Produces (exact):
  - `collision_shape_type::Type` (`Box, Wedge, Cylinder, HelixRamp, Plane, Count_`) and `collision_shape_op::Type` (`Add, Cut`).
  - `constexpr float CollisionWalkableFloorY = 0.71f;`
  - `struct CollisionShape` (fields below).
  - Functions:
    - `CollisionShape SanitizeCollisionShape(const CollisionShape&)`
    - `bool CollisionShapeSupportsCut(collision_shape_type::Type)`
    - `const char* GetCollisionShapeTypeName(collision_shape_type::Type)`
    - `Matrix4 GetCollisionShapeTransform(const CollisionShape&)`
    - `void TessellateCollisionShape(const CollisionShape&, std::vector<Vector3>&, std::vector<uint32>&)` (appends)
    - `bool IsPointInsideCollisionShape(const CollisionShape&, const Vector3&)`
    - `bool IsCollisionFaceWalkable(const Vector3& a, const Vector3& b, const Vector3& c)`

- [ ] **Step 1: Write the failing tests**

`src/tests/math_tests/test_collision_shape.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_shape.h"
#include "math/aabb_tree.h"
#include "math/ray.h"

#include <cmath>
#include <map>
#include <tuple>

using namespace mmo;

namespace
{
	struct TriangleSoup
	{
		std::vector<Vector3> vertices;
		std::vector<uint32> indices;
	};

	TriangleSoup Tessellate(const CollisionShape& shape)
	{
		TriangleSoup soup;
		TessellateCollisionShape(shape, soup.vertices, soup.indices);
		return soup;
	}

	typedef std::tuple<long, long, long> PositionKey;

	PositionKey KeyOf(const Vector3& p)
	{
		return { std::lround(p.x * 1000.0f), std::lround(p.y * 1000.0f), std::lround(p.z * 1000.0f) };
	}

	/// Closed and consistently wound: every directed edge (by position) occurs exactly once and its
	/// reverse occurs exactly once.
	bool IsClosedAndConsistentlyWound(const TriangleSoup& soup)
	{
		std::map<std::pair<PositionKey, PositionKey>, int> directed;
		for (size_t f = 0; f + 2 < soup.indices.size(); f += 3)
		{
			for (int e = 0; e < 3; ++e)
			{
				const Vector3& a = soup.vertices[soup.indices[f + e]];
				const Vector3& b = soup.vertices[soup.indices[f + (e + 1) % 3]];
				++directed[{ KeyOf(a), KeyOf(b) }];
			}
		}

		for (const auto& [edge, count] : directed)
		{
			if (count != 1)
			{
				return false;
			}

			const auto reverse = directed.find({ edge.second, edge.first });
			if (reverse == directed.end() || reverse->second != 1)
			{
				return false;
			}
		}

		return !directed.empty();
	}

	/// For convex shapes: every face normal points away from a known interior point.
	bool AllFacesPointAwayFrom(const TriangleSoup& soup, const Vector3& interior)
	{
		for (size_t f = 0; f + 2 < soup.indices.size(); f += 3)
		{
			const Vector3& a = soup.vertices[soup.indices[f]];
			const Vector3& b = soup.vertices[soup.indices[f + 1]];
			const Vector3& c = soup.vertices[soup.indices[f + 2]];
			const Vector3 normal = (b - a).Cross(c - a);
			const Vector3 centroid = (a + b + c) / 3.0f;
			if (normal.Dot(centroid - interior) <= 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	AABB BoundsOf(const TriangleSoup& soup)
	{
		AABB box(soup.vertices.front(), soup.vertices.front());
		for (const auto& v : soup.vertices)
		{
			box.Combine(v);
		}
		return box;
	}

	CollisionShape MakeShape(collision_shape_type::Type type, const Vector3& position, const Vector3& scale)
	{
		CollisionShape shape;
		shape.type = type;
		shape.position = position;
		shape.scale = scale;
		return shape;
	}
}

TEST_CASE("Box shape is a closed outward box of the given size", "[collision_shape]")
{
	const CollisionShape box = MakeShape(collision_shape_type::Box, Vector3(1.0f, 2.0f, 3.0f), Vector3(2.0f, 4.0f, 6.0f));
	const TriangleSoup soup = Tessellate(box);

	CHECK(soup.indices.size() == 36);
	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, box.position));

	const AABB bounds = BoundsOf(soup);
	CHECK(bounds.min.x == Approx(0.0f));
	CHECK(bounds.max.x == Approx(2.0f));
	CHECK(bounds.min.y == Approx(0.0f));
	CHECK(bounds.max.y == Approx(4.0f));
	CHECK(bounds.min.z == Approx(0.0f));
	CHECK(bounds.max.z == Approx(6.0f));
}

TEST_CASE("Rotated box is still outward and front faces block an outside ray", "[collision_shape]")
{
	CollisionShape box = MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(2.0f, 2.0f, 2.0f));
	box.rotation = Quaternion(Degree(30.0f), Vector3::UnitY);
	const TriangleSoup soup = Tessellate(box);

	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, Vector3::Zero));

	AABBTree tree;
	tree.Build(soup.vertices, soup.indices);
	Ray ray(Vector3(0.0f, 10.0f, 0.0f), Vector3(0.0f, -10.0f, 0.0f));
	REQUIRE(tree.IntersectRay(ray, nullptr, raycast_flags::IgnoreBackface));
	const Vector3 hit = ray.origin + (ray.destination - ray.origin) * ray.hitDistance;
	CHECK(hit.y == Approx(1.0f));
}

TEST_CASE("Wedge rises along local +Z and is closed", "[collision_shape]")
{
	const CollisionShape wedge = MakeShape(collision_shape_type::Wedge, Vector3::Zero, Vector3(2.0f, 1.0f, 4.0f));
	const TriangleSoup soup = Tessellate(wedge);

	CHECK(soup.indices.size() == 24);
	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, Vector3(0.0f, -0.25f, 1.0f)));

	AABBTree tree;
	tree.Build(soup.vertices, soup.indices);

	// Halfway up the ramp (z = 0) the top is at y = 0; three quarters up (z = 1) at y = 0.25.
	Ray mid(Vector3(0.0f, 5.0f, 0.0f), Vector3(0.0f, -5.0f, 0.0f));
	REQUIRE(tree.IntersectRay(mid, nullptr, raycast_flags::IgnoreBackface));
	CHECK((mid.origin + (mid.destination - mid.origin) * mid.hitDistance).y == Approx(0.0f).margin(1e-4));

	Ray upper(Vector3(0.0f, 5.0f, 1.0f), Vector3(0.0f, -5.0f, 1.0f));
	REQUIRE(tree.IntersectRay(upper, nullptr, raycast_flags::IgnoreBackface));
	CHECK((upper.origin + (upper.destination - upper.origin) * upper.hitDistance).y == Approx(0.25f).margin(1e-4));
}

TEST_CASE("Plane is one quad facing +Y, two-sided adds the reverse face", "[collision_shape]")
{
	CollisionShape plane = MakeShape(collision_shape_type::Plane, Vector3(0.0f, 1.0f, 0.0f), Vector3(4.0f, 1.0f, 4.0f));
	TriangleSoup soup = Tessellate(plane);
	REQUIRE(soup.indices.size() == 6);
	const Vector3 n = (soup.vertices[soup.indices[1]] - soup.vertices[soup.indices[0]]).Cross(soup.vertices[soup.indices[2]] - soup.vertices[soup.indices[0]]);
	CHECK(n.y > 0.0f);

	AABBTree oneSided;
	oneSided.Build(soup.vertices, soup.indices);
	Ray fromBelow(Vector3(0.0f, -5.0f, 0.0f), Vector3(0.0f, 5.0f, 0.0f));
	CHECK_FALSE(oneSided.IntersectRay(fromBelow, nullptr, raycast_flags::IgnoreBackface));

	plane.twoSided = true;
	soup = Tessellate(plane);
	CHECK(soup.indices.size() == 12);
	AABBTree twoSided;
	twoSided.Build(soup.vertices, soup.indices);
	Ray fromBelowAgain(Vector3(0.0f, -5.0f, 0.0f), Vector3(0.0f, 5.0f, 0.0f));
	CHECK(twoSided.IntersectRay(fromBelowAgain, nullptr, raycast_flags::IgnoreBackface));
}

TEST_CASE("Point inside box and wedge respects rotation and scale", "[collision_shape]")
{
	CollisionShape box = MakeShape(collision_shape_type::Box, Vector3(10.0f, 0.0f, 0.0f), Vector3(4.0f, 1.0f, 1.0f));
	box.rotation = Quaternion(Degree(90.0f), Vector3::UnitY);   // long axis now along Z
	CHECK(IsPointInsideCollisionShape(box, Vector3(10.0f, 0.0f, 1.5f)));
	CHECK_FALSE(IsPointInsideCollisionShape(box, Vector3(11.5f, 0.0f, 0.0f)));

	const CollisionShape wedge = MakeShape(collision_shape_type::Wedge, Vector3::Zero, Vector3(2.0f, 2.0f, 2.0f));
	CHECK(IsPointInsideCollisionShape(wedge, Vector3(0.0f, -0.5f, 0.5f)));    // under the slope
	CHECK_FALSE(IsPointInsideCollisionShape(wedge, Vector3(0.0f, 0.5f, -0.5f)));   // above the slope
}

TEST_CASE("Planes have no volume and do not support Cut", "[collision_shape]")
{
	const CollisionShape plane = MakeShape(collision_shape_type::Plane, Vector3::Zero, Vector3(4.0f, 4.0f, 4.0f));
	CHECK_FALSE(IsPointInsideCollisionShape(plane, Vector3::Zero));
	CHECK_FALSE(CollisionShapeSupportsCut(collision_shape_type::Plane));
	CHECK(CollisionShapeSupportsCut(collision_shape_type::Box));

	CollisionShape cutPlane = plane;
	cutPlane.op = collision_shape_op::Cut;
	CHECK(SanitizeCollisionShape(cutPlane).op == collision_shape_op::Add);
}

TEST_CASE("Sanitize clamps degenerate input", "[collision_shape]")
{
	CollisionShape shape = MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(0.0f, -2.0f, 1.0f));
	shape.segments = 0;
	const CollisionShape sane = SanitizeCollisionShape(shape);
	CHECK(sane.scale.x == Approx(0.001f));
	CHECK(sane.scale.y == Approx(2.0f));      // sign dropped: negative scale would invert the winding
	CHECK(sane.segments == 3);

	CHECK_FALSE(IsPointInsideCollisionShape(MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(0.0f, 1.0f, 1.0f)), Vector3(0.5f, 0.0f, 0.0f)));

	const TriangleSoup soup = Tessellate(MakeShape(collision_shape_type::Box, Vector3::Zero, Vector3(0.0f, 1.0f, 1.0f)));
	for (const auto& v : soup.vertices)
	{
		CHECK(std::isfinite(v.x));
	}
}

TEST_CASE("Walkability uses the absolute normal Y against 0.71", "[collision_shape]")
{
	// Flat, either winding: walkable.
	CHECK(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector3(1, 0, 0)));
	CHECK(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)));
	// 40 degrees: walkable (cos 40 = 0.766). 50 degrees: too steep (cos 50 = 0.643).
	const float t40 = std::tan(40.0f * 3.14159265f / 180.0f);
	const float t50 = std::tan(50.0f * 3.14159265f / 180.0f);
	CHECK(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, t40, 1), Vector3(1, 0, 0)));
	CHECK_FALSE(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, t50, 1), Vector3(1, 0, 0)));
	// Wall and degenerate: not walkable.
	CHECK_FALSE(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)));
	CHECK_FALSE(IsCollisionFaceWalkable(Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(1, 0, 0)));
}
```
`AABB::Combine(const Vector3&)` is assumed here. Check `src/shared/math/aabb.h` and, if it is named differently (e.g. `Merge`), use that name.

- [ ] **Step 2: Run the tests to verify they fail**

Re-run the configure (Task 0 Step 2) so the new file is globbed, then:
```powershell
cmake --build build --config Debug -t math_tests --parallel
```
Expected: compile error, `math/collision_shape.h` not found.

- [ ] **Step 3: Write the header**

`src/shared/math/collision_shape.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	namespace collision_shape_type
	{
		/// @brief Primitive types an authored collision shape can have.
		enum Type : uint8
		{
			/// @brief Cube from -0.5 to 0.5 on every local axis.
			Box,
			/// @brief Ramp: footprint -0.5..0.5 in X/Z, bottom at y=-0.5, top rising from y=-0.5 at -Z to y=0.5 at +Z.
			Wedge,
			/// @brief Capped cylinder around local +Y, radius 0.5, y from -0.5 to 0.5.
			Cylinder,
			/// @brief Spiral ramp band around local +Y between inner and outer radius, rising from y=-0.5 to 0.5.
			HelixRamp,
			/// @brief Quad -0.5..0.5 in X/Z at y=0 facing +Y. Has no volume.
			Plane,

			Count_
		};
	}

	namespace collision_shape_op
	{
		/// @brief What a shape does to the baked collision.
		enum Type : uint8
		{
			/// @brief The shape's triangles are added to the collision.
			Add,
			/// @brief Render-derived faces whose centroid lies inside the shape are removed.
			Cut
		};
	}

	/// @brief Lowest |normal.y| of a walkable face; matches the client's UnitMovement::SetWalkableFloorY default.
	constexpr float CollisionWalkableFloorY = 0.71f;

	/// @brief An authored collision primitive, placed in mesh space.
	struct CollisionShape
	{
		/// @brief Primitive type.
		collision_shape_type::Type type { collision_shape_type::Box };
		/// @brief Add or Cut.
		collision_shape_op::Type op { collision_shape_op::Add };
		/// @brief Display name.
		String name;
		/// @brief Mesh-space position of the primitive's local origin.
		Vector3 position { 0.0f, 0.0f, 0.0f };
		/// @brief Mesh-space orientation.
		Quaternion rotation { Quaternion::Identity };
		/// @brief Size of the unit primitive along its local axes.
		Vector3 scale { 1.0f, 1.0f, 1.0f };
		/// @brief Submesh id stored for the baked faces (footstep surface type).
		uint16 surfaceSubMesh { 0 };
		/// @brief Cylinder sides or helix segments over the whole sweep.
		uint16 segments { 24 };
		/// @brief Helix: inner radius as a fraction of the outer radius.
		float innerRadius { 0.25f };
		/// @brief Helix: total turn in degrees; may exceed 360.
		float sweepDegrees { 360.0f };
		/// @brief Helix: tread thickness as a fraction of the unit height.
		float thickness { 0.05f };
		/// @brief Helix: turns clockwise when seen from above (north = -Z) while rising.
		bool clockwise { false };
		/// @brief Plane: also add the reverse-wound face.
		bool twoSided { false };
	};

	/// @brief Returns a copy with every field clamped to a usable range (positive scale, sane segments, Add for planes).
	CollisionShape SanitizeCollisionShape(const CollisionShape& shape);

	/// @brief Whether a shape type has a volume and can therefore be used as a Cut shape.
	bool CollisionShapeSupportsCut(collision_shape_type::Type type);

	/// @brief Human-readable name of a shape type.
	const char* GetCollisionShapeTypeName(collision_shape_type::Type type);

	/// @brief Local-to-mesh transform of the (sanitized) shape.
	Matrix4 GetCollisionShapeTransform(const CollisionShape& shape);

	/// @brief Appends the shape's mesh-space triangles; outward normal is (b - a).Cross(c - a).
	void TessellateCollisionShape(const CollisionShape& shape, std::vector<Vector3>& vertices, std::vector<uint32>& indices);

	/// @brief Whether a mesh-space point lies inside the shape's volume. Always false for planes.
	bool IsPointInsideCollisionShape(const CollisionShape& shape, const Vector3& point);

	/// @brief Whether a face is walkable for the client: |normal.y| >= CollisionWalkableFloorY. Degenerate faces are not.
	bool IsCollisionFaceWalkable(const Vector3& a, const Vector3& b, const Vector3& c);
}
```

- [ ] **Step 4: Write the implementation (Box, Wedge, Plane; Cylinder and Helix stubs come in Task 2)**

`src/shared/math/collision_shape.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_shape.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr float MinScale = 0.001f;

		/// Collects local-space triangles, winding each so its normal agrees with an outward hint.
		class LocalTriangles final
		{
		public:
			void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& outward)
			{
				const Vector3 normal = (b - a).Cross(c - a);
				const uint32 base = static_cast<uint32>(m_vertices.size());
				m_vertices.push_back(a);
				if (normal.Dot(outward) >= 0.0f)
				{
					m_vertices.push_back(b);
					m_vertices.push_back(c);
				}
				else
				{
					m_vertices.push_back(c);
					m_vertices.push_back(b);
				}
				m_indices.push_back(base);
				m_indices.push_back(base + 1);
				m_indices.push_back(base + 2);
			}

			/// a, b, c, d in perimeter order.
			void AddQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& outward)
			{
				// Decide the winding once from the whole quad so both halves agree.
				const Vector3 normal = (b - a).Cross(c - a) + (c - a).Cross(d - a);
				if (normal.Dot(outward) >= 0.0f)
				{
					Push(a, b, c);
					Push(a, c, d);
				}
				else
				{
					Push(a, c, b);
					Push(a, d, c);
				}
			}

			void AppendTransformed(const Matrix4& transform, std::vector<Vector3>& vertices, std::vector<uint32>& indices) const
			{
				const uint32 base = static_cast<uint32>(vertices.size());
				for (const auto& v : m_vertices)
				{
					vertices.push_back(transform.TransformAffine(v));
				}
				for (const uint32 i : m_indices)
				{
					indices.push_back(base + i);
				}
			}

		private:
			void Push(const Vector3& a, const Vector3& b, const Vector3& c)
			{
				const uint32 base = static_cast<uint32>(m_vertices.size());
				m_vertices.push_back(a);
				m_vertices.push_back(b);
				m_vertices.push_back(c);
				m_indices.push_back(base);
				m_indices.push_back(base + 1);
				m_indices.push_back(base + 2);
			}

			std::vector<Vector3> m_vertices;
			std::vector<uint32> m_indices;
		};

		void BuildBox(LocalTriangles& out)
		{
			const float h = 0.5f;
			const Vector3 p000(-h, -h, -h), p100(h, -h, -h), p110(h, h, -h), p010(-h, h, -h);
			const Vector3 p001(-h, -h, h), p101(h, -h, h), p111(h, h, h), p011(-h, h, h);

			out.AddQuad(p100, p101, p111, p110, Vector3(1, 0, 0));
			out.AddQuad(p000, p010, p011, p001, Vector3(-1, 0, 0));
			out.AddQuad(p010, p110, p111, p011, Vector3(0, 1, 0));
			out.AddQuad(p000, p001, p101, p100, Vector3(0, -1, 0));
			out.AddQuad(p001, p011, p111, p101, Vector3(0, 0, 1));
			out.AddQuad(p000, p100, p110, p010, Vector3(0, 0, -1));
		}

		void BuildWedge(LocalTriangles& out)
		{
			const float h = 0.5f;
			const Vector3 b0(-h, -h, -h), b1(h, -h, -h), b2(h, -h, h), b3(-h, -h, h);
			const Vector3 t2(h, h, h), t3(-h, h, h);

			out.AddQuad(b0, b1, b2, b3, Vector3(0, -1, 0));     // bottom
			out.AddQuad(b3, b2, t2, t3, Vector3(0, 0, 1));      // high back face
			out.AddQuad(b0, t3, t2, b1, Vector3(0, 1, -1));     // slope
			out.AddTriangle(b1, b2, t2, Vector3(1, 0, 0));      // right side
			out.AddTriangle(b0, t3, b3, Vector3(-1, 0, 0));     // left side
		}

		void BuildPlane(LocalTriangles& out, const bool twoSided)
		{
			const float h = 0.5f;
			const Vector3 a(-h, 0, -h), b(h, 0, -h), c(h, 0, h), d(-h, 0, h);
			out.AddQuad(a, b, c, d, Vector3(0, 1, 0));
			if (twoSided)
			{
				out.AddQuad(a, b, c, d, Vector3(0, -1, 0));
			}
		}

		Vector3 ToLocal(const CollisionShape& shape, const Vector3& point)
		{
			const Vector3 rotated = shape.rotation.UnitInverse() * (point - shape.position);
			return Vector3(rotated.x / shape.scale.x, rotated.y / shape.scale.y, rotated.z / shape.scale.z);
		}
	}

	CollisionShape SanitizeCollisionShape(const CollisionShape& shape)
	{
		CollisionShape sane = shape;
		if (sane.type >= collision_shape_type::Count_)
		{
			sane.type = collision_shape_type::Box;
		}

		sane.scale = Vector3(
			std::max(std::abs(shape.scale.x), MinScale),
			std::max(std::abs(shape.scale.y), MinScale),
			std::max(std::abs(shape.scale.z), MinScale));

		sane.rotation.Normalize();
		sane.segments = static_cast<uint16>(std::clamp<int>(shape.segments, 3, 256));
		sane.innerRadius = std::clamp(shape.innerRadius, 0.05f, 0.95f);
		sane.sweepDegrees = std::clamp(shape.sweepDegrees, 1.0f, 3600.0f);
		sane.thickness = std::clamp(shape.thickness, 0.001f, 1.0f);

		if (!CollisionShapeSupportsCut(sane.type))
		{
			sane.op = collision_shape_op::Add;
		}

		return sane;
	}

	bool CollisionShapeSupportsCut(const collision_shape_type::Type type)
	{
		return type != collision_shape_type::Plane;
	}

	const char* GetCollisionShapeTypeName(const collision_shape_type::Type type)
	{
		switch (type)
		{
		case collision_shape_type::Box: return "Box";
		case collision_shape_type::Wedge: return "Wedge";
		case collision_shape_type::Cylinder: return "Cylinder";
		case collision_shape_type::HelixRamp: return "Helix Ramp";
		case collision_shape_type::Plane: return "Plane";
		default: return "Unknown";
		}
	}

	Matrix4 GetCollisionShapeTransform(const CollisionShape& shape)
	{
		const CollisionShape sane = SanitizeCollisionShape(shape);
		Matrix4 transform;
		transform.MakeTransform(sane.position, sane.scale, sane.rotation);
		return transform;
	}

	void TessellateCollisionShape(const CollisionShape& shape, std::vector<Vector3>& vertices, std::vector<uint32>& indices)
	{
		const CollisionShape sane = SanitizeCollisionShape(shape);

		LocalTriangles local;
		switch (sane.type)
		{
		case collision_shape_type::Box: BuildBox(local); break;
		case collision_shape_type::Wedge: BuildWedge(local); break;
		case collision_shape_type::Plane: BuildPlane(local, sane.twoSided); break;
		default: break;
		}

		local.AppendTransformed(GetCollisionShapeTransform(sane), vertices, indices);
	}

	bool IsPointInsideCollisionShape(const CollisionShape& shape, const Vector3& point)
	{
		const CollisionShape sane = SanitizeCollisionShape(shape);
		const Vector3 p = ToLocal(sane, point);
		const float h = 0.5f;

		switch (sane.type)
		{
		case collision_shape_type::Box:
			return std::abs(p.x) <= h && std::abs(p.y) <= h && std::abs(p.z) <= h;
		case collision_shape_type::Wedge:
			return std::abs(p.x) <= h && p.y >= -h && p.z <= h && p.y <= p.z;
		default:
			return false;
		}
	}

	bool IsCollisionFaceWalkable(const Vector3& a, const Vector3& b, const Vector3& c)
	{
		const Vector3 normal = (b - a).Cross(c - a);
		const float length = normal.GetLength();
		if (length < 1.0e-6f)
		{
			return false;
		}
		return std::abs(normal.y / length) >= CollisionWalkableFloorY;
	}
}
```
Notes:
- The test "zero-scale box: (0.5,0,0) not inside" works because the scale is clamped to 0.001 before dividing.
- Confirm `Quaternion::Normalize()` mutates the quaternion in place (`quaternion.h:232` returns float, so it does).

- [ ] **Step 5: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t math_tests --parallel
./bin/Debug/math_tests.exe "[collision_shape]"
```
Expected: all `[collision_shape]` tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/shared/math/collision_shape.h src/shared/math/collision_shape.cpp src/tests/math_tests/test_collision_shape.cpp
git commit -m "feat(math): authored collision shapes - box, wedge, plane"
```

---

### Task 2: Cylinder and Helix ramp

**Files:**
- Modify: `src/shared/math/collision_shape.cpp` (adds `BuildCylinder`, `BuildHelix`, and the inside tests)
- Test: `src/tests/math_tests/test_collision_shape.cpp` (append)

**Interfaces:**
- Consumes: everything from Task 1.
- Produces: no new symbols. `TessellateCollisionShape` and `IsPointInsideCollisionShape` now handle `Cylinder` and `HelixRamp`.

- [ ] **Step 1: Append failing tests**

```cpp
namespace
{
	float TopHeightAt(const TriangleSoup& soup, const float x, const float z)
	{
		AABBTree tree;
		tree.Build(soup.vertices, soup.indices);
		Ray ray(Vector3(x, 100.0f, z), Vector3(x, -100.0f, z));
		REQUIRE(tree.IntersectRay(ray, nullptr, raycast_flags::IgnoreBackface));
		return (ray.origin + (ray.destination - ray.origin) * ray.hitDistance).y;
	}
}

TEST_CASE("Cylinder is closed, outward and has the given size", "[collision_shape]")
{
	CollisionShape cylinder = MakeShape(collision_shape_type::Cylinder, Vector3(0.0f, 1.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
	cylinder.segments = 16;
	const TriangleSoup soup = Tessellate(cylinder);

	CHECK(soup.indices.size() == 16 * 4 * 3);   // 2 side triangles + 2 cap triangles per segment
	CHECK(IsClosedAndConsistentlyWound(soup));
	CHECK(AllFacesPointAwayFrom(soup, cylinder.position));
	CHECK(TopHeightAt(soup, 0.0f, 0.0f) == Approx(2.0f));

	CHECK(IsPointInsideCollisionShape(cylinder, Vector3(0.9f, 1.0f, 0.0f)));
	CHECK_FALSE(IsPointInsideCollisionShape(cylinder, Vector3(0.8f, 1.0f, 0.8f)));   // r = 1.13 > 1
	CHECK_FALSE(IsPointInsideCollisionShape(cylinder, Vector3(0.0f, 2.1f, 0.0f)));
}

TEST_CASE("Helix ramp is closed and its top rises linearly with the turn", "[collision_shape]")
{
	CollisionShape helix = MakeShape(collision_shape_type::HelixRamp, Vector3::Zero, Vector3(4.0f, 3.0f, 4.0f));
	helix.segments = 32;
	helix.innerRadius = 0.5f;      // inner radius 1 m, outer 2 m
	helix.sweepDegrees = 360.0f;
	const TriangleSoup soup = Tessellate(helix);

	CHECK(IsClosedAndConsistentlyWound(soup));

	// Counter-clockwise seen from above with north = -Z: angle 0 at +X, 90 degrees at -Z.
	// Probe at the middle of a segment (11.25 degrees each) on the band's mid radius, never on a
	// seam between quads and never at the 0/360 overlap where start and end share an angle.
	auto expectTopAt = [&soup](const float degrees, const float sideSign)
	{
		const float radians = degrees * 3.14159265f / 180.0f;
		const float x = 1.5f * std::cos(radians);
		const float z = -sideSign * 1.5f * std::sin(radians);
		CHECK(TopHeightAt(soup, x, z) == Approx(-1.5f + 3.0f * degrees / 360.0f).margin(0.01f));
	};
	expectTopAt(5.625f + 11.25f * 3, 1.0f);
	expectTopAt(5.625f + 11.25f * 8, 1.0f);
	expectTopAt(5.625f + 11.25f * 16, 1.0f);
	expectTopAt(5.625f + 11.25f * 24, 1.0f);

	// The treads of a 32-segment, 3 m high helix between 1 m and 2 m radius are walkable.
	for (size_t f = 0; f + 2 < soup.indices.size(); f += 3)
	{
		const Vector3& a = soup.vertices[soup.indices[f]];
		const Vector3& b = soup.vertices[soup.indices[f + 1]];
		const Vector3& c = soup.vertices[soup.indices[f + 2]];
		const Vector3 n = (b - a).Cross(c - a);
		if (n.y > 0.0f && n.GetLength() > 1e-6f && n.y / n.GetLength() > 0.5f)
		{
			CHECK(IsCollisionFaceWalkable(a, b, c));
		}
	}
}

TEST_CASE("Clockwise helix mirrors the turn direction", "[collision_shape]")
{
	CollisionShape helix = MakeShape(collision_shape_type::HelixRamp, Vector3::Zero, Vector3(4.0f, 3.0f, 4.0f));
	helix.segments = 32;
	helix.innerRadius = 0.5f;
	helix.clockwise = true;
	const TriangleSoup soup = Tessellate(helix);

	// Mid-segment probe near a quarter turn: clockwise seen from above puts it at +Z, not -Z.
	const float degrees = 5.625f + 11.25f * 8;
	const float radians = degrees * 3.14159265f / 180.0f;
	CHECK(TopHeightAt(soup, 1.5f * std::cos(radians), 1.5f * std::sin(radians)) == Approx(-1.5f + 3.0f * degrees / 360.0f).margin(0.01f));
}

TEST_CASE("Helix point-inside follows the band across several turns", "[collision_shape]")
{
	CollisionShape helix = MakeShape(collision_shape_type::HelixRamp, Vector3::Zero, Vector3(4.0f, 4.0f, 4.0f));
	helix.sweepDegrees = 720.0f;     // two turns over 4 m: 2 m per turn
	helix.thickness = 0.1f;          // 0.4 m thick
	// At angle 90 degrees the tread tops are at -2 + 4 * (90 / 720) = -1.5 and at -1.5 + 2 = 0.5.
	CHECK(IsPointInsideCollisionShape(helix, Vector3(0.0f, -1.6f, -1.25f)));
	CHECK(IsPointInsideCollisionShape(helix, Vector3(0.0f, 0.4f, -1.25f)));
	CHECK_FALSE(IsPointInsideCollisionShape(helix, Vector3(0.0f, -0.5f, -1.25f)));   // between the two turns
	CHECK_FALSE(IsPointInsideCollisionShape(helix, Vector3(0.0f, -1.6f, -0.2f)));    // inside the inner radius
}

TEST_CASE("Helix sanitising clamps sweep, segments and inner radius", "[collision_shape]")
{
	CollisionShape helix = MakeShape(collision_shape_type::HelixRamp, Vector3::Zero, Vector3(4.0f, 3.0f, 4.0f));
	helix.sweepDegrees = 0.0f;
	helix.segments = 1;
	helix.innerRadius = 1.5f;
	const CollisionShape sane = SanitizeCollisionShape(helix);
	CHECK(sane.sweepDegrees == Approx(1.0f));
	CHECK(sane.segments == 3);
	CHECK(sane.innerRadius == Approx(0.95f));
	CHECK(IsClosedAndConsistentlyWound(Tessellate(helix)));

	helix.sweepDegrees = 7200.0f;
	CHECK(SanitizeCollisionShape(helix).sweepDegrees == Approx(3600.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```powershell
cmake --build build --config Debug -t math_tests --parallel
./bin/Debug/math_tests.exe "[collision_shape]"
```
Expected: the new cylinder and helix cases fail (empty tessellation, `REQUIRE(IntersectRay)` fails).

- [ ] **Step 3: Implement**

In `collision_shape.cpp`'s anonymous namespace, add:
```cpp
		constexpr float TwoPi = 6.28318530718f;

		/// Unit-circle point for angle theta, turning counter-clockwise seen from above (north = -Z)
		/// unless `clockwise`.
		Vector3 HelixDirection(const float theta, const bool clockwise)
		{
			const float side = clockwise ? -1.0f : 1.0f;
			return Vector3(std::cos(theta), 0.0f, -side * std::sin(theta));
		}

		void BuildCylinder(LocalTriangles& out, const uint16 segments)
		{
			const float r = 0.5f;
			const Vector3 top(0, 0.5f, 0), bottom(0, -0.5f, 0);
			for (uint16 i = 0; i < segments; ++i)
			{
				const float a0 = TwoPi * i / segments;
				const float a1 = TwoPi * (i + 1) / segments;
				const Vector3 d0(std::cos(a0), 0, std::sin(a0)), d1(std::cos(a1), 0, std::sin(a1));
				const Vector3 b0 = d0 * r + bottom, b1 = d1 * r + bottom;
				const Vector3 t0 = d0 * r + top, t1 = d1 * r + top;
				const Vector3 outward = (d0 + d1) * 0.5f;

				out.AddQuad(b0, b1, t1, t0, outward);
				out.AddTriangle(top, t0, t1, Vector3(0, 1, 0));
				out.AddTriangle(bottom, b0, b1, Vector3(0, -1, 0));
			}
		}

		void BuildHelix(LocalTriangles& out, const CollisionShape& s)
		{
			const float outer = 0.5f;
			const float inner = 0.5f * s.innerRadius;
			const float sweep = s.sweepDegrees * TwoPi / 360.0f;
			const uint16 n = s.segments;

			auto topY = [&](const uint16 i) { return -0.5f + static_cast<float>(i) / n; };
			auto point = [&](const uint16 i, const float radius, const float y)
			{
				const Vector3 d = HelixDirection(sweep * i / n, s.clockwise);
				return Vector3(d.x * radius, y, d.z * radius);
			};

			for (uint16 i = 0; i < n; ++i)
			{
				const uint16 j = i + 1;
				const Vector3 ti0 = point(i, inner, topY(i)), to0 = point(i, outer, topY(i));
				const Vector3 ti1 = point(j, inner, topY(j)), to1 = point(j, outer, topY(j));
				const Vector3 bi0 = point(i, inner, topY(i) - s.thickness), bo0 = point(i, outer, topY(i) - s.thickness);
				const Vector3 bi1 = point(j, inner, topY(j) - s.thickness), bo1 = point(j, outer, topY(j) - s.thickness);
				const Vector3 radial = HelixDirection(sweep * (i + 0.5f) / n, s.clockwise);

				out.AddQuad(ti0, to0, to1, ti1, Vector3(0, 1, 0));    // tread
				out.AddQuad(bi0, bi1, bo1, bo0, Vector3(0, -1, 0));   // underside
				out.AddQuad(bo0, bo1, to1, to0, radial);              // outer wall
				out.AddQuad(bi0, ti0, ti1, bi1, -radial);             // inner wall
			}

			// End caps face against (start) and along (end) the direction of travel. The travel
			// direction is d/dtheta of HelixDirection: (-sin t, 0, -side * cos t).
			const float side = s.clockwise ? -1.0f : 1.0f;
			const Vector3 startTangent(0.0f, 0.0f, side);
			const Vector3 endTangent(-std::sin(sweep), 0.0f, -side * std::cos(sweep));

			out.AddQuad(point(0, inner, topY(0) - s.thickness), point(0, outer, topY(0) - s.thickness),
				point(0, outer, topY(0)), point(0, inner, topY(0)), startTangent);
			out.AddQuad(point(n, inner, topY(n) - s.thickness), point(n, outer, topY(n) - s.thickness),
				point(n, outer, topY(n)), point(n, inner, topY(n)), endTangent);
		}
```

Add the cases to the switch in `TessellateCollisionShape`:
```cpp
		case collision_shape_type::Cylinder: BuildCylinder(local, sane.segments); break;
		case collision_shape_type::HelixRamp: BuildHelix(local, sane); break;
```
Add these to the switch in `IsPointInsideCollisionShape`:
```cpp
		case collision_shape_type::Cylinder:
			return std::abs(p.y) <= h && p.x * p.x + p.z * p.z <= h * h;
		case collision_shape_type::HelixRamp:
		{
			const float radiusSq = p.x * p.x + p.z * p.z;
			const float inner = h * sane.innerRadius;
			if (radiusSq < inner * inner || radiusSq > h * h)
			{
				return false;
			}

			const float side = sane.clockwise ? -1.0f : 1.0f;
			float phi = std::atan2(-side * p.z, p.x);
			if (phi < 0.0f)
			{
				phi += TwoPi;
			}

			const float sweep = sane.sweepDegrees * TwoPi / 360.0f;
			for (float theta = phi; theta <= sweep; theta += TwoPi)
			{
				const float top = -h + theta / sweep;
				if (p.y <= top && p.y >= top - sane.thickness)
				{
					return true;
				}
			}
			return false;
		}
```
`TwoPi` must be declared before `ToLocal` and the helper functions, inside the anonymous namespace.

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t math_tests --parallel
./bin/Debug/math_tests.exe "[collision_shape]"
```
Expected: all pass. If `IsClosedAndConsistentlyWound` fails for the helix, check that the end caps use the same four corner positions as the first and last segment's walls. They do when built with `point(0, ...)` and `point(n, ...)`.

- [ ] **Step 5: Commit**

```bash
git add src/shared/math/collision_shape.cpp src/tests/math_tests/test_collision_shape.cpp
git commit -m "feat(math): cylinder and helix ramp collision shapes"
```

---

### Task 3: Collision recipe

**Files:**
- Create: `src/shared/math/collision_recipe.h`, `src/shared/math/collision_recipe.cpp`
- Test: `src/tests/math_tests/test_collision_recipe.cpp`

**Interfaces:**
- Consumes: `CollisionShape`, `SanitizeCollisionShape` (Task 1).
- Produces:
  - `struct CollisionRecipe { bool useRenderGeometry; std::vector<uint16> includedSubMeshes; std::vector<CollisionShape> shapes; }`
  - `constexpr uint32 CollisionRecipeVersion = 1;`
  - `io::Writer& operator<<(io::Writer&, const CollisionRecipe&)` and `io::Reader& operator>>(io::Reader&, CollisionRecipe&)`
  - `std::vector<uint16> InferIncludedSubMeshes(const std::vector<uint16>& faceSubMeshes)`
  - `void SanitizeCollisionRecipe(CollisionRecipe&, uint16 subMeshCount)`

- [ ] **Step 1: Write the failing tests**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_recipe.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

using namespace mmo;

namespace
{
	CollisionRecipe MakeRecipe()
	{
		CollisionRecipe recipe;
		recipe.useRenderGeometry = true;
		recipe.includedSubMeshes = { 0, 2 };

		CollisionShape helix;
		helix.type = collision_shape_type::HelixRamp;
		helix.op = collision_shape_op::Add;
		helix.name = "Spiral";
		helix.position = Vector3(1.0f, 2.0f, 3.0f);
		helix.rotation = Quaternion(Degree(45.0f), Vector3::UnitY);
		helix.scale = Vector3(4.0f, 3.0f, 4.0f);
		helix.surfaceSubMesh = 2;
		helix.segments = 48;
		helix.innerRadius = 0.3f;
		helix.sweepDegrees = 540.0f;
		helix.thickness = 0.1f;
		helix.clockwise = true;

		CollisionShape cut;
		cut.type = collision_shape_type::Cylinder;
		cut.op = collision_shape_op::Cut;
		cut.name = "Steps";

		CollisionShape plane;
		plane.type = collision_shape_type::Plane;
		plane.twoSided = true;

		recipe.shapes = { helix, cut, plane };
		return recipe;
	}
}

TEST_CASE("Collision recipe survives a serialization round trip", "[collision_recipe]")
{
	const CollisionRecipe recipe = MakeRecipe();

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << recipe;

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe loaded;
	reader >> loaded;
	REQUIRE(reader);

	CHECK(loaded.useRenderGeometry);
	CHECK(loaded.includedSubMeshes == std::vector<uint16>{ 0, 2 });
	REQUIRE(loaded.shapes.size() == 3);

	const CollisionShape& h = loaded.shapes[0];
	CHECK(h.type == collision_shape_type::HelixRamp);
	CHECK(h.name == "Spiral");
	CHECK(h.position.y == Approx(2.0f));
	CHECK(h.rotation.w == Approx(recipe.shapes[0].rotation.w));
	CHECK(h.rotation.y == Approx(recipe.shapes[0].rotation.y));
	CHECK(h.scale.x == Approx(4.0f));
	CHECK(h.surfaceSubMesh == 2);
	CHECK(h.segments == 48);
	CHECK(h.innerRadius == Approx(0.3f));
	CHECK(h.sweepDegrees == Approx(540.0f));
	CHECK(h.thickness == Approx(0.1f));
	CHECK(h.clockwise);

	CHECK(loaded.shapes[1].op == collision_shape_op::Cut);
	CHECK(loaded.shapes[2].twoSided);
}

TEST_CASE("Collision recipe with an unknown version fails to read", "[collision_recipe]")
{
	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << io::write<uint32>(CollisionRecipeVersion + 1);

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe loaded;
	reader >> loaded;
	CHECK_FALSE(reader);
}

TEST_CASE("Collision recipe with an invalid shape type fails to read", "[collision_recipe]")
{
	CollisionRecipe recipe = MakeRecipe();
	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << recipe;

	// Shape type is the first byte after: version(4) + useRender(1) + count(4) + 2 ids(4) + shape count(4).
	buffer[4 + 1 + 4 + 4 + 4] = static_cast<char>(collision_shape_type::Count_);

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe loaded;
	reader >> loaded;
	CHECK_FALSE(reader);
}

TEST_CASE("Truncated collision recipe fails to read", "[collision_recipe]")
{
	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << MakeRecipe();
	buffer.resize(buffer.size() / 2);

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe loaded;
	reader >> loaded;
	CHECK_FALSE(reader);
}

TEST_CASE("Included submeshes are inferred from a tree's face submesh ids", "[collision_recipe]")
{
	CHECK(InferIncludedSubMeshes({ 3, 0, 3, 3, 1, 0 }) == std::vector<uint16>{ 0, 1, 3 });
	CHECK(InferIncludedSubMeshes({}).empty());
}

TEST_CASE("Sanitizing a recipe drops submesh ids the mesh no longer has", "[collision_recipe]")
{
	CollisionRecipe recipe = MakeRecipe();
	recipe.includedSubMeshes = { 0, 2, 5, 2 };
	recipe.shapes[0].surfaceSubMesh = 9;
	recipe.shapes[2].op = collision_shape_op::Cut;

	SanitizeCollisionRecipe(recipe, 3);

	CHECK(recipe.includedSubMeshes == std::vector<uint16>{ 0, 2 });
	CHECK(recipe.shapes[0].surfaceSubMesh == 0);
	CHECK(recipe.shapes[2].op == collision_shape_op::Add);

	SanitizeCollisionRecipe(recipe, 0);
	CHECK(recipe.includedSubMeshes.empty());
	CHECK(recipe.shapes[0].surfaceSubMesh == 0);
}
```
The byte offset in the invalid-type test assumes the layout in Step 3: version u32, useRender u8, `write_dynamic_range<uint32>` of u16 ids (4 + 2×2 bytes), then the u32 shape count, then the type byte.

- [ ] **Step 2: Run the tests to verify they fail**

Reconfigure, then build `math_tests`. Expected: `math/collision_recipe.h` not found.

- [ ] **Step 3: Implement**

`src/shared/math/collision_recipe.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/collision_shape.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

namespace mmo
{
	/// @brief Version of the serialized collision recipe; readers reject any other value.
	constexpr uint32 CollisionRecipeVersion = 1;

	/// @brief Editor-only description of how a mesh's collision tree is built.
	struct CollisionRecipe
	{
		/// @brief Whether the render triangles of the included submeshes feed the collision.
		bool useRenderGeometry { true };
		/// @brief Submeshes whose render triangles are collidable, sorted and unique.
		std::vector<uint16> includedSubMeshes;
		/// @brief Authored shapes, applied in order (all cuts first, then all adds).
		std::vector<CollisionShape> shapes;
	};

	/// @brief Writes a recipe, prefixed with CollisionRecipeVersion.
	io::Writer& operator<<(io::Writer& writer, const CollisionRecipe& recipe);

	/// @brief Reads a recipe; sets the reader's failure flag on an unknown version or invalid enum value.
	io::Reader& operator>>(io::Reader& reader, CollisionRecipe& recipe);

	/// @brief Sorted, distinct submesh ids of a tree's per-face submesh mapping.
	std::vector<uint16> InferIncludedSubMeshes(const std::vector<uint16>& faceSubMeshes);

	/// @brief Drops included ids >= subMeshCount, sorts and dedupes them, clamps each shape's
	///        surfaceSubMesh into range (0 if the mesh has no submeshes) and sanitizes every shape.
	void SanitizeCollisionRecipe(CollisionRecipe& recipe, uint16 subMeshCount);
}
```

`src/shared/math/collision_recipe.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_recipe.h"

#include <algorithm>

namespace mmo
{
	io::Writer& operator<<(io::Writer& writer, const CollisionRecipe& recipe)
	{
		writer
			<< io::write<uint32>(CollisionRecipeVersion)
			<< io::write<uint8>(recipe.useRenderGeometry ? 1 : 0)
			<< io::write_dynamic_range<uint32>(recipe.includedSubMeshes)
			<< io::write<uint32>(recipe.shapes.size());

		for (const CollisionShape& shape : recipe.shapes)
		{
			writer
				<< io::write<uint8>(shape.type)
				<< io::write<uint8>(shape.op)
				<< io::write_dynamic_range<uint8>(shape.name.begin(), shape.name.end())
				<< shape.position
				<< io::write<float>(shape.rotation.w)
				<< io::write<float>(shape.rotation.x)
				<< io::write<float>(shape.rotation.y)
				<< io::write<float>(shape.rotation.z)
				<< shape.scale
				<< io::write<uint16>(shape.surfaceSubMesh)
				<< io::write<uint16>(shape.segments)
				<< io::write<float>(shape.innerRadius)
				<< io::write<float>(shape.sweepDegrees)
				<< io::write<float>(shape.thickness)
				<< io::write<uint8>(shape.clockwise ? 1 : 0)
				<< io::write<uint8>(shape.twoSided ? 1 : 0);
		}

		return writer;
	}

	io::Reader& operator>>(io::Reader& reader, CollisionRecipe& recipe)
	{
		uint32 version = 0;
		if (!(reader >> io::read<uint32>(version)) || version != CollisionRecipeVersion)
		{
			reader.setFailure();
			return reader;
		}

		uint8 useRender = 0;
		uint32 shapeCount = 0;
		reader
			>> io::read<uint8>(useRender)
			>> io::read_container<uint32>(recipe.includedSubMeshes)
			>> io::read<uint32>(shapeCount);
		recipe.useRenderGeometry = useRender != 0;

		recipe.shapes.clear();
		for (uint32 i = 0; reader && i < shapeCount; ++i)
		{
			CollisionShape shape;
			uint8 type = 0, op = 0, clockwise = 0, twoSided = 0;
			reader
				>> io::read<uint8>(type)
				>> io::read<uint8>(op)
				>> io::read_container<uint8>(shape.name)
				>> shape.position
				>> io::read<float>(shape.rotation.w)
				>> io::read<float>(shape.rotation.x)
				>> io::read<float>(shape.rotation.y)
				>> io::read<float>(shape.rotation.z)
				>> shape.scale
				>> io::read<uint16>(shape.surfaceSubMesh)
				>> io::read<uint16>(shape.segments)
				>> io::read<float>(shape.innerRadius)
				>> io::read<float>(shape.sweepDegrees)
				>> io::read<float>(shape.thickness)
				>> io::read<uint8>(clockwise)
				>> io::read<uint8>(twoSided);

			if (type >= collision_shape_type::Count_ || op > collision_shape_op::Cut)
			{
				reader.setFailure();
				return reader;
			}

			shape.type = static_cast<collision_shape_type::Type>(type);
			shape.op = static_cast<collision_shape_op::Type>(op);
			shape.clockwise = clockwise != 0;
			shape.twoSided = twoSided != 0;
			recipe.shapes.push_back(shape);
		}

		return reader;
	}

	std::vector<uint16> InferIncludedSubMeshes(const std::vector<uint16>& faceSubMeshes)
	{
		std::vector<uint16> result = faceSubMeshes;
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	void SanitizeCollisionRecipe(CollisionRecipe& recipe, const uint16 subMeshCount)
	{
		auto& ids = recipe.includedSubMeshes;
		ids.erase(std::remove_if(ids.begin(), ids.end(), [subMeshCount](const uint16 id) { return id >= subMeshCount; }), ids.end());
		ids = InferIncludedSubMeshes(ids);

		for (CollisionShape& shape : recipe.shapes)
		{
			shape = SanitizeCollisionShape(shape);
			if (shape.surfaceSubMesh >= subMeshCount)
			{
				shape.surfaceSubMesh = 0;
			}
		}
	}
}
```
Check that `io::read_container<uint8>(String&)` exists; it is used by `material_instance_serializer.cpp:73`. Check that `io::Reader::setFailure()` exists; it is used by `aabb_tree.cpp:465`. A truncated read sets the failure flag in the `io::Reader`, so the truncated test passes without extra code.

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t math_tests --parallel
./bin/Debug/math_tests.exe "[collision_recipe]"
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/math/collision_recipe.h src/shared/math/collision_recipe.cpp src/tests/math_tests/test_collision_recipe.cpp
git commit -m "feat(math): versioned collision recipe with legacy inference"
```

---

### Task 4: Bake

**Files:**
- Create: `src/shared/math/collision_bake.h`, `src/shared/math/collision_bake.cpp`
- Test: `src/tests/math_tests/test_collision_bake.cpp`

**Interfaces:**
- Consumes: `CollisionShape`, `TessellateCollisionShape`, `IsPointInsideCollisionShape`, `CollisionShapeSupportsCut`, `SanitizeCollisionShape`.
- Produces: `struct CollisionBakeResult` with fields:
  - `vertices`, `indices`, `faceSubMeshes`;
  - `cutFaces` (uint32);
  - `cutFaceIndices` (`std::vector<uint32>`, render-face numbering);
  - `faceShape` (`std::vector<int32>`, −1 for a render face).

  And `CollisionBakeResult BakeCollision(const std::vector<Vector3>&, const std::vector<uint32>&, const std::vector<uint16>&, const std::vector<CollisionShape>&)`.

- [ ] **Step 1: Write the failing tests**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_bake.h"

using namespace mmo;

namespace
{
	// Two unit quads on y = 0: submesh 0 at x 0..1, submesh 1 at x 2..3.
	const std::vector<Vector3> s_vertices = {
		{ 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 },
		{ 2, 0, 0 }, { 3, 0, 0 }, { 3, 0, 1 }, { 2, 0, 1 }
	};
	const std::vector<uint32> s_indices = { 0, 2, 1, 0, 3, 2, 4, 6, 5, 4, 7, 6 };
	const std::vector<uint16> s_subMeshes = { 0, 0, 1, 1 };

	CollisionShape Box(const Vector3& position, const Vector3& scale, collision_shape_op::Type op, uint16 surface = 0)
	{
		CollisionShape shape;
		shape.type = collision_shape_type::Box;
		shape.op = op;
		shape.position = position;
		shape.scale = scale;
		shape.surfaceSubMesh = surface;
		return shape;
	}
}

TEST_CASE("Bake without shapes keeps the render geometry", "[collision_bake]")
{
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, {});
	CHECK(result.indices.size() == s_indices.size());
	CHECK(result.vertices.size() == 8);
	CHECK(result.faceSubMeshes == s_subMeshes);
	CHECK(result.faceShape == std::vector<int32>{ -1, -1, -1, -1 });
	CHECK(result.cutFaces == 0);
}

TEST_CASE("Cut shape removes the faces whose centroid it contains and compacts vertices", "[collision_bake]")
{
	const auto cut = Box(Vector3(2.5f, 0.0f, 0.5f), Vector3(2.0f, 1.0f, 2.0f), collision_shape_op::Cut);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { cut });

	CHECK(result.cutFaces == 2);
	CHECK(result.cutFaceIndices == std::vector<uint32>{ 2, 3 });
	CHECK(result.indices.size() == 6);
	CHECK(result.vertices.size() == 4);
	CHECK(result.faceSubMeshes == std::vector<uint16>{ 0, 0 });
	for (const auto& v : result.vertices)
	{
		CHECK(v.x <= 1.0f);
	}
}

TEST_CASE("Add shape appends its triangles with its surface submesh", "[collision_bake]")
{
	const auto add = Box(Vector3(5.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), collision_shape_op::Add, 7);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { add });

	REQUIRE(result.indices.size() == s_indices.size() + 36);
	CHECK(result.faceSubMeshes.size() == 16);
	CHECK(result.faceSubMeshes.back() == 7);
	CHECK(result.faceShape.back() == 0);
	CHECK(result.faceShape.front() == -1);
	for (const uint32 i : result.indices)
	{
		CHECK(i < result.vertices.size());
	}
}

TEST_CASE("Cut shapes never remove faces of added shapes", "[collision_bake]")
{
	const auto add = Box(Vector3(5.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), collision_shape_op::Add);
	const auto cut = Box(Vector3(5.0f, 0.0f, 0.0f), Vector3(3.0f, 3.0f, 3.0f), collision_shape_op::Cut);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { add, cut });
	CHECK(result.indices.size() == s_indices.size() + 36);
	CHECK(result.cutFaces == 0);
}

TEST_CASE("Bake with nothing left is empty", "[collision_bake]")
{
	CHECK(BakeCollision({}, {}, {}, {}).indices.empty());

	const auto cutAll = Box(Vector3(1.5f, 0.0f, 0.5f), Vector3(10.0f, 1.0f, 10.0f), collision_shape_op::Cut);
	const CollisionBakeResult result = BakeCollision(s_vertices, s_indices, s_subMeshes, { cutAll });
	CHECK(result.indices.empty());
	CHECK(result.vertices.empty());
	CHECK(result.cutFaces == 4);
}

TEST_CASE("Bake without a submesh mapping uses submesh 0 and skips out-of-range faces", "[collision_bake]")
{
	std::vector<uint32> indices = s_indices;
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(99);     // broken face
	const CollisionBakeResult result = BakeCollision(s_vertices, indices, {}, {});
	CHECK(result.indices.size() == s_indices.size());
	CHECK(result.faceSubMeshes == std::vector<uint16>{ 0, 0, 0, 0 });
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Reconfigure, then build. Expected: `math/collision_bake.h` not found.

- [ ] **Step 3: Implement**

`src/shared/math/collision_bake.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/collision_shape.h"

#include <vector>

namespace mmo
{
	/// @brief Collision geometry produced by BakeCollision, ready for AABBTree::Build.
	struct CollisionBakeResult
	{
		/// @brief Baked vertices.
		std::vector<Vector3> vertices;
		/// @brief Triangle list.
		std::vector<uint32> indices;
		/// @brief Submesh id per baked face.
		std::vector<uint16> faceSubMeshes;
		/// @brief Number of render faces removed by Cut shapes.
		uint32 cutFaces { 0 };
		/// @brief Render-face numbers (index into the input triangle list / 3) that were cut.
		std::vector<uint32> cutFaceIndices;
		/// @brief Per baked face: index of the Add shape that produced it, or -1 for a render face.
		std::vector<int32> faceShape;
	};

	/// @brief Bakes collision: render faces minus those whose centroid lies in a Cut shape, plus the Add shapes.
	/// @param renderVertices Render-derived collision vertices.
	/// @param renderIndices Render triangle list; faces with an out-of-range index are skipped.
	/// @param renderFaceSubMeshes Submesh per render face, or empty (then 0 is used).
	/// @param shapes Authored shapes; Cut acts on render faces only, never on other shapes.
	CollisionBakeResult BakeCollision(const std::vector<Vector3>& renderVertices, const std::vector<uint32>& renderIndices,
		const std::vector<uint16>& renderFaceSubMeshes, const std::vector<CollisionShape>& shapes);
}
```

`src/shared/math/collision_bake.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_bake.h"

#include <limits>

namespace mmo
{
	CollisionBakeResult BakeCollision(const std::vector<Vector3>& renderVertices, const std::vector<uint32>& renderIndices,
		const std::vector<uint16>& renderFaceSubMeshes, const std::vector<CollisionShape>& shapes)
	{
		CollisionBakeResult result;

		std::vector<CollisionShape> cutShapes;
		for (const CollisionShape& shape : shapes)
		{
			const CollisionShape sane = SanitizeCollisionShape(shape);
			if (sane.op == collision_shape_op::Cut && CollisionShapeSupportsCut(sane.type))
			{
				cutShapes.push_back(sane);
			}
		}

		const size_t faceCount = renderIndices.size() / 3;
		const bool hasSubMeshes = renderFaceSubMeshes.size() == faceCount;
		constexpr uint32 unmapped = std::numeric_limits<uint32>::max();
		std::vector<uint32> remap(renderVertices.size(), unmapped);

		for (size_t face = 0; face < faceCount; ++face)
		{
			const uint32 corners[3] = { renderIndices[face * 3], renderIndices[face * 3 + 1], renderIndices[face * 3 + 2] };
			if (corners[0] >= renderVertices.size() || corners[1] >= renderVertices.size() || corners[2] >= renderVertices.size())
			{
				continue;
			}

			const Vector3 centroid = (renderVertices[corners[0]] + renderVertices[corners[1]] + renderVertices[corners[2]]) / 3.0f;
			bool cut = false;
			for (const CollisionShape& shape : cutShapes)
			{
				if (IsPointInsideCollisionShape(shape, centroid))
				{
					cut = true;
					break;
				}
			}

			if (cut)
			{
				result.cutFaceIndices.push_back(static_cast<uint32>(face));
				continue;
			}

			for (const uint32 corner : corners)
			{
				if (remap[corner] == unmapped)
				{
					remap[corner] = static_cast<uint32>(result.vertices.size());
					result.vertices.push_back(renderVertices[corner]);
				}
				result.indices.push_back(remap[corner]);
			}

			result.faceSubMeshes.push_back(hasSubMeshes ? renderFaceSubMeshes[face] : 0);
			result.faceShape.push_back(-1);
		}

		result.cutFaces = static_cast<uint32>(result.cutFaceIndices.size());

		for (size_t i = 0; i < shapes.size(); ++i)
		{
			const CollisionShape sane = SanitizeCollisionShape(shapes[i]);
			if (sane.op != collision_shape_op::Add)
			{
				continue;
			}

			std::vector<Vector3> vertices;
			std::vector<uint32> indices;
			TessellateCollisionShape(sane, vertices, indices);

			const uint32 base = static_cast<uint32>(result.vertices.size());
			result.vertices.insert(result.vertices.end(), vertices.begin(), vertices.end());
			for (const uint32 index : indices)
			{
				result.indices.push_back(base + index);
			}

			const size_t faces = indices.size() / 3;
			result.faceSubMeshes.insert(result.faceSubMeshes.end(), faces, sane.surfaceSubMesh);
			result.faceShape.insert(result.faceShape.end(), faces, static_cast<int32>(i));
		}

		return result;
	}
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t math_tests --parallel
./bin/Debug/math_tests.exe "[collision_bake],[collision_recipe],[collision_shape],[aabb_tree]"
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/math/collision_bake.h src/shared/math/collision_bake.cpp src/tests/math_tests/test_collision_bake.cpp
git commit -m "feat(math): bake render collision with cut and add shapes"
```

---

### Task 5: Mesh format 0x0302 with the editor-only CSRC chunk

**Files:**
- Modify: `src/shared/scene_graph/mesh_serializer.h` (version enum at :23-34, `Serialize` at :169, new free function and enum)
- Modify: `src/shared/scene_graph/mesh_serializer.cpp` (magics at :19-26, `Serialize` at :122-162, `ReadMeshChunk` at :258-302)
- Test: `src/tests/scene_graph_tests/test_mesh_collision_recipe_chunk.cpp`

**Interfaces:**
- Consumes: `CollisionRecipe` and its stream operators (Task 3).
- Produces:
  - `mesh_version::Version_0_3_2 = 0x0302`;
  - `void MeshSerializer::Serialize(const MeshPtr&, io::Writer&, MeshVersion = mesh_version::Latest, const CollisionRecipe* recipe = nullptr)`;
  - `namespace collision_recipe_read { enum Type { Absent, Read, Corrupt }; }`;
  - `collision_recipe_read::Type ReadMeshCollisionRecipe(io::Reader& reader, CollisionRecipe& out_recipe)`.

- [ ] **Step 1: Write the failing tests**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_recipe.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_serializer.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

using namespace mmo;

namespace
{
	MeshPtr MakeMeshWithCollision()
	{
		auto mesh = std::make_shared<Mesh>("CollisionRecipeChunkTest");
		const std::vector<Vector3> vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } };
		const std::vector<uint32> indices = { 0, 2, 1, 0, 3, 2 };
		mesh->GetCollisionTree().Build(vertices, indices, { 0, 0 });
		return mesh;
	}

	CollisionRecipe MakeRecipe()
	{
		CollisionRecipe recipe;
		recipe.includedSubMeshes = { 0 };
		CollisionShape box;
		box.name = "Blocker";
		recipe.shapes.push_back(box);
		return recipe;
	}

	std::vector<char> Serialize(const MeshPtr& mesh, MeshVersion version, const CollisionRecipe* recipe)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		MeshSerializer serializer;
		serializer.Serialize(mesh, writer, version, recipe);
		return buffer;
	}

	MeshPtr Deserialize(const std::vector<char>& buffer, bool& ok)
	{
		auto mesh = std::make_shared<Mesh>("Loaded");
		io::MemorySource source{ buffer };
		io::Reader reader{ source };
		MeshDeserializer deserializer{ *mesh };
		ok = deserializer.Read(reader);
		return mesh;
	}

	void AppendChunk(std::vector<char>& buffer, const uint32 magic, const std::vector<char>& payload)
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << io::write<uint32>(magic) << io::write<uint32>(payload.size());
		for (const char byte : payload)
		{
			writer << io::write<uint8>(static_cast<uint8>(byte));
		}
	}
}

TEST_CASE("Runtime loads a 0x0302 mesh with a recipe chunk and an identical tree", "[mesh_serializer]")
{
	const MeshPtr mesh = MakeMeshWithCollision();
	const CollisionRecipe recipe = MakeRecipe();
	const std::vector<char> buffer = Serialize(mesh, mesh_version::Latest, &recipe);

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	REQUIRE(ok);
	CHECK(loaded->GetCollisionTree().GetIndices() == mesh->GetCollisionTree().GetIndices());
	CHECK(loaded->GetCollisionTree().GetVertices().size() == mesh->GetCollisionTree().GetVertices().size());

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe read;
	REQUIRE(ReadMeshCollisionRecipe(reader, read) == collision_recipe_read::Read);
	REQUIRE(read.shapes.size() == 1);
	CHECK(read.shapes[0].name == "Blocker");
}

TEST_CASE("Mesh without a recipe reports the recipe as absent", "[mesh_serializer]")
{
	const std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe read;
	CHECK(ReadMeshCollisionRecipe(reader, read) == collision_recipe_read::Absent);
}

TEST_CASE("A 0x0301 mesh still loads and never carries a recipe", "[mesh_serializer]")
{
	const CollisionRecipe recipe = MakeRecipe();
	const std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Version_0_3_1, &recipe);

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	REQUIRE(ok);
	CHECK_FALSE(loaded->GetCollisionTree().IsEmpty());

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe read;
	CHECK(ReadMeshCollisionRecipe(reader, read) == collision_recipe_read::Absent);
}

TEST_CASE("A 0x0302 mesh with an unknown extra chunk still loads", "[mesh_serializer]")
{
	std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	AppendChunk(buffer, 'XTRA', { 1, 2, 3, 4, 5 });

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	CHECK(ok);
	CHECK_FALSE(loaded->GetCollisionTree().IsEmpty());
}

TEST_CASE("A corrupt recipe chunk is reported and does not break the runtime load", "[mesh_serializer]")
{
	std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	AppendChunk(buffer, 'CSRC', { 9, 9 });    // too short for even the version field

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	CHECK(ok);
	CHECK_FALSE(loaded->GetCollisionTree().IsEmpty());

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	CollisionRecipe read;
	CHECK(ReadMeshCollisionRecipe(reader, read) == collision_recipe_read::Corrupt);
}
```
`'CSRC'` and `'XTRA'` as `uint32` multi-char literals give the same byte order as `*MakeChunkMagic('CSRC')`, because `MakeChunkMagic` reinterprets the `uint32`'s bytes.

- [ ] **Step 2: Run the tests to verify they fail**

Reconfigure, then:
```powershell
cmake --build build --config Debug -t scene_graph_tests --parallel
```
Expected: compile error. `Serialize` has no recipe parameter, and `ReadMeshCollisionRecipe` and `Version_0_3_2` are undefined.

- [ ] **Step 3: Implement**

`mesh_serializer.h`:
- Add `Version_0_3_2 = 0x0302,` after `Version_0_3_1` in `mesh_version`, with the doc comment `/// @brief Adds the optional editor-only collision recipe chunk (CSRC); unknown chunks are ignored from here on.`
- Add `#include "math/collision_recipe.h"`.
- Change the signature to
  `void Serialize(const MeshPtr& mesh, io::Writer& writer, MeshVersion version = mesh_version::Latest, const CollisionRecipe* recipe = nullptr);`
  with the doc `/// @param recipe Editor-only collision recipe, written as a CSRC chunk when non-null and version >= 0x0302.`
- After the `MeshDeserializer` class, add:
```cpp
	namespace collision_recipe_read
	{
		/// @brief Outcome of ReadMeshCollisionRecipe.
		enum Type
		{
			/// @brief The mesh has no recipe chunk.
			Absent,
			/// @brief The recipe was read.
			Read,
			/// @brief A recipe chunk exists but could not be read.
			Corrupt
		};
	}

	/// @brief Scans a serialized mesh's top-level chunks for the editor-only collision recipe.
	/// @param reader Reader positioned at the start of the mesh file.
	/// @param out_recipe Receives the recipe when the result is Read.
	collision_recipe_read::Type ReadMeshCollisionRecipe(io::Reader& reader, CollisionRecipe& out_recipe);
```

`mesh_serializer.cpp`:
- After the `COLL` magic (:26), add `static const ChunkMagic MeshCollisionRecipeChunk = MakeChunkMagic('CSRC');`.
- In `Serialize`, change the definition signature to match, and make `Latest` map to `mesh_version::Version_0_3_2`.
- After the `COLL` block, add:
```cpp
		// Editor-only collision recipe; runtime readers skip it.
		if (recipe && version >= mesh_version::Version_0_3_2)
		{
			ChunkWriter recipeChunk{ MeshCollisionRecipeChunk, writer };
			writer << *recipe;
			recipeChunk.Finish();
		}
```
- In `ReadMeshChunk`, inside the `version >= mesh_version::Version_0_3` block after `RemoveChunkHandler(*MeshIndexChunk);`, add:
```cpp
						if (version >= mesh_version::Version_0_3_2)
						{
							// The collision recipe is editor-only data; the editor reads it with
							// ReadMeshCollisionRecipe. From this version on, chunks a reader does not
							// know are skipped instead of failing the load.
							AddChunkHandler(*MeshCollisionRecipeChunk, false, [](io::Reader& chunkReader, uint32, const uint32 chunkSize)
							{
								chunkReader >> io::skip(chunkSize);
								return static_cast<bool>(chunkReader);
							});
							SetIgnoreUnhandledChunks(true);
						}
```
- At the end of the file, inside the namespace, add:
```cpp
	collision_recipe_read::Type ReadMeshCollisionRecipe(io::Reader& reader, CollisionRecipe& out_recipe)
	{
		while (reader)
		{
			uint32 chunkId = 0, chunkSize = 0;
			if (!(reader >> io::read<uint32>(chunkId) >> io::read<uint32>(chunkSize)))
			{
				break;
			}

			if (chunkId == *MeshCollisionRecipeChunk)
			{
				CollisionRecipe recipe;
				reader >> recipe;
				if (!reader)
				{
					return collision_recipe_read::Corrupt;
				}

				out_recipe = std::move(recipe);
				return collision_recipe_read::Read;
			}

			reader >> io::skip(chunkSize);
		}

		return collision_recipe_read::Absent;
	}
```
A 2-byte `CSRC` chunk at the end of the file makes `reader >> recipe` hit end-of-data, which gives `Corrupt`. A short chunk in the *middle* of a file would read into the next chunk's bytes. That is acceptable, because the version check (`== 1`) and the enum range checks make that fail in practice.

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t scene_graph_tests --parallel
./bin/Debug/scene_graph_tests.exe "[mesh_serializer]"
```
Expected: all pass. Then make sure nothing else regressed:
```powershell
cmake --build build --config Debug -t all_tests --parallel
cd build; ctest -C Debug --output-on-failure; cd ..
```

- [ ] **Step 5: Commit**

```bash
git add src/shared/scene_graph/mesh_serializer.h src/shared/scene_graph/mesh_serializer.cpp src/tests/scene_graph_tests/test_mesh_collision_recipe_chunk.cpp
git commit -m "feat(scene_graph): mesh format 0x0302 with editor-only collision recipe chunk"
```

---

### Task 6: Editor geometry gather, recipe-driven rebuild and save

**Files:**
- Create: `src/mmo_edit/editors/mesh_editor/mesh_collision_geometry.h/.cpp`
- Modify: `src/mmo_edit/editors/mesh_editor/mesh_editor_instance.cpp`
  - move `ReadVertexDataPositions` and `ReadIndexData` (:1400-1449) out;
  - the "Build Complex" block (:1476-1553) is replaced in Task 8 through the collision editor; in this task it calls the new gather helper.

**Interfaces:**
- Consumes: `CollisionRecipe`, `SanitizeCollisionRecipe`, `BakeCollision`, `ReadMeshCollisionRecipe`, `MeshSerializer::Serialize(..., recipe)`.
- Produces:
  - `struct RenderCollisionGeometry { std::vector<Vector3> vertices; std::vector<uint32> indices; std::vector<uint16> faceSubMeshes; };`
  - `RenderCollisionGeometry GatherRenderCollisionGeometry(Mesh& mesh, const std::vector<uint16>& includedSubMeshes);`
  - `CollisionBakeResult RebuildMeshCollision(Mesh& mesh, CollisionRecipe& recipe, RenderCollisionGeometry* out_renderGeometry = nullptr, bool buildTree = true);` It sanitizes the recipe against the mesh's submesh count. It clears the tree, and builds it only when `buildTree` is true and the bake is non-empty.
  - `std::vector<uint16> AllSubMeshesWithIndexData(Mesh& mesh);`
  - `bool LoadMeshCollisionRecipe(const String& assetPath, CollisionRecipe& out_recipe, collision_recipe_read::Type& out_result);`
  - `bool SaveMeshWithRecipe(const MeshPtr& mesh, const String& assetPath, const CollisionRecipe* recipe);` It writes the mesh file only, not the skeleton.
  - `bool RebakeMeshCollisionFile(const String& assetPath);` (used by Task 10)

This is editor code with no automated test harness. Its pure parts are covered by Tasks 1–5. Verification is by build plus the manual check in Step 4.

- [ ] **Step 1: Create the header**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/collision_bake.h"
#include "math/collision_recipe.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_serializer.h"

#include <vector>

namespace mmo
{
	/// @brief Render triangles of a mesh's submeshes, gathered as collision input.
	struct RenderCollisionGeometry
	{
		/// @brief Vertex positions.
		std::vector<Vector3> vertices;
		/// @brief Triangle list.
		std::vector<uint32> indices;
		/// @brief Source submesh per face.
		std::vector<uint16> faceSubMeshes;
	};

	/// @brief Gathers the render triangles of the given submeshes (shared or per-submesh vertex data). Ids out of range are skipped.
	RenderCollisionGeometry GatherRenderCollisionGeometry(Mesh& mesh, const std::vector<uint16>& includedSubMeshes);

	/// @brief Ids of every submesh that has index data.
	std::vector<uint16> AllSubMeshesWithIndexData(Mesh& mesh);

	/// @brief Bakes the recipe into the mesh's collision tree.
	/// @param mesh Mesh whose tree is replaced.
	/// @param recipe Recipe; sanitized against the mesh in place.
	/// @param out_renderGeometry Receives the gathered render geometry, if not null (used by the overlay to draw cut faces).
	/// @param buildTree If false, only bakes (cheap preview during a gizmo drag) and leaves the tree untouched.
	/// @return The bake result.
	CollisionBakeResult RebuildMeshCollision(Mesh& mesh, CollisionRecipe& recipe, RenderCollisionGeometry* out_renderGeometry = nullptr, bool buildTree = true);

	/// @brief Reads the editor-only collision recipe from a mesh asset file.
	/// @return False if the file could not be opened.
	bool LoadMeshCollisionRecipe(const String& assetPath, CollisionRecipe& out_recipe, collision_recipe_read::Type& out_result);

	/// @brief Writes the mesh file at the latest version, with the recipe chunk if `recipe` is non-null.
	bool SaveMeshWithRecipe(const MeshPtr& mesh, const String& assetPath, const CollisionRecipe* recipe);

	/// @brief Unattended `mmo_edit --rebake-collision`: loads a mesh, rebakes its stored recipe and saves it.
	/// @return False if the mesh has no (readable) recipe or could not be saved.
	bool RebakeMeshCollisionFile(const String& assetPath);
}
```

- [ ] **Step 2: Create the implementation**

Move `ReadVertexDataPositions` and `ReadIndexData` verbatim from `mesh_editor_instance.cpp:1400-1449` into an anonymous namespace here, and delete them from `mesh_editor_instance.cpp`.
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_collision_geometry.h"

#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/render_operation.h"

namespace mmo
{
	namespace
	{
		// ReadVertexDataPositions(...) and ReadIndexData(...) moved here unchanged.
	}

	RenderCollisionGeometry GatherRenderCollisionGeometry(Mesh& mesh, const std::vector<uint16>& includedSubMeshes)
	{
		RenderCollisionGeometry out;

		uint32 sharedVertexOffset = 0;
		bool sharedVerticesGathered = false;

		for (const uint16 i : includedSubMeshes)
		{
			if (i >= mesh.GetSubMeshCount())
			{
				continue;
			}

			SubMesh& sub = mesh.GetSubMesh(i);
			if (!sub.indexData)
			{
				continue;
			}

			uint32 vertexOffset = 0;
			if (sub.useSharedVertices)
			{
				if (!mesh.sharedVertexData)
				{
					continue;
				}

				if (!sharedVerticesGathered)
				{
					sharedVertexOffset = static_cast<uint32>(out.vertices.size());
					ReadVertexDataPositions(*mesh.sharedVertexData, out.vertices);
					sharedVerticesGathered = true;
				}

				vertexOffset = sharedVertexOffset;
			}
			else
			{
				if (!sub.vertexData)
				{
					continue;
				}

				vertexOffset = static_cast<uint32>(out.vertices.size());
				ReadVertexDataPositions(*sub.vertexData, out.vertices);
			}

			const size_t indexCountBefore = out.indices.size();
			ReadIndexData(*sub.indexData, vertexOffset, out.indices);
			out.faceSubMeshes.insert(out.faceSubMeshes.end(), (out.indices.size() - indexCountBefore) / 3, i);
		}

		return out;
	}

	std::vector<uint16> AllSubMeshesWithIndexData(Mesh& mesh)
	{
		std::vector<uint16> ids;
		for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
		{
			if (mesh.GetSubMesh(i).indexData)
			{
				ids.push_back(i);
			}
		}
		return ids;
	}

	CollisionBakeResult RebuildMeshCollision(Mesh& mesh, CollisionRecipe& recipe, RenderCollisionGeometry* out_renderGeometry, const bool buildTree)
	{
		SanitizeCollisionRecipe(recipe, mesh.GetSubMeshCount());

		RenderCollisionGeometry render;
		if (recipe.useRenderGeometry)
		{
			render = GatherRenderCollisionGeometry(mesh, recipe.includedSubMeshes);
		}

		CollisionBakeResult result = BakeCollision(render.vertices, render.indices, render.faceSubMeshes, recipe.shapes);

		if (buildTree)
		{
			AABBTree& tree = mesh.GetCollisionTree();
			tree.Clear();

			// Building from empty input would still allocate a node pool and serialize as bogus collision.
			if (!result.indices.empty())
			{
				tree.Build(result.vertices, result.indices, result.faceSubMeshes);
			}
		}

		if (out_renderGeometry)
		{
			*out_renderGeometry = std::move(render);
		}

		return result;
	}

	bool LoadMeshCollisionRecipe(const String& assetPath, CollisionRecipe& out_recipe, collision_recipe_read::Type& out_result)
	{
		const auto file = AssetRegistry::OpenFile(assetPath);
		if (!file)
		{
			return false;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };
		out_result = ReadMeshCollisionRecipe(reader, out_recipe);
		return true;
	}

	bool SaveMeshWithRecipe(const MeshPtr& mesh, const String& assetPath, const CollisionRecipe* recipe)
	{
		const auto file = AssetRegistry::CreateNewFile(assetPath);
		if (!file)
		{
			ELOG("Failed to open mesh file " << assetPath << " for writing!");
			return false;
		}

		io::StreamSink sink{ *file };
		io::Writer writer{ sink };
		MeshSerializer serializer;
		serializer.Serialize(mesh, writer, mesh_version::Latest, recipe);
		return true;
	}

	bool RebakeMeshCollisionFile(const String& assetPath)
	{
		CollisionRecipe recipe;
		collision_recipe_read::Type result = collision_recipe_read::Absent;
		if (!LoadMeshCollisionRecipe(assetPath, recipe, result))
		{
			ELOG("Rebake collision: cannot open " << assetPath);
			return false;
		}

		if (result != collision_recipe_read::Read)
		{
			ELOG("Rebake collision: " << assetPath << (result == collision_recipe_read::Absent ? " has no collision recipe" : " has a corrupt collision recipe"));
			return false;
		}

		const MeshPtr mesh = MeshManager::Get().Load(assetPath);
		if (!mesh)
		{
			ELOG("Rebake collision: cannot load mesh " << assetPath);
			return false;
		}

		const CollisionBakeResult bake = RebuildMeshCollision(*mesh, recipe);
		if (!SaveMeshWithRecipe(mesh, assetPath, &recipe))
		{
			return false;
		}

		ILOG("Rebake collision: " << assetPath << " - " << bake.indices.size() / 3 << " faces, " << bake.cutFaces << " cut, " << recipe.shapes.size() << " shapes");
		return true;
	}
}
```
Notes:
- Check the `stream_sink.h` include path against `mesh_editor_instance.cpp:11`, which includes `"stream_sink.h"`.
- Check the `StreamSource` include against `server_collision_map.cpp`'s includes.

- [ ] **Step 3: Use the helper in the existing Build Complex button and save path**

In `mesh_editor_instance.cpp`:
- `#include "mesh_collision_geometry.h"`.
- Replace the body of the `if (ImGui::Button("Build Complex"))` block with:
```cpp
				const std::vector<uint16> included(m_includedSubMeshes.begin(), m_includedSubMeshes.end());
				CollisionRecipe recipe;
				recipe.includedSubMeshes = included;
				RebuildMeshCollision(*m_mesh, recipe);
```
This is an interim step. Task 8 replaces the whole panel.

- [ ] **Step 4: Build and check by hand**

Reconfigure, then:
```powershell
cmake --build build --config Debug -t mmo_edit --parallel
```
Expected: the build succeeds. Then:
1. Open any static mesh, tick all its submeshes, and press Build Complex.
2. Check that "Nodes:" shows the same number as before this change.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editors/mesh_editor/mesh_collision_geometry.h src/mmo_edit/editors/mesh_editor/mesh_collision_geometry.cpp src/mmo_edit/editors/mesh_editor/mesh_editor_instance.cpp
git commit -m "refactor(mmo_edit): gather and bake mesh collision through a recipe helper"
```

---

### Task 7: Collision overlay

**Files:**
- Create: `src/mmo_edit/editors/mesh_editor/collision_overlay.h/.cpp`

**Interfaces:**
- Consumes: `IsCollisionFaceWalkable` (Task 1), `ManualRenderObject` (`scene_graph/manual_render_object.h`), `Scene::CreateManualRenderObject/DestroyManualRenderObject`.
- Produces:
  - `struct CollisionOverlayData` with fields:
    - `const std::vector<Vector3>* vertices`, `const std::vector<uint32>* indices`;
    - `const std::vector<int32>* faceShape` (may be null);
    - `std::vector<Vector3> cutTriangles` (3 per face);
    - `int32 selectedShape = -1`, `bool wireframe = true`.
  - `class CollisionOverlay` with:
    - `CollisionOverlay(Scene&, SceneNode& parent)`, a destructor;
    - `void Rebuild(const CollisionOverlayData&)`;
    - `void SetVisible(bool)`.

- [ ] **Step 1: Write the header**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	class Scene;
	class SceneNode;
	class ManualRenderObject;

	/// @brief What CollisionOverlay draws: baked collision faces plus faces a Cut shape removed.
	struct CollisionOverlayData
	{
		/// @brief Baked collision vertices.
		const std::vector<Vector3>* vertices { nullptr };
		/// @brief Baked collision triangle list.
		const std::vector<uint32>* indices { nullptr };
		/// @brief Per face: producing shape index or -1; may be null (everything render-derived).
		const std::vector<int32>* faceShape { nullptr };
		/// @brief Removed render faces, 3 vertices each.
		std::vector<Vector3> cutTriangles;
		/// @brief Shape whose faces are highlighted, or -1.
		int32 selectedShape { -1 };
		/// @brief Whether to draw triangle edges.
		bool wireframe { true };
	};

	/// @brief Semi-transparent collision visualisation coloured by walkability and origin.
	///        Reusable by any editor that has a Scene (mesh editor now, world model editor later).
	class CollisionOverlay final : public NonCopyable
	{
	public:
		/// @brief Creates the overlay's render objects under `parent`.
		CollisionOverlay(Scene& scene, SceneNode& parent);

		/// @brief Destroys the render objects.
		~CollisionOverlay() override;

	public:
		/// @brief Replaces everything drawn with `data`.
		void Rebuild(const CollisionOverlayData& data);

		/// @brief Shows or hides the overlay.
		void SetVisible(bool visible);

	private:
		Scene& m_scene;
		SceneNode* m_node { nullptr };
		ManualRenderObject* m_faces { nullptr };
	};
}
```

- [ ] **Step 2: Write the implementation**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_overlay.h"

#include "math/collision_shape.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	namespace
	{
		// ARGB, translucent like the other editor overlays.
		constexpr uint32 WalkableColor = 0x5040D040u;       // green
		constexpr uint32 SteepColor = 0x50FF8C1Au;          // orange: too steep to walk on
		constexpr uint32 ShapeWalkableColor = 0x504090FFu;  // blue: walkable face of an authored shape
		constexpr uint32 CutColor = 0x50FF3030u;            // red: render face removed by a Cut shape
		constexpr uint32 SelectedColor = 0x70FFE030u;       // yellow: selected shape
		constexpr uint32 EdgeColor = 0xA0FFFFFFu;
	}

	CollisionOverlay::CollisionOverlay(Scene& scene, SceneNode& parent)
		: m_scene(scene)
	{
		m_node = parent.CreateChildSceneNode();
		m_faces = m_scene.CreateManualRenderObject("CollisionOverlay");
		m_faces->SetCastShadows(false);
		m_faces->SetQueryFlags(0);
		m_faces->SetRenderQueueGroup(Overlay);
		m_node->AttachObject(*m_faces);
	}

	CollisionOverlay::~CollisionOverlay()
	{
		if (m_faces)
		{
			m_scene.DestroyManualRenderObject(*m_faces);
		}
		if (m_node)
		{
			m_scene.DestroySceneNode(*m_node);
		}
	}

	void CollisionOverlay::Rebuild(const CollisionOverlayData& data)
	{
		m_faces->Clear();
		if (!data.vertices || !data.indices)
		{
			return;
		}

		const MaterialPtr faceMaterial = MaterialManager::Get().Load("Models/Engine/AxisPlaneHighlight.hmat");
		const MaterialPtr edgeMaterial = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		const auto& vertices = *data.vertices;
		const auto& indices = *data.indices;

		if (faceMaterial)
		{
			auto triangles = m_faces->AddTriangleListOperation(faceMaterial);
			for (size_t f = 0; f + 2 < indices.size(); f += 3)
			{
				const Vector3& a = vertices[indices[f]];
				const Vector3& b = vertices[indices[f + 1]];
				const Vector3& c = vertices[indices[f + 2]];
				const int32 shape = data.faceShape && f / 3 < data.faceShape->size() ? (*data.faceShape)[f / 3] : -1;
				const bool walkable = IsCollisionFaceWalkable(a, b, c);

				uint32 color = walkable ? (shape >= 0 ? ShapeWalkableColor : WalkableColor) : SteepColor;
				if (shape >= 0 && shape == data.selectedShape)
				{
					color = SelectedColor;
				}

				// Both windings: collision is two-sided for the player, and the camera is often inside it.
				triangles->AddTriangle(a, b, c).SetColor(color);
				triangles->AddTriangle(a, c, b).SetColor(color);
			}

			for (size_t i = 0; i + 2 < data.cutTriangles.size(); i += 3)
			{
				triangles->AddTriangle(data.cutTriangles[i], data.cutTriangles[i + 1], data.cutTriangles[i + 2]).SetColor(CutColor);
				triangles->AddTriangle(data.cutTriangles[i], data.cutTriangles[i + 2], data.cutTriangles[i + 1]).SetColor(CutColor);
			}
		}

		if (data.wireframe && edgeMaterial)
		{
			auto lines = m_faces->AddLineListOperation(edgeMaterial);
			for (size_t f = 0; f + 2 < indices.size(); f += 3)
			{
				const Vector3& a = vertices[indices[f]];
				const Vector3& b = vertices[indices[f + 1]];
				const Vector3& c = vertices[indices[f + 2]];
				lines->AddLine(a, b).SetColor(EdgeColor);
				lines->AddLine(b, c).SetColor(EdgeColor);
				lines->AddLine(c, a).SetColor(EdgeColor);
			}
		}
	}

	void CollisionOverlay::SetVisible(const bool visible)
	{
		m_faces->SetVisible(visible);
	}
}
```
Check these against the code before building:
- `SceneNode::CreateChildSceneNode()` with no arguments (used at `mesh_editor_instance.cpp:96`).
- `Scene::DestroySceneNode(const SceneNode&)` (`scene.h:298`).
- The `Overlay` enumerator is in `scene_graph/render_queue.h:67`.
- `Line::SetColor`; if `AddLine(...)` returns a type with a different colour setter, follow `terrain_edit_mode.cpp:311`.

- [ ] **Step 3: Build**

Reconfigure, then build `mmo_edit`. Expected: success. It isn't wired up yet; Task 8 does that.

- [ ] **Step 4: Commit**

```bash
git add src/mmo_edit/editors/mesh_editor/collision_overlay.h src/mmo_edit/editors/mesh_editor/collision_overlay.cpp
git commit -m "feat(mmo_edit): walkability-coloured collision overlay"
```

---

### Task 8: MeshCollisionEditor — recipe state, view modes and shapes panel

**Files:**
- Create: `src/mmo_edit/editors/mesh_editor/mesh_collision_editor.h/.cpp`
- Modify: `src/mmo_edit/editors/mesh_editor/mesh_editor_instance.h/.cpp`
  - own the editor;
  - `DrawCollision` delegates to it;
  - `Render` calls `Update`;
  - `Save` uses the recipe;
  - remove `m_includedSubMeshes`.

**Interfaces:**
- Consumes: Tasks 1–7.
- Produces:
  - `namespace collision_view_mode { enum Type { Off, Overlay, CollisionOnly }; }`
  - `class MeshCollisionEditor`:
    - `MeshCollisionEditor(Scene& scene, Camera& camera, SceneNode& cameraAnchor, MeshPtr mesh, Entity* entity, const String& assetPath)`
    - `void Update()` (call before the scene renders)
    - `void DrawPanel()`
    - `const CollisionRecipe* GetRecipeForSave() const` (nullptr until touched or loaded)
    - `CollisionShape* GetShape(uint32 index)`
    - `void OnShapeChanged(bool final)`
    - `void DeleteShape(uint32 index)`
    - `void DuplicateShape(uint32 index)`
    - `bool IsActive() const` (view mode != Off)

  Task 9 adds the input methods.

- [ ] **Step 1: Write the header**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "collision_overlay.h"
#include "mesh_collision_geometry.h"
#include "selection.h"
#include "transform_widget.h"

#include "base/non_copyable.h"
#include "scene_graph/mesh.h"

#include <memory>

namespace mmo
{
	class Camera;
	class Entity;
	class Scene;
	class SceneNode;

	namespace collision_view_mode
	{
		/// @brief How the mesh editor shows collision.
		enum Type
		{
			/// @brief No collision visualisation; shapes cannot be edited.
			Off,
			/// @brief Collision drawn over the render mesh.
			Overlay,
			/// @brief Render mesh hidden, only collision drawn.
			CollisionOnly
		};
	}

	/// @brief Collision editing of one mesh: recipe, overlay, shapes panel, picking and gizmo.
	class MeshCollisionEditor final : public NonCopyable
	{
	public:
		/// @brief Loads the mesh's collision recipe (if any) and sets up overlay and gizmo.
		MeshCollisionEditor(Scene& scene, Camera& camera, SceneNode& cameraAnchor, MeshPtr mesh, Entity* entity, String assetPath);

		/// @brief Releases overlay and gizmo.
		~MeshCollisionEditor() override;

	public:
		/// @brief Per-frame update before the scene renders: overlay refresh and gizmo.
		void Update();

		/// @brief Draws the collision panel contents (inside the caller's ImGui window).
		void DrawPanel();

		/// @brief The recipe to save, or nullptr if the mesh had none and collision was never edited.
		[[nodiscard]] const CollisionRecipe* GetRecipeForSave() const;

		/// @brief Whether collision is shown (and shapes are editable).
		[[nodiscard]] bool IsActive() const { return m_viewMode != collision_view_mode::Off; }

		/// @brief Shape by index, or nullptr.
		CollisionShape* GetShape(uint32 index);

		/// @brief Called after a shape changed; final=false only refreshes the preview, true also rebuilds the tree.
		void OnShapeChanged(bool final);

		/// @brief Removes a shape and rebakes.
		void DeleteShape(uint32 index);

		/// @brief Copies a shape (offset slightly) and selects the copy.
		void DuplicateShape(uint32 index);

		/// @brief Deletes a shape at the start of the next Update (safe to call from the shape's own selectable).
		void RequestDeleteShape(uint32 index) { m_pendingDelete = static_cast<int32>(index); }

		/// @brief Duplicates a shape at the start of the next Update (safe to call from the shape's own selectable).
		void RequestDuplicateShape(uint32 index) { m_pendingDuplicate = static_cast<int32>(index); }

	private:
		void EnsureRecipe();
		void Rebake(bool buildTree);
		void AddShape(collision_shape_type::Type type);
		void SelectShape(int32 index);
		void ApplyViewMode();
		void DrawShapeDetails(CollisionShape& shape);

	private:
		Scene& m_scene;
		Camera& m_camera;
		SceneNode& m_cameraAnchor;
		MeshPtr m_mesh;
		Entity* m_entity { nullptr };
		String m_assetPath;

		std::unique_ptr<CollisionRecipe> m_recipe;
		bool m_recipeTouched { false };
		bool m_recipeLoaded { false };

		RenderCollisionGeometry m_renderGeometry;
		CollisionBakeResult m_lastBake;
		bool m_overlayDirty { true };
		/// @brief A preview bake ran without rebuilding the tree; rebuild once the mouse is released.
		bool m_previewPending { false };
		int32 m_pendingDelete { -1 };
		int32 m_pendingDuplicate { -1 };

		std::unique_ptr<CollisionOverlay> m_overlay;
		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;
		int32 m_selectedShape { -1 };

		collision_view_mode::Type m_viewMode { collision_view_mode::Off };
		bool m_wireframe { true };
		String m_status;
	};
}
```

- [ ] **Step 2: Write the implementation (minus input, which is Task 9)**

`mesh_collision_editor.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_collision_editor.h"

#include "editor_windows/editor_imgui_helpers.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>

namespace mmo
{
	MeshCollisionEditor::MeshCollisionEditor(Scene& scene, Camera& camera, SceneNode& cameraAnchor, MeshPtr mesh, Entity* entity, String assetPath)
		: m_scene(scene)
		, m_camera(camera)
		, m_cameraAnchor(cameraAnchor)
		, m_mesh(std::move(mesh))
		, m_entity(entity)
		, m_assetPath(std::move(assetPath))
	{
		CollisionRecipe recipe;
		collision_recipe_read::Type result = collision_recipe_read::Absent;
		if (LoadMeshCollisionRecipe(m_assetPath, recipe, result))
		{
			if (result == collision_recipe_read::Read)
			{
				m_recipe = std::make_unique<CollisionRecipe>(std::move(recipe));
				m_recipeLoaded = true;
			}
			else if (result == collision_recipe_read::Corrupt)
			{
				WLOG("Mesh " << m_assetPath << " has an unreadable collision recipe; it is ignored and replaced on the next collision edit.");
				m_status = "Stored collision recipe is unreadable and was ignored.";
			}
		}

		m_overlay = std::make_unique<CollisionOverlay>(m_scene, m_scene.GetRootSceneNode());
		m_transformWidget = std::make_unique<TransformWidget>(m_selection, m_scene, m_camera);
		m_transformWidget->SetTransformMode(TransformMode::Translate);
		ApplyViewMode();

		// The stored recipe is the source of truth: rebuild the tree from it, without marking it
		// touched (EnsureRecipe would), so an unedited mesh saves exactly what it loaded.
		if (m_recipeLoaded)
		{
			m_lastBake = RebuildMeshCollision(*m_mesh, *m_recipe, &m_renderGeometry, true);
		}
	}

	MeshCollisionEditor::~MeshCollisionEditor()
	{
		m_selection.Clear();
		m_transformWidget.reset();
		m_overlay.reset();
	}

	const CollisionRecipe* MeshCollisionEditor::GetRecipeForSave() const
	{
		return (m_recipeTouched || m_recipeLoaded) ? m_recipe.get() : nullptr;
	}

	CollisionShape* MeshCollisionEditor::GetShape(const uint32 index)
	{
		return m_recipe && index < m_recipe->shapes.size() ? &m_recipe->shapes[index] : nullptr;
	}

	void MeshCollisionEditor::EnsureRecipe()
	{
		m_recipeTouched = true;
		if (m_recipe)
		{
			return;
		}

		// Keep today's collision: take the included submeshes from the tree's face mapping.
		m_recipe = std::make_unique<CollisionRecipe>();
		const AABBTree& tree = m_mesh->GetCollisionTree();
		m_recipe->includedSubMeshes = InferIncludedSubMeshes(tree.GetFaceSubMeshes());
		if (m_recipe->includedSubMeshes.empty())
		{
			if (tree.IsEmpty())
			{
				// No collision so far: shapes alone, nothing from the render mesh.
				m_recipe->useRenderGeometry = false;
			}
			else
			{
				WLOG("Mesh " << m_assetPath << " has a legacy collision tree without submesh mapping; including every submesh.");
				m_recipe->includedSubMeshes = AllSubMeshesWithIndexData(*m_mesh);
			}
		}
	}

	void MeshCollisionEditor::Rebake(const bool buildTree)
	{
		EnsureRecipe();
		m_lastBake = RebuildMeshCollision(*m_mesh, *m_recipe, &m_renderGeometry, buildTree);
		m_overlayDirty = true;
		m_previewPending = !buildTree;

		char buffer[128];
		snprintf(buffer, sizeof(buffer), "Faces: %zu  Cut: %u  Nodes: %zu", m_lastBake.indices.size() / 3, m_lastBake.cutFaces, m_mesh->GetCollisionTree().GetNodes().size());
		m_status = buffer;
	}

	void MeshCollisionEditor::OnShapeChanged(const bool final)
	{
		Rebake(final);
	}

	void MeshCollisionEditor::AddShape(const collision_shape_type::Type type)
	{
		EnsureRecipe();

		const AABB& bounds = m_mesh->GetBounds();
		const Vector3 size = bounds.GetSize();
		const float typical = std::max(0.5f, std::max(size.x, std::max(size.y, size.z)) * 0.25f);

		CollisionShape shape;
		shape.type = type;
		shape.name = String(GetCollisionShapeTypeName(type)) + " " + std::to_string(m_recipe->shapes.size() + 1);
		shape.position = m_cameraAnchor.GetPosition();
		shape.scale = Vector3(typical, typical, typical);
		if (type == collision_shape_type::HelixRamp)
		{
			// Spiral stairs usually fill the mesh: match its footprint and height.
			shape.position = bounds.GetCenter();
			shape.scale = Vector3(std::max(size.x, 0.5f), std::max(size.y, 0.5f), std::max(size.z, 0.5f));
			shape.segments = 48;
		}
		if (!m_recipe->includedSubMeshes.empty())
		{
			shape.surfaceSubMesh = m_recipe->includedSubMeshes.front();
		}

		m_recipe->shapes.push_back(shape);
		Rebake(true);
		SelectShape(static_cast<int32>(m_recipe->shapes.size() - 1));
	}

	void MeshCollisionEditor::DeleteShape(const uint32 index)
	{
		if (!m_recipe || index >= m_recipe->shapes.size())
		{
			return;
		}

		SelectShape(-1);
		m_recipe->shapes.erase(m_recipe->shapes.begin() + index);
		m_recipeTouched = true;
		Rebake(true);
	}

	void MeshCollisionEditor::DuplicateShape(const uint32 index)
	{
		if (!m_recipe || index >= m_recipe->shapes.size())
		{
			return;
		}

		CollisionShape copy = m_recipe->shapes[index];
		copy.name += " Copy";
		copy.position += Vector3(0.25f, 0.0f, 0.25f);
		m_recipe->shapes.push_back(copy);
		m_recipeTouched = true;
		Rebake(true);
		SelectShape(static_cast<int32>(m_recipe->shapes.size() - 1));
	}

	void MeshCollisionEditor::ApplyViewMode()
	{
		const bool active = IsActive();
		m_overlay->SetVisible(active);
		if (m_entity)
		{
			m_entity->SetVisible(m_viewMode != collision_view_mode::CollisionOnly);
		}
		if (!active)
		{
			SelectShape(-1);
		}
		m_overlayDirty = true;
	}

	void MeshCollisionEditor::Update()
	{
		if (m_pendingDelete >= 0)
		{
			const int32 index = m_pendingDelete;
			m_pendingDelete = -1;
			DeleteShape(static_cast<uint32>(index));
		}
		if (m_pendingDuplicate >= 0)
		{
			const int32 index = m_pendingDuplicate;
			m_pendingDuplicate = -1;
			DuplicateShape(static_cast<uint32>(index));
		}

		// Drags (panel fields or gizmo) only preview; rebuild the tree once the mouse is up.
		if (m_previewPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			Rebake(true);
		}

		if (m_overlayDirty && IsActive())
		{
			CollisionOverlayData data;
			if (m_recipe && (m_recipeTouched || m_recipeLoaded))
			{
				data.vertices = &m_lastBake.vertices;
				data.indices = &m_lastBake.indices;
				data.faceShape = &m_lastBake.faceShape;
				for (const uint32 face : m_lastBake.cutFaceIndices)
				{
					for (int corner = 0; corner < 3; ++corner)
					{
						data.cutTriangles.push_back(m_renderGeometry.vertices[m_renderGeometry.indices[face * 3 + corner]]);
					}
				}
			}
			else
			{
				data.vertices = &m_mesh->GetCollisionTree().GetVertices();
				data.indices = &m_mesh->GetCollisionTree().GetIndices();
			}

			data.selectedShape = m_selectedShape;
			data.wireframe = m_wireframe;
			m_overlay->Rebuild(data);
			m_overlayDirty = false;
		}

		m_transformWidget->Update(&m_camera);
	}
}
```
Add `#include <cstdio>` for `snprintf`.

`SelectShape` and `DrawPanel` come next, in the same file:
```cpp
	void MeshCollisionEditor::SelectShape(const int32 index)
	{
		m_selection.Clear();
		m_selectedShape = (m_recipe && index >= 0 && index < static_cast<int32>(m_recipe->shapes.size())) ? index : -1;
		// Task 9 adds the SelectedCollisionShape selectable here.
		m_overlayDirty = true;
	}

	void MeshCollisionEditor::DrawPanel()
	{
		int viewMode = m_viewMode;
		ImGui::TextUnformatted("View");
		ImGui::SameLine();
		bool changed = ImGui::RadioButton("Off", &viewMode, collision_view_mode::Off);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Overlay", &viewMode, collision_view_mode::Overlay);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Collision only", &viewMode, collision_view_mode::CollisionOnly);
		if (changed)
		{
			m_viewMode = static_cast<collision_view_mode::Type>(viewMode);
			ApplyViewMode();
		}

		if (ImGui::Checkbox("Wireframe", &m_wireframe))
		{
			m_overlayDirty = true;
		}

		ImGui::TextDisabled("Green walkable, orange too steep, blue shape, red cut, yellow selected.");
		if (!m_status.empty())
		{
			ImGui::TextUnformatted(m_status.c_str());
		}

		ImGui::Separator();

		if (ImGui::Button("Build Complex"))
		{
			Rebake(true);
		}
		ImGui::SameLine();
		if (DrawDangerButton("Clear"))
		{
			ImGui::OpenPopup("Clear collision?");
		}
		if (ImGui::BeginPopupModal("Clear collision?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Removes the baked collision, all shapes and the included submeshes.");
			if (ImGui::Button("Clear"))
			{
				EnsureRecipe();
				SelectShape(-1);
				m_recipe->shapes.clear();
				m_recipe->includedSubMeshes.clear();
				Rebake(true);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		static const char* s_noMaterial = "(No Material)";
		auto subMeshLabel = [this](const uint16 i)
		{
			const SubMesh& sub = m_mesh->GetSubMesh(i);
			return "#" + std::to_string(i + 1) + ": " + (sub.GetMaterial() ? String(sub.GetMaterial()->GetName()) : String(s_noMaterial));
		};

		if (ImGui::CollapsingHeader("Meshes To Include", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Show the current state without creating a recipe; the first change creates it.
			std::vector<uint16> included = m_recipe ? m_recipe->includedSubMeshes : InferIncludedSubMeshes(m_mesh->GetCollisionTree().GetFaceSubMeshes());
			bool useRender = m_recipe ? m_recipe->useRenderGeometry : true;
			bool edited = ImGui::Checkbox("Use render geometry", &useRender);

			if (ImGui::Button("Select All"))
			{
				included = AllSubMeshesWithIndexData(*m_mesh);
				edited = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Deselect All"))
			{
				included.clear();
				edited = true;
			}

			for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
			{
				ImGui::PushID(i);
				bool isIncluded = std::find(included.begin(), included.end(), i) != included.end();
				if (ImGui::Checkbox(subMeshLabel(i).c_str(), &isIncluded))
				{
					if (isIncluded)
					{
						included.push_back(i);
					}
					else
					{
						included.erase(std::remove(included.begin(), included.end(), i), included.end());
					}
					edited = true;
				}
				ImGui::PopID();
			}

			if (edited)
			{
				EnsureRecipe();
				m_recipe->useRenderGeometry = useRender;
				m_recipe->includedSubMeshes = included;
				Rebake(true);
			}
		}

		if (ImGui::CollapsingHeader("Shapes", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!IsActive())
			{
				ImGui::TextDisabled("Switch the view to Overlay or Collision only to edit shapes.");
			}
			ImGui::BeginDisabled(!IsActive());

			for (uint8 t = 0; t < collision_shape_type::Count_; ++t)
			{
				if (t > 0)
				{
					ImGui::SameLine();
				}
				const auto type = static_cast<collision_shape_type::Type>(t);
				if (ImGui::Button((String("+ ") + GetCollisionShapeTypeName(type)).c_str()))
				{
					AddShape(type);
				}
			}

			if (m_recipe)
			{
				for (size_t i = 0; i < m_recipe->shapes.size(); ++i)
				{
					const CollisionShape& shape = m_recipe->shapes[i];
					const String label = shape.name + (shape.op == collision_shape_op::Cut ? "  [Cut]" : "") + "##shape" + std::to_string(i);
					if (ImGui::Selectable(label.c_str(), m_selectedShape == static_cast<int32>(i)))
					{
						SelectShape(static_cast<int32>(i));
					}
				}

				if (CollisionShape* selected = m_selectedShape >= 0 ? GetShape(m_selectedShape) : nullptr)
				{
					ImGui::Separator();
					DrawShapeDetails(*selected);
				}
			}

			ImGui::EndDisabled();
		}
	}

	void MeshCollisionEditor::DrawShapeDetails(CollisionShape& shape)
	{
		bool changed = false;
		const uint32 index = static_cast<uint32>(m_selectedShape);

		ImGui::InputText("Name", &shape.name);
		ImGui::Text("Type: %s", GetCollisionShapeTypeName(shape.type));

		ImGui::BeginDisabled(!CollisionShapeSupportsCut(shape.type));
		int op = shape.op;
		changed |= ImGui::RadioButton("Add", &op, collision_shape_op::Add);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Cut", &op, collision_shape_op::Cut);
		shape.op = static_cast<collision_shape_op::Type>(op);
		ImGui::EndDisabled();

		if (m_mesh->GetSubMeshCount() > 0 && shape.op == collision_shape_op::Add)
		{
			const uint16 current = std::min<uint16>(shape.surfaceSubMesh, m_mesh->GetSubMeshCount() - 1);
			const SubMesh& sub = m_mesh->GetSubMesh(current);
			const String preview = "#" + std::to_string(current + 1) + ": " + (sub.GetMaterial() ? String(sub.GetMaterial()->GetName()) : String("(No Material)"));
			if (ImGui::BeginCombo("Surface", preview.c_str()))
			{
				for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
				{
					const SubMesh& option = m_mesh->GetSubMesh(i);
					const String label = "#" + std::to_string(i + 1) + ": " + (option.GetMaterial() ? String(option.GetMaterial()->GetName()) : String("(No Material)"));
					if (ImGui::Selectable(label.c_str(), i == current))
					{
						shape.surfaceSubMesh = i;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
		}

		changed |= ImGui::DragFloat3("Position", &shape.position.x, 0.05f);
		changed |= DrawQuaternionEulerDegreesControl("Rotation", shape.rotation);
		changed |= ImGui::DragFloat3("Size", &shape.scale.x, 0.05f, 0.001f, 10000.0f);

		if (shape.type == collision_shape_type::Cylinder || shape.type == collision_shape_type::HelixRamp)
		{
			int segments = shape.segments;
			if (ImGui::SliderInt("Segments", &segments, 3, 256))
			{
				shape.segments = static_cast<uint16>(segments);
				changed = true;
			}
		}
		if (shape.type == collision_shape_type::HelixRamp)
		{
			changed |= ImGui::SliderFloat("Inner radius", &shape.innerRadius, 0.05f, 0.95f);
			changed |= ImGui::DragFloat("Sweep (deg)", &shape.sweepDegrees, 1.0f, 1.0f, 3600.0f);
			changed |= ImGui::SliderFloat("Thickness", &shape.thickness, 0.001f, 1.0f);
			changed |= ImGui::Checkbox("Clockwise", &shape.clockwise);
		}
		if (shape.type == collision_shape_type::Plane)
		{
			changed |= ImGui::Checkbox("Two-sided", &shape.twoSided);
		}

		if (ImGui::Button("Duplicate"))
		{
			DuplicateShape(index);
			return;
		}
		ImGui::SameLine();
		if (DrawDangerButton("Delete"))
		{
			DeleteShape(index);
			return;
		}

		if (changed)
		{
			m_recipeTouched = true;
			// Drags fire every frame: preview while the mouse is held; Update() rebuilds the tree on release.
			Rebake(!ImGui::IsMouseDown(ImGuiMouseButton_Left));
		}
	}
```
`DrawQuaternionEulerDegreesControl` and `DrawDangerButton` come from `editor_windows/editor_imgui_helpers.h`. The panel's Duplicate and Delete buttons call `DuplicateShape` / `DeleteShape` directly, and then `return`. That is safe because they are not called from inside a selectable.

- [ ] **Step 3: Wire into MeshEditorInstance**

`mesh_editor_instance.h`:
- add `#include "mesh_collision_editor.h"`;
- replace `std::set<uint16> m_includedSubMeshes;` with `std::unique_ptr<MeshCollisionEditor> m_collisionEditor;`.

`mesh_editor_instance.cpp`:
- Constructor, after the skeleton debug block:
```cpp
		m_collisionEditor = std::make_unique<MeshCollisionEditor>(m_scene, *m_camera, *m_cameraAnchor, m_mesh, m_entity, m_assetPath.string());
```
- Destructor, first line: `m_collisionEditor.reset();`. This must run before the entity is destroyed and before `m_scene.Clear()`.
- `Render()`, just before `m_scene.Render(...)`: `m_collisionEditor->Update();`
- `Save()`: replace the serializer lines with
```cpp
		if (!SaveMeshWithRecipe(m_mesh, GetAssetPath().string(), m_collisionEditor->GetRecipeForSave()))
		{
			return false;
		}
```
  and remove the now-unused `file`, `sink` and `writer` locals that belonged to the mesh write. The skeleton write stays.
- `DrawCollision()`: keep the `Save` button and the skeletal note. Replace everything from `if (ImGui::Button("Clear"))` to the end of the "Meshes To Include" header with `m_collisionEditor->DrawPanel();`.

- [ ] **Step 4: Build and check by hand**

```powershell
cmake --build build --config Debug -t mmo_edit --parallel
```
Expected: success. Then in mmo_edit:
1. Open `Models/Dungeon/Staircase_03.hmsh` and set View to Overlay. The collision should appear: green treads, orange risers.
2. Switch to Collision only. The render mesh should hide.
3. Add a Box. It should appear blue at the orbit point, and Faces should go up by 12.
4. Change its Size, then switch it to Cut. The faces inside it should turn red.
5. Delete it, then close the mesh without saving.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editors/mesh_editor/mesh_collision_editor.h src/mmo_edit/editors/mesh_editor/mesh_collision_editor.cpp src/mmo_edit/editors/mesh_editor/mesh_editor_instance.h src/mmo_edit/editors/mesh_editor/mesh_editor_instance.cpp
git commit -m "feat(mmo_edit): collision view modes and shapes panel in the mesh editor"
```

---

### Task 9: Viewport picking and gizmo

**Files:**
- Modify: `src/mmo_edit/editors/mesh_editor/mesh_collision_editor.h/.cpp`
  - add `SelectedCollisionShape`;
  - add the input methods;
  - add the selectable in `SelectShape`.
- Modify: `src/mmo_edit/editors/mesh_editor/mesh_editor_instance.h/.cpp` (mouse, keys and viewport rect forwarding)

**Interfaces:**
- Consumes: `Selectable` (`selectable.h`), `TransformWidget` (`OnMousePressed/OnMouseReleased/OnMouseMoved(x01, y01)`, `IsActive`, `SetTransformMode`, `SetUseLocalTransform`), `Camera::GetCameraToViewportRay(x01, y01, maxDistance)`.
- Produces, on `MeshCollisionEditor`:
  - `void OnMousePressed(uint32 button, float x, float y)`
  - `void OnMouseReleased(uint32 button, float x, float y, bool wasClick)`
  - `void OnMouseMoved(float x, float y)`
  - `bool IsGizmoActive() const`
  - `void HandleKeys()`

- [ ] **Step 1: Add the selectable**

At the top of `mesh_collision_editor.h`, before `MeshCollisionEditor`, add a forward declaration `class MeshCollisionEditor;`. After the class, add:
```cpp
	/// @brief Gizmo handle for one collision shape; writes transforms back into the recipe.
	class SelectedCollisionShape final : public Selectable
	{
	public:
		/// @brief Selects shape `index` of `editor`.
		SelectedCollisionShape(MeshCollisionEditor& editor, uint32 index);

		void Visit(SelectableVisitor& visitor) override {}
		void Duplicate() override;
		void Translate(const Vector3& delta) override;
		void Rotate(const Quaternion& delta) override;
		void Scale(const Vector3& delta) override;
		void Remove() override;
		void Deselect() override {}
		void SetPosition(const Vector3& position) const override;
		void SetOrientation(const Quaternion& orientation) const override;
		void SetScale(const Vector3& scale) const override;
		Vector3 GetPosition() const override;
		Quaternion GetOrientation() const override;
		Vector3 GetScale() const override;

	private:
		MeshCollisionEditor& m_editor;
		uint32 m_index;
	};
```
In the `.cpp`:
```cpp
	SelectedCollisionShape::SelectedCollisionShape(MeshCollisionEditor& editor, const uint32 index)
		: m_editor(editor)
		, m_index(index)
	{
	}

	void SelectedCollisionShape::Duplicate()
	{
		// Deferred: duplicating reselects, which would destroy this selectable mid-call.
		m_editor.RequestDuplicateShape(m_index);
	}

	void SelectedCollisionShape::Translate(const Vector3& delta)
	{
		if (CollisionShape* shape = m_editor.GetShape(m_index))
		{
			shape->position += delta;
			positionChanged(*this);
			m_editor.OnShapeChanged(false);
		}
	}

	void SelectedCollisionShape::Rotate(const Quaternion& delta)
	{
		if (CollisionShape* shape = m_editor.GetShape(m_index))
		{
			shape->rotation = delta * shape->rotation;
			shape->rotation.Normalize();
			rotationChanged(*this);
			m_editor.OnShapeChanged(false);
		}
	}

	void SelectedCollisionShape::Scale(const Vector3& delta)
	{
		// TransformWidget passes multiplicative per-frame factors (transform_widget.cpp ApplyScale).
		if (CollisionShape* shape = m_editor.GetShape(m_index))
		{
			shape->scale = Vector3(shape->scale.x * delta.x, shape->scale.y * delta.y, shape->scale.z * delta.z);
			scaleChanged(*this);
			m_editor.OnShapeChanged(false);
		}
	}

	void SelectedCollisionShape::Remove()
	{
		// Deferred: deleting clears the selection, which would destroy this selectable mid-call.
		m_editor.RequestDeleteShape(m_index);
	}

	void SelectedCollisionShape::SetPosition(const Vector3& position) const
	{
		if (CollisionShape* shape = m_editor.GetShape(m_index))
		{
			shape->position = position;
			m_editor.OnShapeChanged(false);
		}
	}

	void SelectedCollisionShape::SetOrientation(const Quaternion& orientation) const
	{
		if (CollisionShape* shape = m_editor.GetShape(m_index))
		{
			shape->rotation = orientation;
			m_editor.OnShapeChanged(false);
		}
	}

	void SelectedCollisionShape::SetScale(const Vector3& scale) const
	{
		if (CollisionShape* shape = m_editor.GetShape(m_index))
		{
			shape->scale = scale;
			m_editor.OnShapeChanged(false);
		}
	}

	Vector3 SelectedCollisionShape::GetPosition() const
	{
		const CollisionShape* shape = m_editor.GetShape(m_index);
		return shape ? shape->position : Vector3::Zero;
	}

	Quaternion SelectedCollisionShape::GetOrientation() const
	{
		const CollisionShape* shape = m_editor.GetShape(m_index);
		return shape ? shape->rotation : Quaternion::Identity;
	}

	Vector3 SelectedCollisionShape::GetScale() const
	{
		const CollisionShape* shape = m_editor.GetShape(m_index);
		return shape ? shape->scale : Vector3::UnitScale;
	}
```
The `const` setters call the non-const `GetShape` and `OnShapeChanged` through a reference member. That is fine: `m_editor` is a reference, so constness does not propagate to it.

`Remove()` and `Duplicate()` use the deferred `Request*` calls from Task 8, because both end up re-selecting, which destroys the calling selectable.

In `SelectShape`, after setting `m_selectedShape`:
```cpp
		if (m_selectedShape >= 0)
		{
			m_selection.AddSelectable(std::make_unique<SelectedCollisionShape>(*this, static_cast<uint32>(m_selectedShape)));
		}
```

- [ ] **Step 2: Picking and input forwarding**

Add to `MeshCollisionEditor` (public):
```cpp
		/// @brief Mouse button pressed over the viewport; coordinates are 0..1 within it.
		void OnMousePressed(uint32 button, float x, float y);
		/// @brief Mouse button released; `wasClick` if it barely moved since the press (selects under the cursor).
		void OnMouseReleased(uint32 button, float x, float y, bool wasClick);
		/// @brief Mouse moved; coordinates are 0..1 within the viewport.
		void OnMouseMoved(float x, float y);
		/// @brief Whether the gizmo is being dragged (the caller must not orbit the camera).
		[[nodiscard]] bool IsGizmoActive() const { return m_transformWidget->IsActive(); }
		/// @brief Gizmo mode keys 1-4 and Delete; call once per frame while the viewport is hovered.
		void HandleKeys();
```
Implementation:
```cpp
	void MeshCollisionEditor::OnMousePressed(const uint32 button, const float x, const float y)
	{
		if (IsActive())
		{
			m_transformWidget->OnMousePressed(button, x, y);
		}
	}

	void MeshCollisionEditor::OnMouseReleased(const uint32 button, const float x, const float y, const bool wasClick)
	{
		if (!IsActive())
		{
			return;
		}

		const bool gizmoWasActive = m_transformWidget->IsActive();
		m_transformWidget->OnMouseReleased(button, x, y);
		if (gizmoWasActive)
		{
			Rebake(true);
			return;
		}

		if (button != 0 || !wasClick || !m_recipe)
		{
			return;
		}

		const Ray ray = m_camera.GetCameraToViewportRay(x, y, 10000.0f);
		int32 best = -1;
		float bestDistance = std::numeric_limits<float>::max();
		for (size_t i = 0; i < m_recipe->shapes.size(); ++i)
		{
			std::vector<Vector3> vertices;
			std::vector<uint32> indices;
			TessellateCollisionShape(m_recipe->shapes[i], vertices, indices);
			for (size_t f = 0; f + 2 < indices.size(); f += 3)
			{
				const auto [hit, distance] = ray.IntersectsTriangle(vertices[indices[f]], vertices[indices[f + 1]], vertices[indices[f + 2]]);
				if (hit && distance < bestDistance)
				{
					bestDistance = distance;
					best = static_cast<int32>(i);
				}
			}
		}

		SelectShape(best);
	}

	void MeshCollisionEditor::OnMouseMoved(const float x, const float y)
	{
		if (IsActive())
		{
			m_transformWidget->OnMouseMoved(x, y);
		}
	}

	void MeshCollisionEditor::HandleKeys()
	{
		if (!IsActive() || ImGui::GetIO().WantTextInput)
		{
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_1, false)) { m_transformWidget->SetTransformMode(TransformMode::Translate); }
		if (ImGui::IsKeyPressed(ImGuiKey_2, false)) { m_transformWidget->SetTransformMode(TransformMode::Rotate); }
		if (ImGui::IsKeyPressed(ImGuiKey_3, false)) { m_transformWidget->SetTransformMode(TransformMode::Scale); }
		if (ImGui::IsKeyPressed(ImGuiKey_4, false)) { m_transformWidget->SetUseLocalTransform(!m_transformWidget->IsUsingLocalTransform()); }
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_selectedShape >= 0)
		{
			RequestDeleteShape(static_cast<uint32>(m_selectedShape));
		}
	}
```
Allman style requires each of the single-line `if` blocks above to be expanded to the braces-on-own-line form when writing the file.

Add these includes: `"math/ray.h"`, `<limits>`. The ray's `IntersectsTriangle` distance is in the same units for every shape, so comparing distances is valid.

- [ ] **Step 3: Forward from MeshEditorInstance**

`mesh_editor_instance.h`: add members `ImVec2 m_viewportImageMin {};`, `bool m_viewportHovered { false };` and `int16 m_pressMouseX { 0 }, m_pressMouseY { 0 };`.

`mesh_editor_instance.cpp`:
- In `DrawViewport`, right after `ImGui::Image(...)`:
```cpp
			m_viewportImageMin = ImGui::GetItemRectMin();
			m_viewportHovered = ImGui::IsItemHovered();
			if (m_viewportHovered)
			{
				m_collisionEditor->HandleKeys();
			}
```
- A helper in the anonymous scope of the `.cpp`, or a private method:
```cpp
	ImVec2 MeshEditorInstance::ViewportMouse01() const
	{
		const ImVec2 mouse = ImGui::GetMousePos();
		return ImVec2((mouse.x - m_viewportImageMin.x) / m_lastAvailViewportSize.x, (mouse.y - m_viewportImageMin.y) / m_lastAvailViewportSize.y);
	}
```
  Declare it in the header as a private `ImVec2 ViewportMouse01() const;`.
- `OnMouseButtonDown`: after storing the last mouse position, add
```cpp
		m_pressMouseX = x;
		m_pressMouseY = y;
		if (m_viewportHovered)
		{
			const ImVec2 p = ViewportMouse01();
			m_collisionEditor->OnMousePressed(button, p.x, p.y);
		}
```
- `OnMouseButtonUp`: at the start, add
```cpp
		const ImVec2 p = ViewportMouse01();
		const bool wasClick = std::abs(x - m_pressMouseX) < 4 && std::abs(y - m_pressMouseY) < 4;
		m_collisionEditor->OnMouseReleased(button, p.x, p.y, wasClick && m_viewportHovered);
```
- `OnMouseMoved`: wrap the camera orbit/pan block in `if (!m_collisionEditor->IsGizmoActive()) { ... }` and, after it, add
```cpp
		const ImVec2 p = ViewportMouse01();
		m_collisionEditor->OnMouseMoved(p.x, p.y);
```

- [ ] **Step 4: Build and check by hand**

```powershell
cmake --build build --config Debug -t mmo_edit --parallel
```
Expected: success. Then in mmo_edit:
1. Open Staircase_03 with View set to Overlay, and add a Box.
2. Click on it in the viewport. It should turn yellow, the gizmo should appear, and the list entry should be highlighted.
3. Drag the gizmo's arrow. The box should move and the camera must not orbit.
4. Release the mouse. "Nodes" should update.
5. Press 2 and rotate, then press 3 and scale.
6. Press Del. The box should disappear.
7. Click empty space. The selection should clear.

- [ ] **Step 5: Commit**

```bash
git add src/mmo_edit/editors/mesh_editor/mesh_collision_editor.h src/mmo_edit/editors/mesh_editor/mesh_collision_editor.cpp src/mmo_edit/editors/mesh_editor/mesh_editor_instance.h src/mmo_edit/editors/mesh_editor/mesh_editor_instance.cpp
git commit -m "feat(mmo_edit): pick and transform collision shapes with the gizmo"
```

---

### Task 10: `--rebake-collision` CLI job

**Files:**
- Modify: `src/mmo_edit/mmo_edit.cpp` (the unattended jobs block, ~:321-400, and its header comment)

**Interfaces:**
- Consumes: `RebakeMeshCollisionFile(const String&)` (Task 6).

- [ ] **Step 1: Add the job**

Add the include `#include "editors/mesh_editor/mesh_collision_geometry.h"`. Extend the comment at the top of the jobs block:
```cpp
	// `--rebake-collision <asset.hmsh> [--rebake-collision ...]` rebuilds the meshes' collision trees from
	// their stored collision recipes (editor-only CSRC chunk), saves them and exits.
```
Inside the `for` loop, next to `--rebuild-material`, add:
```cpp
			if (std::string(args[i]) == "--rebake-collision")
			{
				static std::ofstream rebakeLog("collision_rebake.log", std::ios::out | std::ios::trunc);
				static std::mutex rebakeLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ rebakeLogMutex };
					rebakeLog << entry.message << std::endl;
				});

				bool allRebaked = true;
				for (int j = 1; j + 1 < argCount; ++j)
				{
					if (std::string(args[j]) == "--rebake-collision")
					{
						allRebaked = mmo::RebakeMeshCollisionFile(args[j + 1]) && allRebaked;
					}
				}

				PostQuitMessage(allRebaked ? 0 : 1);
				break;
			}
```

- [ ] **Step 2: Build and run against a test copy**

```powershell
cmake --build build --config Debug -t mmo_edit --parallel
```
Then:
1. In the editor, give `Models/Dungeon/Staircase_03.hmsh` one Add box and save.
2. Back up the file first, using a copy outside `data/` in the scratchpad.
3. Run:
```powershell
./bin/Debug/mmo_edit.exe --rebake-collision Models/Dungeon/Staircase_03.hmsh; echo $LASTEXITCODE
Get-Content collision_rebake.log
```
Expected: exit code `0`, and the log line `Rebake collision: Models/Dungeon/Staircase_03.hmsh - N faces, 0 cut, 1 shapes`.

Next, run it on a mesh that has no recipe. Expected: exit `1` and "has no collision recipe".

Finally, restore the backup. Do not commit data changes from this check.

- [ ] **Step 3: Commit**

```bash
git add src/mmo_edit/mmo_edit.cpp
git commit -m "feat(mmo_edit): --rebake-collision unattended job"
```

---

### Task 11: Gate and hand-off

- [ ] **Step 1: Full unit run**

```powershell
cmake --build build --config Debug -t all_tests --parallel
cd build; ctest -C Debug --output-on-failure; cd ..
```
Expected: 100% tests passed.

- [ ] **Step 2: Fast gate**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast
```
Expected: green, including the WSL Linux step (math compiles with gcc). The protocol check must report no change.

If gcc fails, the usual causes are:
- multi-char literal warnings treated as errors: use `MakeChunkMagic` and the existing patterns;
- a missing `<cmath>` or `<limits>` include.

Fix the cause and re-run.

- [ ] **Step 3: Commit any gate fixes**

```bash
git add -A src
git commit -m "fix: gate findings for mesh collision editor"
```
Skip this step if nothing changed.

- [ ] **Step 4: Hand-off note to the user** (in chat; nothing is pushed)

It covers:
- Branch `feature/mesh-collision-editor`, with the commits and the gate result.
- The manual test:
  1. Open Staircase_03 and switch to Collision only.
  2. Add a Cylinder, set it to Cut, and size it around the steps until they turn red.
  3. Add a Helix Ramp that matches the stairs' sweep, direction and height. Its tread should be green.
  4. Save, then reopen to check the shapes persist.
  5. Rebuild the navmesh: `bin/Release/nav_builder.exe -d data/client -w <World> -o data/editor`.
- Notes to pass on:
  - **Existing meshes:** meshes edited this way save at 0x0302. Binaries older than this branch cannot load them. That is fine because the client and its data ship together.
  - **Stair-ramp merge:** the uncommitted stair-to-ramp button in the hollow-choir worktree rewrites the tree directly, so a recipe rebake would undo it. The merge with `mesh_editor_instance.cpp`'s collision panel will conflict and needs resolving.
  - **Mesh with no collision:** the first shape added to a mesh that had no collision does not turn on render geometry, so only the shapes collide. This refines the spec's fallback, which applied only to a legacy BVH1 tree.
  - **Helix inner radius** is clamped to at least 0.05, not the spec's 0. At exactly 0 the inner wall is degenerate and the shape would no longer be closed.
