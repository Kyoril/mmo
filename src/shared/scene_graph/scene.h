// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "queued_renderable_visitor.h"
#include "render_queue.h"
#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "camera.h"
#include "entity.h"
#include "light.h"
#include "manual_render_object.h"
#include "movable_object.h"
#include "scene_node.h"
#include "math/ray.h"

#include <map>
#include <memory>
#include <vector>


namespace mmo
{
	class Scene;
	class Material;

	class SceneQueuedRenderableVisitor final : public QueuedRenderableVisitor
	{
	public:
		void Visit(RenderablePass& rp) override;
		bool Visit(const Pass& p) override;
		void Visit(Renderable& r) override;

	public:
		/// Target SM to send renderables to
		Scene* targetScene { nullptr };
		/// Scissoring if requested?
		bool scissoring { false };
	};

	/// Base class of a scene query.
	class SceneQuery
	{
	protected:
		Scene& m_scene;
		uint32 m_queryMask{ 0xffffffff };
		uint32 m_queryTypeMask{ 0xffffffff };

	public:
		explicit SceneQuery(Scene& scene);
		virtual ~SceneQuery() = default;

		/// @brief Filters which movable objects will be returned by the query.
		///	A movable object will only be returned if a bitwise AND operation between
		///	this mask and the movable objects query mask returns a non-zero value.
		///	By default, this mask is set to 0xffffffff, which means that all movable
		///	objects will be returned.
		virtual void SetQueryMask(const uint32 mask) { m_queryMask = mask; }

		/// @brief Gets the query mask used by the query.
		virtual uint32 GetQueryMask() const { return m_queryMask; }

		/// @brief Filters which types of movable objects will be returned by the query.
		virtual void SetQueryTypeMask(const uint32 mask) { m_queryTypeMask = mask; }

		/// @brief Gets the query type mask.
		virtual uint32 GetQueryTypeMask() const { return m_queryTypeMask; }
	};

	typedef std::vector<MovableObject*> SceneQueryResult;

	/// Listener interface for scene queries.
	class SceneQueryListener
	{
	public:
		virtual ~SceneQueryListener() = default;

	public:
		/// @brief Called whenever a movable object has been found by a scene query.
		/// @param object Reference to the movable object which has been hit by a query.
		/// @returns false to stop the query, true to continue.
		virtual bool QueryResult(MovableObject& object) = 0;
	};

	/// Abstract base class for region based scene queries.
	class RegionSceneQuery : public SceneQuery, public SceneQueryListener
	{
	private:
		SceneQueryResult m_lastResult;

	public:
		explicit RegionSceneQuery(Scene& scene);
		virtual ~RegionSceneQuery() = default;

	public:
		/// @brief Executes the scene query with itself as a listener.
		virtual const SceneQueryResult& Execute();

		/// @brief Executes the scene query with a specific listener.
		/// @param listener The listener to call on hit results.
		virtual void Execute(SceneQueryListener& listener) = 0;

		/// Gets the last result of the query if it has been executed without an external listener.
		const SceneQueryResult& GetLastResult() const { return m_lastResult; }

		/// Clears the last query results.
		void ClearResult() { m_lastResult.clear(); }

	public:
		/// @copydoc SceneQueryListener::QueryResult
		bool QueryResult(MovableObject& first) override;
	};

	/// @brief Specialized scene query to perform axis aligned bounding box based queries.
	class AABBSceneQuery : public RegionSceneQuery
	{
	protected:
		AABB m_aabb;

	public:
		explicit AABBSceneQuery(Scene& scene);
		virtual ~AABBSceneQuery() = default;

	public:
		/// @brief Sets the bounding box to use when executing the scene query.
		void SetBox(const AABB& box) { m_aabb = box; }

		/// @brief Gets the axis aligned bounding box used by the query. 
		const AABB& GetBox() const { return m_aabb; }

		/// @copydoc RegionSceneQuery::Execute
		virtual void Execute(SceneQueryListener& listener) override;
	};

	/// @brief Specialized scene query to perform sphere based queries.
	class SphereSceneQuery : public RegionSceneQuery
	{
	protected:
		Sphere m_sphere;

	public:
		explicit SphereSceneQuery(Scene& scene);
		virtual ~SphereSceneQuery() = default;

	public:
		/// @brief Sets the sphere to be used by the query.
		void SetSphere(const Sphere& sphere) { m_sphere = sphere; }

		/// @brief Gets the sphere used by the query.
		const Sphere& GetSphere() const { return m_sphere; }

		/// @copydoc RegionSceneQuery::Execute
		virtual void Execute(SceneQueryListener& listener) override;
	};

