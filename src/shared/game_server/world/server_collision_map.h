// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/aabb.h"
#include "math/aabb_tree.h"
#include "math/matrix4.h"
#include "math/vector3.h"

#include <memory>
#include <vector>

namespace mmo
{
	/// @brief One placed collision object in the world.
	/// The AABBTree is stored in local (mesh) space and shared across all instances
	/// that use the same mesh, so memory is not duplicated.
	struct CollisionInstance
	{
		AABB                        worldBounds;    ///< World-space AABB — used for fast ray rejection.
		Matrix4                     transform;      ///< Local → world (for hit-point reconstruction).
		Matrix4                     invTransform;   ///< World → local (for ray transformation).
		std::shared_ptr<AABBTree>   tree;           ///< Local-space collision tree (shared, not owned).
	};

	/// @brief Loads world collision geometry and provides fast per-entity ray testing.
	///
	/// Each mesh's AABBTree is loaded once and shared by all instances that reference it.
	/// A LOS ray is first tested against each entity's world-space AABB; only entities
	/// whose AABB is intersected have their local-space AABBTree tested, with the ray
	/// transformed into local space first.
	///
	/// No geometry is merged, so load time and memory scale with the number of unique
	/// meshes, not with the total triangle count of the world.
	class ServerCollisionMap final : public NonCopyable
	{
	public:
		/// @brief Loads all collision instances for the given world.
		/// @param mapName The map directory name (same as proto::MapEntry::directory()).
		explicit ServerCollisionMap(const std::string& mapName);
		~ServerCollisionMap() override = default;

		/// @brief Returns true when at least one collision instance was loaded.
		[[nodiscard]] bool IsLoaded() const { return !m_instances.empty(); }

		/// @brief Returns true when the line from @p from to @p to is unobstructed.
		/// Both points should already be at eye height.
		[[nodiscard]] bool LineOfSight(const Vector3& from, const Vector3& to) const;

		/// @brief Like LineOfSight but also reports the closest obstruction position.
		/// @param hitPoint Set to the first hit when returning false, otherwise equals @p to.
		[[nodiscard]] bool LineOfSightEx(const Vector3& from, const Vector3& to, Vector3& hitPoint) const;

	private:
		/// Loads just the COLL chunk from a .mesh file into a shared AABBTree.
		/// Returns nullptr if the file has no collision tree.
		static std::shared_ptr<AABBTree> LoadMeshTree(const std::string& meshPath);

		/// Reads all visible mesh refs from a .hwmo world model file and appends
		/// CollisionInstances for each, combining the instance transform with the
		/// per-mesh-ref transform.
		void LoadWorldModelInstances(const std::string& hwmoPath,
		                             const Matrix4& instanceTransform);

		void AddInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform);

	private:
		std::vector<CollisionInstance> m_instances;
	};
}
