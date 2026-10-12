# Mesh collision editor — design

Date: 2026-10-10 · Branch: `feature/mesh-collision-editor`

## Goal

Let the user fix mesh collision by hand in mmo_edit's mesh editor: see the collision, add
primitive collision shapes, move/rotate/scale them with the transform gizmo, and bake the
result into the mesh's existing collision tree. Motivating case: spiral staircases such as
`Models/Dungeon/Staircase_03.hmsh`, whose steps should walk like a ramp.

Non-goals for this round: a Collision mode in the world model editor, stripping editor-only
chunks when packaging the client, undo, convex hull shapes, and making the uncommitted
stair-to-ramp tool (hollow-choir worktree) a recipe step.

## Decisions (made with the user)

- Shapes **add** triangles or **cut** render-derived triangles (Add + Cut).
- Shape types: **Box, Wedge, Cylinder, Helix ramp, Plane**.
- The shapes and their bake inputs (the *collision recipe*) are stored in a new **editor-only**
  `.hmsh` chunk. Runtime readers skip it; only the editor reads it and rebuilds collision from it.
  Stripping it from client packages comes later.
- World model editor collision overlay comes later, separately. The overlay renderer must be
  reusable for it.

## What does not change

The client capsule movement, server line of sight (`ServerCollisionMap`) and the nav builder
consume only the baked `AABBTree` in the `COLL` chunk. Its format (BVH2) is unchanged, so none of
them change and there is no wire-protocol change and no protocol version bump.

## 1. Shapes and bake (`src/shared/math/`, pure, Linux-buildable)

### `collision_shape.h/.cpp`

```cpp
namespace collision_shape_type { enum Type : uint8 { Box, Wedge, Cylinder, HelixRamp, Plane, Count_ }; }
namespace collision_shape_op   { enum Type : uint8 { Add, Cut }; }

struct CollisionShape
{
	collision_shape_type::Type type;
	collision_shape_op::Type op;
	String name;
	Vector3 position;
	Quaternion rotation;
	Vector3 scale;              // dimensions of the unit primitive
	uint16 surfaceSubMesh;      // face submesh id for the baked faces (footstep surface)
	uint16 segments;            // cylinder sides / helix segments
	float innerRadius;          // helix: inner radius as fraction of outer (0..<1)
	float sweepDegrees;         // helix: total turn, may exceed 360
	float thickness;            // helix: tread thickness, unit-height fraction
	bool clockwise;             // helix: turn direction when rising
	bool twoSided;              // plane only
};
```

Unit primitives in shape-local space (scale maps them to world size, then rotation, then
position):

| Type | Unit primitive | Cut allowed |
|---|---|---|
| Box | cube, −0.5..0.5 on all axes | yes |
| Wedge | box footprint −0.5..0.5 in X/Z, bottom at y=−0.5; top slopes from y=−0.5 at −Z to y=0.5 at +Z (a ramp rising along +Z), closed with sloped top, bottom, back face and two triangular sides | yes |
| Cylinder | radius 0.5, y −0.5..0.5, `segments` sides, capped | yes |
| HelixRamp | spiral band between `innerRadius`·0.5 and 0.5 around +Y, rising from y=−0.5 to 0.5 over `sweepDegrees` in `segments` steps; walkable top surface, underside offset down by `thickness`, inner and outer side walls, end caps | yes (volume = the band including its thickness) |
| Plane | quad −0.5..0.5 in X/Z at y=0, normal +Y; optional reverse face when `twoSided` | **no** (no volume; the UI disables Cut) |

Functions:
- `void TessellateCollisionShape(const CollisionShape&, std::vector<Vector3>& verts, std::vector<uint32>& indices)`
  appends world-space (mesh-space) triangles. Closed shapes are outward-wound with the same
  winding convention as the render-derived triangles (verified by a test against
  `AABBTree::IntersectRay` with `IgnoreBackface`).
- `bool IsPointInsideCollisionShape(const CollisionShape&, const Vector3& point)` tests in shape-local
  space. Always false for planes.
- `Matrix4 GetCollisionShapeTransform(const CollisionShape&)`.
- Sanitising: segments are clamped to 3..256, the helix inner radius to 0..0.95, the scale to at least 0.001 per axis.

Helix rise per segment equals `height · (segmentSweep / sweep)`. This makes one revolution of
Staircase_03 a single continuous slope that the client and Recast both walk.

### `collision_recipe.h/.cpp`

```cpp
struct CollisionRecipe
{
	bool useRenderGeometry { true };
	std::vector<uint16> includedSubMeshes;
	std::vector<CollisionShape> shapes;
};
io::Writer& operator<<(io::Writer&, const CollisionRecipe&);
io::Reader& operator>>(io::Reader&, CollisionRecipe&);
```

