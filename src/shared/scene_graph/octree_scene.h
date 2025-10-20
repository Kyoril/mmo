#pragma once

#include "scene.h"
#include "octree.h"
#include "math/plane.h"
#include <array>
#include <memory>

namespace mmo
{
	class OctreeScene;
	class OctreeNode;
	class Camera;
	class Octree;

	// Cache for camera frustum planes to avoid recalculating them for each node
	struct CachedFrustumPlanes 
	{
		std::array<Plane, 6> planes;
		float farDistance;
		
		// Extract and cache the frustum planes from a camera
		explicit CachedFrustumPlanes(const Camera& camera);
		
		// Check if an AABB is visible using the cached planes
		AABBVisibility GetVisibility(const AABB& bound) const;
		
		// Check if an AABB is visible (simple bool version)
		bool IsVisible(const AABB& bound) const;
	};

	/// @brief Optimized ray scene query that uses octree spatial acceleration
	/// 
	/// This class provides a much more efficient ray casting implementation compared to
	/// the base RaySceneQuery which iterates through all entities in the scene.
	/// Instead, it uses the octree spatial structure to only test objects that are
	/// in octree nodes intersected by the ray.
	///
	/// Usage example:
	/// @code
	/// // Create ray query (automatically returns optimized version for OctreeScene)
	/// auto rayQuery = octreeScene.CreateRayQuery(ray);
	/// rayQuery->SetSortByDistance(true, 10); // Sort by distance, max 10 results
	/// 
	/// // Execute query
	/// const auto& results = rayQuery->Execute();
	/// for (const auto& result : results) {
	///     // Process hit result
	///     float distance = result.distance;
	///     MovableObject* object = result.movable;
	/// }
	/// @endcode
	class OctreeRaySceneQuery : public RaySceneQuery
	{
	private:
		OctreeScene& m_octreeScene;

	public:
		/// @brief Creates an octree-optimized ray scene query
		/// @param scene The octree scene to query against
		explicit OctreeRaySceneQuery(OctreeScene& scene);

	public:
		/// @brief Executes the query with a specific listener using octree acceleration
		/// @param listener The listener to use for reporting hit results
		void Execute(RaySceneQueryListener& listener) override;

	private:
		/// @brief Recursively walks the octree to find ray intersections
		/// @param listener The listener for hit results
		/// @param octant The current octree node being processed
		/// @param ray The ray being tested
		/// @return false if the query should stop early
		bool WalkOctreeForRay(RaySceneQueryListener& listener, Octree& octant, const Ray& ray);
	};

	/// @brief Optimized AABB scene query that uses octree spatial acceleration
	/// 
	/// This class provides efficient AABB queries for finding all MovableObjects
	/// whose bounding boxes intersect with a query AABB. It uses the octree
	/// spatial structure to only test objects in relevant octree nodes.
	class OctreeAABBSceneQuery : public AABBSceneQuery
	{
	private:
		OctreeScene& m_octreeScene;

	public:
		/// @brief Creates an octree-optimized AABB scene query
		/// @param scene The octree scene to query against
		explicit OctreeAABBSceneQuery(OctreeScene& scene);

	public:
		/// @brief Executes the AABB query using octree acceleration
		/// @param listener The listener to call on hit results
		void Execute(SceneQueryListener& listener) override;

	private:
		/// @brief Recursively walks the octree to find AABB intersections
		/// @param listener The listener to call on hit results
		/// @param octant The current octree node being processed
		/// @param queryAABB The AABB being tested
		void WalkOctreeForAABB(SceneQueryListener& listener, Octree& octant, const AABB& queryAABB);
	};

	class OctreeScene : public Scene
	{
		// Allow queries to access protected members
		friend class OctreeRaySceneQuery;
		friend class OctreeAABBSceneQuery;

	public:
		OctreeScene();

		OctreeScene(const AABB& box, size_t maxDepth);

		~OctreeScene() override;

	public:
		void Clear() override;

	public:
		void Init(const AABB& box, size_t maxDepth);

		void Resize(const AABB& box);

		void UpdateOctreeNode(OctreeNode& node);

		void RemoveOctreeNode(OctreeNode& node) const;

		void AddOctreeNode(OctreeNode& node, Octree& octant, size_t depth = 0);

		/// @brief Creates an optimized ray query that uses octree acceleration
		/// @param ray The ray for the query
		/// @return Unique pointer to the optimized ray query
		std::unique_ptr<OctreeRaySceneQuery> CreateOctreeRayQuery(const Ray& ray);

		/// @brief Creates an optimized AABB query that uses octree acceleration
		/// @param box The AABB for the query
		/// @return Unique pointer to the optimized AABB query
		std::unique_ptr<OctreeAABBSceneQuery> CreateOctreeAABBQuery(const AABB& box);

		/// @brief Overrides base Scene method to return octree-optimized ray query
		/// @param ray The ray for the query
		/// @return Unique pointer to the optimized ray query
		std::unique_ptr<RaySceneQuery> CreateRayQuery(const Ray& ray) override;

		/// @brief Overrides base Scene method to return octree-optimized AABB query
		/// @param box The AABB for the query
		/// @return Unique pointer to the optimized AABB query
		std::unique_ptr<AABBSceneQuery> CreateAABBQuery(const AABB& box) override;

	protected:
		void FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds, bool onlyShadowCasters) override;

		void WalkOctree(Camera& camera, RenderQueue& queue, Octree& octant, VisibleObjectsBoundsInfo& visibleBounds, 
			bool foundVisible, bool onlyShadowCasters, const CachedFrustumPlanes& cachedPlanes);

		std::unique_ptr<SceneNode> CreateSceneNodeImpl() override;

		std::unique_ptr<SceneNode> CreateSceneNodeImpl(const String& name) override;

	protected:
		/// The root octree
		std::unique_ptr<Octree> m_octree;

		/// Max depth for the tree
		size_t m_maxDepth;

		/// Size of the octree
		AABB m_bounds;
	};
}