	/// @brief Special listener for ray scene queries which not only provides the object but also the distance to the origin of the ray.
	class RaySceneQueryListener
	{
	public:
		virtual ~RaySceneQueryListener() = default;

	public:
		virtual void NotifyObjectChecked(MovableObject& obj) = 0;

		/// @brief Callback for when a movable object was hit by the raycast.
		/// @param obj The object that was hit.
		/// @param distance The distance to the origin of the ray.
		virtual bool QueryResult(MovableObject& obj, float distance) = 0;
	};

	/// @brief Result of a ray scene query.
	struct RaySceneQueryResultEntry
	{
		/// Distance along the ray
		float distance;

		/// The movable
		MovableObject* movable;

		/// Comparison operator for sorting
		bool operator < (const RaySceneQueryResultEntry& rhs) const
		{
			return this->distance < rhs.distance;
		}
	};

	typedef std::vector<RaySceneQueryResultEntry> RaySceneQueryResult;

	/// @brief Specialized scene query to perform ray-cast based queries.
	class RaySceneQuery : public SceneQuery, public RaySceneQueryListener
	{
	protected:
		Ray m_ray;

	private:
		bool m_sortByDistance = false;
		uint16_t m_maxResults = 0;
		RaySceneQueryResult m_result;
		bool m_debugHitTests = false;
		std::vector<MovableObject*> m_debugHitTestResults;

	public:
		explicit RaySceneQuery(Scene& scene);
		virtual ~RaySceneQuery() = default;

	public:
		void NotifyObjectChecked(MovableObject& obj) override;

		/// @brief Sets the ray to be used by the query.
		virtual void SetRay(const Ray& ray) { m_ray = ray; }

		/// @brief Gets the ray to be used by the query.
		virtual const Ray& GetRay() const { return m_ray; }

		/// @brief Sets whether to sort the results based on their distance.
		/// @param sort true to sort objects by their distance (closest ones come first).
		/// @param maxResults Maximum number of results to be returned. If set to 0, all results will be returned.
		virtual void SetSortByDistance(bool sort, uint16_t maxResults = 0) { m_sortByDistance = sort; m_maxResults = maxResults; }

		/// @breif Gets whether to sort results by their distance.
		virtual bool GetSortByDistance() const { return m_sortByDistance; }

		/// @brief Gets the maximum amount of results to be returned. 0 if all results should be returned.
		virtual uint16_t GetMaxResults() const { return m_maxResults; }

		/// @brief Executes the query with itself as hit result listener.
		virtual const RaySceneQueryResult& Execute();

		/// @brief Executes the query with a specific listener.
		/// @param listener The listener to use for reporting hit results.
		virtual void Execute(RaySceneQueryListener& listener);

		/// @brief Gets the result of the last query execution without an external listener.
		const RaySceneQueryResult& GetLastResult() const { return m_result; }

		/// @brief Clears the result from the last execution without an external listener.
		void ClearResult() { m_result.clear(); }

		bool IsDebuggingHitTestResults() const { return m_debugHitTests; }

		void SetDebugHitTestResults(bool debug) { m_debugHitTests = debug; }

		/// Gets the debug hit test results, which are objects that were tested for intersection with the ray cast.
		const std::vector<MovableObject*>& GetDebugHitTestResults() const { return m_debugHitTestResults; }

	public:
		/// @copydoc RaySceneQueryListener::QueryResult
		bool QueryResult(MovableObject& obj, float distance) override;
	};