Serialized with its own `uint32` recipe version (start at 1), so shape fields can grow without
touching the mesh version. Unknown higher recipe versions fail the read (editor warns, keeps the
baked tree untouched).

### `collision_bake.h/.cpp`

```cpp
struct CollisionBakeResult
{
	std::vector<Vector3> vertices;
	std::vector<uint32> indices;
	std::vector<uint16> faceSubMeshes;
	uint32 cutFaces { 0 };                 // render faces removed by Cut shapes
	std::vector<uint32> cutFaceIndices;    // for the overlay (render-face numbering)
	std::vector<int32> faceShape;          // per baked face: shape index, or -1 if render-derived
};

CollisionBakeResult BakeCollision(const std::vector<Vector3>& renderVertices,
	const std::vector<uint32>& renderIndices, const std::vector<uint16>& renderFaceSubMeshes,
	const std::vector<CollisionShape>& shapes);
```

The bake works in this order:
1. Start from the render triangles.
2. Drop every face whose centroid is inside any Cut shape.
3. Append each Add shape's triangles, tagging them with that shape's `surfaceSubMesh`.

Cut shapes act only on render-derived faces, never on other shapes. A large triangle that sticks
out of a Cut volume stays unless its centroid is inside; the overlay shows the cut faces so this
is visible. Unused vertices left behind by cut faces are compacted. The caller passes the result
to `AABBTree::Build(vertices, indices, faceSubMeshes)`.

## 2. File format (`src/shared/scene_graph/mesh_serializer.*`)

- **Version.** Add `mesh_version::Version_0_3_2 = 0x0302` and make `Latest` map to it.
- **New chunk.** Add an editor-only chunk magic `CSRC`. `MeshSerializer::Serialize` gains an
  optional `const CollisionRecipe* recipe = nullptr` parameter and writes `CSRC` only when it is
  non-null.
- **Reading.** For version ≥ 0x0302, `MeshDeserializer` registers a handler that skips `CSRC`. The
  chunk reader already seeks to the chunk end. It also sets `IgnoreUnhandledChunks(true)`, so any
  future optional chunk never breaks readers of this generation again. The runtime `Mesh` gets no
  recipe member.
- **Older files.** Files at 0x0300 and 0x0301 load exactly as before.
- **Editor helper.** `bool ReadCollisionRecipe(std::istream&, CollisionRecipe&)` (scene_graph) scans
  the top-level chunks for `CSRC`, the same way `ServerCollisionMap::LoadMeshTree` scans for
  `COLL`. The mesh editor uses it when it opens a mesh.
- **Compatibility.** Binaries older than this change cannot load meshes saved at 0x0302, whether
  or not they have a recipe. That is acceptable because the client binary and its data ship
  together in the nightly patch, and the servers and nav_builder read only `COLL` or use the new
  deserializer.

## 3. Mesh editor UI (`src/mmo_edit/editors/mesh_editor/`)

### Component structure

New component `mesh_collision_editor.{h,cpp}`, class `MeshCollisionEditor`. It is owned by
`MeshEditorInstance`, which forwards render, mouse and key events and calls `DrawPanel()` from
`DrawCollision`. Keeping the edits to `mesh_editor_instance.cpp` small keeps the merge with the
uncommitted stair-to-ramp change simple.

The component owns:
- the `CollisionRecipe` (loaded via `ReadCollisionRecipe`; null until the first edit);
- the overlay;
- a `Selection` and a `TransformWidget`;
- `SelectedCollisionShape : Selectable`, which writes position, rotation and scale back to the
  recipe shape.

### Overlay renderer

`collision_overlay.{h,cpp}` is reusable later by the world model editor:
- **Triangles:** one `ManualRenderObject` with semi-transparent triangles in
  `Models/Engine/AxisPlaneHighlight.hmat`. Both windings are emitted so the overlay is visible
  from inside. It does not cast shadows and has no query flags.
- **Wireframe:** optional lines in `Editor/Wireframe.hmat`.
- **Colours:**
  - green: walkable, face normal.y ≥ 0.71, the client's `SetWalkableFloorY(0.71f)`;
  - orange: too steep;
  - blue tint: from a shape;
  - red: render faces a Cut shape removed;
  - yellow: the selected shape.
- **View modes:** Off, Overlay (on top of the render mesh), or Collision only (hides the render
  entity). The Wireframe toggle is separate.
- **No cached tree.** The overlay draws from the latest `CollisionBakeResult`, or from the tree's
  vertices and indices when no recipe exists.

### Panel

