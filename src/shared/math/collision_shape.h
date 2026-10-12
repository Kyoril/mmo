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
		Quaternion rotation { 1.0f, 0.0f, 0.0f, 0.0f };
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
