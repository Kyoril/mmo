// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>
#include <memory>

#include "queued_renderable_visitor.h"
#include "render_queue.h"
#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "camera.h"
#include "entity.h"
#include "manual_render_object.h"
#include "scene_node.h"


namespace mmo
{
	class Scene;

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

	/// This class contains all objects of a scene that can be rendered.
	class Scene final
		: public NonCopyable
	{
	public:
		typedef std::map<String, std::unique_ptr<Camera>> Cameras;
        typedef std::map<String, std::unique_ptr<SceneNode>> SceneNodes;

	public:
		Scene();

		/// Removes everything from the scene, completely wiping it.
		void Clear();

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

		/// Tries to find a camera by name.
		/// @param name Name of the searched camera.
		/// @returns Pointer to the camera or nullptr if the camera does not exist.
		Camera* GetCamera(const String& name);
		
		bool HasCamera(const String& name);

		/// Destroys all registered cameras.
		void DestroyAllCameras();

		SceneNode& GetRootSceneNode();

		SceneNode& CreateSceneNode();

		SceneNode& CreateSceneNode(const String& name);

		Entity* CreateEntity(const String& entityName, const String& meshName);
		
		RenderQueue& GetRenderQueue();

	public:
		/// Renders the current scene by using a specific camera as the origin.
		void Render(Camera& camera);

		void UpdateSceneGraph();
		
		void RenderSingleObject(Renderable& renderable);

		ManualRenderObject* CreateManualRenderObject(const String& name);

		[[nodiscard]] float GetShadowFarDistance() const noexcept { return m_defaultShadowFarDist; }

		[[nodiscard]] float GetShadowFarDistanceSquared() const noexcept { return m_defaultShadowFarDist * m_defaultShadowFarDist; }

		void SetShadowFarDistance(const float value) noexcept { m_defaultShadowFarDist = value; }

	private:
		void RenderVisibleObjects();
		
		void InitRenderQueue();

		void PrepareRenderQueue();

		void FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds);

		void RenderObjects(const QueuedRenderableCollection& objects);

		void RenderQueueGroupObjects(RenderQueueGroup& group);

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
		
		SceneQueuedRenderableVisitor m_renderableVisitor;

		float m_defaultShadowFarDist { 0.0f };
	};
}
