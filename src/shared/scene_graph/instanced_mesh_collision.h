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
	/// @brief Collision proxy for a batch of hardware-instanced placements that share a single mesh.
	/// @details Instanced rendering draws many placements of one submesh in a single draw call, which is
	///          great for rendering but carries no per-instance collision geometry. This lightweight,
	///          non-rendered MovableObject sits alongside the render batches and exposes the mesh's
	///          collision tree for every collidable instance of one mesh, so that the regular scene AABB
	///          queries used by movement, the camera and picking pick those placements up just like
	///          static mesh entities.
	///
	///          Used by both instanced foliage (one proxy per cell and mesh) and world model instances
	///          (one proxy per room and mesh).
	///
	///          Performance: every instance keeps a precomputed world-space AABB so that capsule/ray
	///          tests cheaply reject the (typically many) instances that are nowhere near the query
	///          before traversing the (relatively expensive) per-instance collision tree.
	class InstancedMeshCollision final : public MovableObject, public ICollidable
	{
	public:
		/// @brief Creates a collision proxy for a single mesh.
		/// @param name Unique name for the movable object.
		/// @param mesh The mesh whose collision tree is shared by all instances added here.
		InstancedMeshCollision(const String& name, MeshPtr mesh);

	public:
		/// @brief Overrides the material used to resolve surface types for every instance.
		/// @details Mirrors Entity::SetMaterial, which replaces the material of *all* submeshes.
		///          Leave unset to resolve per hit face through the mesh's own submesh materials.
		/// @param material The overriding material, or nullptr to use the mesh's own materials.
		void SetMaterialOverride(MaterialPtr material) { m_materialOverride = std::move(material); }

	public:
		/// @brief Adds a collidable instance with the given world transform.
		/// @param worldTransform The local-to-world transform of the instance. Must be affine and
		///        invertible; authored data with a zero scale component is not.
		/// @return False when the instance was rejected because its transform cannot be inverted,
		///         in which case it is not registered — see the implementation for why a NaN
		///         inverse is worse than a missing instance.
		bool AddInstance(const Matrix4& worldTransform);

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

		/// @copydoc ICollidable::GetSurfaceTypeAt
		/// @details Resolves exactly like Entity::GetSurfaceTypeAt: the hit face maps to its source
		///          submesh through the collision tree, and that submesh's material carries the
		///          surface type. Without this the footstep surface query would fall back to 0 for
		///          every instanced placement.
		[[nodiscard]] uint32 GetSurfaceTypeAt(const CollisionResult& hit) const override;

	private:
		/// @brief A single collidable placement of the shared mesh.
		struct Instance
		{
			Matrix4 worldTransform;     ///< Local-to-world transform of this instance.
			Matrix4 invWorldTransform;  ///< World-to-local transform (used for ray tests in local space).
			AABB worldBounds;           ///< World-space AABB of the mesh bounds (broad-phase reject).
		};

		/// @brief Logs a one-off warning naming this proxy and why its instances were rejected.
		void WarnRejectedTransform(const char* reason);

		static String s_movableType;

		MeshPtr m_mesh;
		/// Optional material that overrides every submesh material when resolving surface types.
		MaterialPtr m_materialOverride;
		std::vector<Instance> m_instances;
		AABB m_bounds;
		float m_boundingRadius = 0.0f;
		/// Set once the first instance was rejected, so one batch of bad data logs one line.
		bool m_warnedRejectedTransform = false;
	};
}
