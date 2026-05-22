#pragma once

#include "scene_graph/movable_object.h"
#include "math/capsule.h"
#include "math/collision.h"
#include "math/ray.h"

#include <array>
#include <vector>

namespace mmo
{
	/// @brief A collidable consisting of a list of triangles, for use in movement tests.
	/// Implements ICollidable so UnitMovement can sweep capsules against it.
	class TriangleCollidable final : public MovableObject, public ICollidable
	{
	public:
		using Triangle = std::array<Vector3, 3>;

		explicit TriangleCollidable() = default;

		void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c)
		{
			m_triangles.push_back({ a, b, c });
		}

		// ICollidable
		bool TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const override;
		bool TestRayCollision(const Ray& ray, CollisionResult& result) const override;
		bool IsCollidable() const override { return true; }

		// MovableObject
		ICollidable* GetCollidable() override { return this; }
		const ICollidable* GetCollidable() const override { return this; }

		const String& GetMovableType() const override
		{
			static const String s_type = "TriangleCollidable";
			return s_type;
		}

		void PopulateRenderQueue(RenderQueue& /*queue*/) override {}

	private:
		std::vector<Triangle> m_triangles;
	};
}
