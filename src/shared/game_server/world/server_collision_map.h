// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/aabb.h"
#include "math/aabb_tree.h"
#include "math/matrix4.h"
#include "math/vector3.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
		/// @brief Creates an empty collision map without any static geometry. Dynamic
		/// instances can still be added — used by maps without static world geometry
		/// and by unit tests.
		ServerCollisionMap() = default;

		/// @brief Loads all collision instances for the given world.
		/// @param mapName The map directory name (same as proto::MapEntry::directory()).
		explicit ServerCollisionMap(const std::string& mapName);
		~ServerCollisionMap() override = default;

		/// @brief Creates a per-instance view over static geometry shared by every instance of a map.
		/// @details Line of sight tests the shared static geometry and this view's own dynamic
		/// instances. Dynamic instances added here are never visible to other views.
		/// @param sharedStatic Static geometry, typically loaded once per map. May be null.
		explicit ServerCollisionMap(std::shared_ptr<const ServerCollisionMap> sharedStatic);

		/// @brief Returns true when at least one static collision instance was loaded.
		[[nodiscard]] bool IsLoaded() const { return !m_instances.empty() || (m_sharedStatic && m_sharedStatic->IsLoaded()); }

		/// @brief Gets the mesh-space bounds of a dynamic instance's collision tree.
		/// @return False for unknown handles.
		[[nodiscard]] bool GetDynamicInstanceLocalBounds(uint64 handle, AABB& out) const;

		/// @brief Returns true when the line from @p from to @p to is unobstructed.
		/// Both points should already be at eye height. Points less than 1 cm apart are
		/// degenerate and always report true — no geometry fits between them.
		[[nodiscard]] bool LineOfSight(const Vector3& from, const Vector3& to) const;

		/// @brief Like LineOfSight but also reports the closest obstruction position.
		/// @param hitPoint Set to the first hit when returning false, otherwise equals @p to.
		/// Degenerate queries (see LineOfSight) return true with @p hitPoint set to @p to.
		[[nodiscard]] bool LineOfSightEx(const Vector3& from, const Vector3& to, Vector3& hitPoint) const;

	public:
		/// @brief Registers a dynamic (toggleable) collision instance from an already loaded tree.
		///
		/// NOTE: Dynamic instances are not thread safe by design — the world server runs its
		/// io_service single threaded, so all mutation and all LoS queries happen on the same
		/// thread. Revisit this if the world server ever goes multi threaded.
		/// @param tree The local-space collision tree (shared, not copied).
		/// @param transform The local → world transform of the instance. Must be invertible.
		/// @param enabled Whether the instance initially blocks rays.
		/// @param debugName Name of the source asset, used in the log message when the instance is rejected.
		/// @return Handle for later removal/toggling, or 0 if the tree is null or empty, or if the
		///	        transform is not invertible.
		uint64 AddDynamicInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, bool enabled,
			const std::string& debugName = "<unnamed>");

		/// @brief Like AddDynamicInstance but resolves the tree from a mesh file's COLL chunk.
		/// Trees are cached per mesh path, so despawn/respawn cycles reuse them.
		/// @return Handle for later removal/toggling, or 0 if the mesh has no collision tree.
		uint64 AddDynamicInstanceFromMesh(const std::string& meshPath, const Matrix4& transform, bool enabled);

		/// @brief Removes a dynamic collision instance. Unknown handles are a safe no-op.
		void RemoveDynamicInstance(uint64 handle);

		/// @brief Enables or disables a dynamic collision instance. Unknown handles are a safe no-op.
		void SetDynamicInstanceEnabled(uint64 handle, bool enabled);

	public:
		/// @brief Height, in the foliage mesh's local space, above which foliage geometry no longer
		///        blocks line of sight.
		///
		/// Foliage collision meshes include the whole canopy as solid geometry, while the rendered
		/// leaves are alpha-tested and visibly see-through. Looking over trees on lower ground (or
		/// under a wide crown) therefore failed line of sight although the target was in plain view.
		/// Only the trunk keeps blocking. The value is in local mesh units, so it scales with the
		/// instance: the crowns of the shipped trees start at about 5 m (Tree_02), and 4 m keeps the
		/// whole trunk below that while dropping the crown. A fixed constant is used instead of
		/// deriving a per-mesh crown height because trunk-only meshes (SM_Tree_05) have no crown to
		/// detect, and a heuristic would silently change with every re-exported tree.
		static constexpr float FoliageLineOfSightTrunkHeight = 4.0f;

		/// @brief Registers a static, always-blocking collision instance (entity, world model part).
		/// @details When a map's static geometry is shared between instances (see the shared-static
		/// constructor), add static instances only while building it, before it is shared.
		/// @return False when the instance was not registered, either because the tree is null or
		///         empty or because MakeInstance rejected the transform.
		bool AddInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, const std::string& debugName);

		/// @brief Registers a static foliage instance. Only its trunk (see
		///        FoliageLineOfSightTrunkHeight) blocks line of sight.
		/// @param tree The full local-space collision tree of the foliage mesh. It is filtered here;
		///        callers registering many instances of one mesh should filter once with
		///        MakeFoliageLineOfSightTree and use AddInstance instead.
		/// @return False when nothing was registered (no trunk geometry or a rejected transform).
		bool AddFoliageInstance(const std::shared_ptr<AABBTree>& tree, const Matrix4& transform, const std::string& debugName);

		/// @brief Builds the line of sight tree of a foliage mesh: only triangles with at least one
		///        vertex below FoliageLineOfSightTrunkHeight are kept.
		/// @return The input tree itself when nothing is above the trunk height, nullptr when no
		///         triangle is below it, otherwise a new filtered tree.
		static std::shared_ptr<AABBTree> MakeFoliageLineOfSightTree(const std::shared_ptr<AABBTree>& tree);

	private:
		/// Loads just the COLL chunk from a .mesh file into a shared AABBTree.
		/// Returns nullptr if the file has no collision tree.
		static std::shared_ptr<AABBTree> LoadMeshTree(const std::string& meshPath);

		/// Reads all visible mesh refs from a .hwmo world model file and appends
		/// CollisionInstances for each, combining the instance transform with the
		/// per-mesh-ref transform.
		void LoadWorldModelInstances(const std::string& hwmoPath,
		                             const Matrix4& instanceTransform);

		/// Builds a CollisionInstance (world bounds + inverse transform) from a tree and transform.
		/// @param debugName Name of the source asset, used in the log message on rejection.
		/// @param outInstance Receives the instance. Only written when this returns true.
		/// @return False when the transform is not affine, not finite, or not invertible, in which
		///         case the caller must not register the instance — see the implementation for why
		///         a bad inverse is worse than a missing instance.
		bool MakeInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform,
			const std::string& debugName, CollisionInstance& outInstance);

		/// Logs a rejected transform once per asset — see m_warnedTransforms.
		void WarnRejectedTransform(const std::string& debugName, const char* reason);

	private:
		/// @brief A toggleable collision instance for spawned world objects (e.g. doors).
		struct DynamicInstance
		{
			CollisionInstance instance;
			bool enabled { true };
		};

	private:
		/// Static geometry shared by every instance of the map. Null for a standalone map.
		std::shared_ptr<const ServerCollisionMap> m_sharedStatic;
		std::vector<CollisionInstance> m_instances;
		std::map<uint64, DynamicInstance> m_dynamicInstances;
		uint64 m_nextDynamicHandle { 1 };

		/// Per-mesh-path tree cache for dynamic instances (static loading uses local caches).
		/// Also caches load failures as nullptr so missing meshes aren't re-read from disk.
		std::unordered_map<std::string, std::shared_ptr<AABBTree>> m_meshTreeCache;

		/// Assets already warned about in MakeInstance. One bad brush stroke can place hundreds of
		/// foliage instances of the same mesh, and every one of them would otherwise log — so each
		/// offending asset is named once per map.
		std::unordered_set<std::string> m_warnedTransforms;
	};
}
