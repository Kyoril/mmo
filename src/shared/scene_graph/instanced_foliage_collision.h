// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "movable_object.h"
#include "mesh.h"
#include "base/typedefs.h"
#include "math/aabb.h"
#include "math/matrix4.h"

#include <vector>

namespace mmo
{
	/// @brief Collision proxy for a batch of authored foliage instances that share a single mesh.
	/// @details Instanced foliage is rendered with a hardware-instanced FoliageChunk per (cell, mesh,
	///          submesh), which is great for rendering but carries no per-instance collision geometry.
	///          This lightweight, non-rendered MovableObject sits alongside the render chunks in a cell
	///          and exposes the mesh's collision tree for every collidable instance of one mesh, so that
	///          the regular scene AABB queries used by movement and the camera pick foliage up just like
	///          static mesh entities.
	///
	///          Performance: every instance keeps a precomputed world-space AABB so that capsule/ray
	///          tests cheaply reject the (typically many) instances that are nowhere near the query
	///          before traversing the (relatively expensive) per-instance collision tree.
	class InstancedFoliageCollision final : public MovableObject, public ICollidable
	{
	public:
		/// @brief Creates a collision proxy for a single mesh.
		/// @param name Unique name for the movable object.
		/// @param mesh The mesh whose collision tree is shared by all instances added here.
		InstancedFoliageCollision(const String& name, MeshPtr mesh);

	public:
		/// @brief Adds a collidable instance with the given world transform.
		void AddInstance(const Matrix4& worldTransform);

		/// @brief Recomputes the aggregate bounding box/radius after all instances were added.
		void Finalize();

		/// @brief Gets the number of collidable instances.
		[[nodiscard]] size_t GetInstanceCount() const { return m_instances.size(); }

		// MovableObject -----------------------------------------------------------------------
		[[nodiscard]] const String& GetMovableType() const override;
		[[nodiscard]] const AABB& GetBoundingBox() const override { return m_bounds; }
		[[nodiscard]] float GetBoundingRadius() const override { return m_boundingRadius; }
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables = false) override {}
		void PopulateRenderQueue(RenderQueue& queue) override {}

		ICollidable* GetCollidable() override { return this; }
		const ICollidable* GetCollidable() const override { return this; }

		// ICollidable -------------------------------------------------------------------------
		bool TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const override;
		bool TestRayCollision(const Ray& ray, CollisionResult& result) const override;
		[[nodiscard]] bool IsCollidable() const override
		{
			return !m_instances.empty() && m_mesh && !m_mesh->GetCollisionTree().IsEmpty();
		}

	private:
		/// @brief A single collidable placement of the shared mesh.
		struct Instance
		{
			Matrix4 worldTransform;     ///< Local-to-world transform of this instance.
			Matrix4 invWorldTransform;  ///< World-to-local transform (used for ray tests in local space).
			AABB worldBounds;           ///< World-space AABB of the mesh bounds (broad-phase reject).
		};

		static String s_movableType;

		MeshPtr m_mesh;
		std::vector<Instance> m_instances;
		AABB m_bounds;
		float m_boundingRadius = 0.0f;
	};
}