	/// This class contains all objects of a scene that can be rendered.
	class Scene
		: public NonCopyable
	{
	public:
		typedef std::map<String, std::unique_ptr<Camera>> Cameras;
        typedef std::map<String, std::unique_ptr<SceneNode>> SceneNodes;

	public:
		Scene();

		virtual ~Scene() override = default;

		/// Removes everything from the scene, completely wiping it.
		virtual void Clear();

	public:
		// Camera management
		
		/// Creates a new camera using the specified name.
		/// @param name Name of the camera. Has to be unique to the scene.
		/// @returns nullptr in case of an error (like a camera with the given name already exists).
		Camera* CreateCamera(const String& name);

		/// Destroys a given camera.
		/// @param camera The camera to remove.
		void DestroyCamera(const Camera& camera);

		/// Destroys a camera using a specific name.
		/// @param name Name of the camera to remove.
		void DestroyCamera(const String& name);

		void DestroyEntity(const Entity& entity);

		void DestroySceneNode(const SceneNode& sceneNode);

		Light& CreateLight(const String& name, LightType type);

		/// Tries to find a camera by name.
		/// @param name Name of the searched camera.
		/// @returns Pointer to the camera or nullptr if the camera does not exist.
		[[nodiscard]] Camera* GetCamera(const String& name);

		[[nodiscard]] Camera* GetCamera(uint32 index) const;

		[[nodiscard]] uint32 GetCameraCount() const { return m_cameras.size(); }
		
		bool HasCamera(const String& name);

		/// Destroys all registered cameras.
		void DestroyAllCameras();

		SceneNode& GetRootSceneNode();

		SceneNode& CreateSceneNode();

		SceneNode& CreateSceneNode(const String& name);

		Entity* CreateEntity(const String& entityName, const String& meshName);

		Entity* CreateEntity(const String& entityName, const MeshPtr& mesh);
		
		RenderQueue& GetRenderQueue();

		std::vector<Entity*> GetAllEntities() const;

		std::unique_ptr<AABBSceneQuery> CreateAABBQuery(const AABB& box);

		std::unique_ptr<SphereSceneQuery> CreateSphereQuery(const Sphere& sphere);

		std::unique_ptr<RaySceneQuery> CreateRayQuery(const Ray& ray);

	protected:
		virtual std::unique_ptr<SceneNode> CreateSceneNodeImpl();

		virtual std::unique_ptr<SceneNode> CreateSceneNodeImpl(const String& name);

	public:
		/// Renders the current scene by using a specific camera as the origin.
		void Render(Camera& camera);

		void UpdateSceneGraph();
		
		void RenderSingleObject(Renderable& renderable);

		ManualRenderObject* CreateManualRenderObject(const String& name);

		void DestroyManualRenderObject(const ManualRenderObject& object);

		[[nodiscard]] float GetShadowFarDistance() const noexcept { return m_defaultShadowFarDist; }

		[[nodiscard]] float GetShadowFarDistanceSquared() const noexcept { return m_defaultShadowFarDist * m_defaultShadowFarDist; }

		void SetShadowFarDistance(const float value) noexcept { m_defaultShadowFarDist = value; }

		/// @brief Freezes or unfreezes the rendering to debug culling. If frozen, the render queue will not be updated any
		///        more, which will allow to view the scene of the frozen camera perspective with a new camera transformation
		///        to debug which objects were rendered during the frozen frame.
		///        If rendering was already frozen when trying to refreeze, nothing will happen (as the previous frozen frame will
		///		   still be used for rendering).
		/// @param freeze true to freeze rendering, false to unfreeze.
		void FreezeRendering(const bool freeze) { m_frozen = freeze; }

		/// @brief Determines whether rendering is currently frozen.
		bool IsRenderingFrozen() const { return m_frozen; }

	protected:
		void RenderVisibleObjects();
		
		void InitRenderQueue();

		void PrepareRenderQueue();

		virtual void FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds);

		void RenderObjects(const QueuedRenderableCollection& objects);

		void RenderQueueGroupObjects(RenderQueueGroup& group);

		void NotifyLightsDirty();
		void FindLightsAffectingCamera(const Camera& camera);

		struct LightInfo
		{
			Light* light;
			LightType type;
			float range;
			Vector3 position;
			uint32 lightMask;
			bool castsShadow;

			bool operator==(const LightInfo& rhs) const
			{
				return light == rhs.light && type == rhs.type &&
					range == rhs.range && position == rhs.position && lightMask == rhs.lightMask && castsShadow == rhs.castsShadow;
			}

			bool operator!=(const LightInfo& rhs) const
			{
				return !(*this == rhs);
			}
		};

		typedef std::set<Light*> LightSet;
		typedef std::vector<LightInfo> LightInfoList;

		LightSet m_lightsAffectingCamera;
		LightInfoList m_cachedLightInfos;
		LightInfoList m_testLightInfos;
		uint32 m_lightsDirtyCounter { 0 };

	private:
		Cameras m_cameras;
        SceneNodes m_sceneNodes;
		SceneNode* m_rootNode { nullptr };
		std::unique_ptr<RenderQueue> m_renderQueue;

		typedef std::map<const Camera*, VisibleObjectsBoundsInfo> CamVisibleObjectsMap;
		CamVisibleObjectsMap m_camVisibleObjectsMap;

		typedef std::map<String, std::unique_ptr<Entity>> EntityMap;
		EntityMap m_entities;
		
		typedef std::map<String, std::unique_ptr<ManualRenderObject>> ManualRenderObjectMap;
		ManualRenderObjectMap m_manualRenderObjects;

		typedef std::map<String, std::unique_ptr<Light>> LightObjectMap;
		LightObjectMap m_lights;
		
		SceneQueuedRenderableVisitor m_renderableVisitor;

		float m_defaultShadowFarDist { 0.0f };

		MaterialPtr m_defaultMaterial;

		bool m_frozen{ false };
	};
}