The panel extends the existing Collision panel:
- view mode and wireframe toggles;
- "Use render geometry" and the existing "Meshes To Include" checkboxes, now persisted in the
  recipe;
- Add Box / Wedge / Cylinder / Helix / Plane buttons. A new shape is placed at the camera's orbit
  point with a size relative to the mesh bounds;
- a shape list showing the name and Add/Cut state. Selecting an entry selects the shape in the
  viewport;
- for the selected shape:
  - name and Add/Cut (disabled for Plane);
  - surface submesh, a combo of the mesh's submeshes by material name;
  - type parameters;
  - numeric position, rotation (Euler degrees) and scale;
  - Duplicate and Delete;
- status: baked face count, cut face count, node count.

### Viewport and baking

- **Viewport:**
  - Left-click ray-picks the nearest shape (against its tessellation) and selects it; clicking
    empty space deselects.
  - The gizmo works as in the world model editor: keys 1–3 switch mode, 4 toggles local axes,
    Del deletes the selected shape.
  - Mouse coordinates go to the widget as 0..1.
- **Baking:**
  - Every recipe edit re-bakes into `m_mesh->GetCollisionTree()` and refreshes the overlay.
  - During a gizmo drag only the dragged shape's preview is updated; the full re-bake runs on
    release.
  - "Build Complex" bakes from the recipe, creating one if necessary.
  - "Clear" also clears the recipe's shapes, after a confirmation.
- **Migrating legacy meshes:**
  - The first edit on a mesh without a recipe creates one.
  - Its `includedSubMeshes` are taken from the distinct values of the existing BVH2 tree's face
    submesh ids, so re-baking reproduces today's collision.
  - With a BVH1 or empty tree it falls back to every submesh that has index data, and logs a
    warning.
- **Save:** Save writes the mesh with `Serialize(m_mesh, writer, Latest, recipe)`.

## 4. Unattended job (`src/mmo_edit/mmo_edit.cpp`)

`mmo_edit --rebake-collision <asset.hmsh> [--rebake-collision ...]`, implemented as
`static bool MeshCollisionEditor::RebakeCollisionInFile(const String& assetPath)`:
1. Load the mesh and read the recipe.
2. If there is no recipe, fail and say so.
3. Bake, then save at 0x0302 with the recipe.
4. Log to `collision_rebake.log`; exit 0 only if every file succeeded.

It follows the `--rebuild-material` pattern. It is useful after re-importing a mesh, or for batch
work.

## 5. Testing

- **`math_tests`** (`test_collision_shape.cpp`, `test_collision_bake.cpp`,
  `test_collision_recipe.cpp`):
  - each closed shape's tessellation is watertight (every edge shared by exactly two faces) and
    outward-facing (a ray from outside hits front faces with `IgnoreBackface`);
  - tessellated dimensions match the scale;
  - helix: first and last tread heights and a constant rise per segment;
  - `IsPointInsideCollisionShape` for each type, inside and outside, under rotation and scale;
  - planes are never inside; a two-sided plane is hit from both sides;
  - bake: Cut removes exactly the faces whose centroid is inside; Add appends with the right
    `surfaceSubMesh`; vertices are compacted; `faceShape` mapping is correct;
  - recipe: round-trip, and an unknown recipe version fails cleanly.
- **`scene_graph_tests`:**
  - a mesh serialized at 0x0302 with a recipe deserializes with an identical collision tree, and
    `ReadCollisionRecipe` returns the recipe;
  - a mesh serialized at 0x0301 still loads;
  - an unknown extra chunk in a 0x0302 file is ignored.
- **Builds and gate:** build `mmo_edit` and run `tools/gate/verify.ps1 -Tier fast`, which also
  covers Linux via WSL.
- **Hand test** (the user):
  1. Open Staircase_03 and switch to Collision only: the steps show green treads and orange risers.
  2. Add a Cut cylinder around the steps (red faces appear) and an Add helix matching their sweep.
  3. Check the helix is green, then save.
  4. Reopen: the shapes persist.
  5. Rebuild the navmesh of a world using it:
     `bin/Release/nav_builder.exe -d data/client -w <World> -o data/editor`.

## Risks

- **Centroid-based cutting.** It may leave long triangles. The red overlay makes this visible;
  splitting triangles at the cut boundary is a possible follow-up.
- **Stair-to-ramp.** The uncommitted stair-to-ramp button rewrites the baked tree directly. A
  later recipe re-bake would undo it. Flag this when the two branches meet; follow-up: a
  "stairs to ramp" recipe step.
- **Overlay material.** `AxisPlaneHighlight.hmat` must honour vertex colour. The transform widget
  and the flatten disc already rely on it. If it does not, use one material instance per colour
  class.
