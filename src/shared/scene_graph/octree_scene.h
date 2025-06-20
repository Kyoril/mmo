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

	class OctreeScene : public Scene
	{
		// Allow OctreeRaySceneQuery to access protected members
		friend class OctreeRaySceneQuery;

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

		/// @brief Overrides base Scene method to return octree-optimized ray query
		/// @param ray The ray for the query
		/// @return Unique pointer to the optimized ray query
		std::unique_ptr<RaySceneQuery> CreateRayQuery(const Ray& ray) override;

		/// @brief Check if this scene supports optimized spatial queries
		/// @return Always returns true for OctreeScene
		bool SupportsOptimizedQueries() const { return true; }

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
