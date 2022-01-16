// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "scene.h"
#include "camera.h"
#include "mesh_manager.h"
#include "render_operation.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"


namespace mmo
{
	void SceneQueuedRenderableVisitor::Visit(RenderablePass& rp)
	{
	}

	bool SceneQueuedRenderableVisitor::Visit(const Pass& p)
	{
		return true;
	}

	void SceneQueuedRenderableVisitor::Visit(Renderable& r)
	{
		targetScene->RenderSingleObject(r);
	}

	Scene::Scene()
	{
		m_renderQueue = std::make_unique<RenderQueue>();
	}

	void Scene::Clear()
	{
		m_rootNode->RemoveAllChildren();
		m_cameras.clear();
		m_camVisibleObjectsMap.clear();
		m_entities.clear();
		m_manualRenderObjects.clear();
	}

	Camera* Scene::CreateCamera(const String& name)
	{
		ASSERT(!name.empty());
		ASSERT(m_cameras.find(name) == m_cameras.end());

		auto camera = std::make_unique<Camera>(name);
		camera->SetScene(this);

		const auto insertedCamIt = 
			m_cameras.emplace(Cameras::value_type(name, std::move(camera)));

		auto* cam = insertedCamIt.first->second.get();
		m_camVisibleObjectsMap[cam] = VisibleObjectsBoundsInfo();

		return cam;
	}

	void Scene::DestroyCamera(const Camera& camera)
	{
		DestroyCamera(camera.GetName());
	}

	void Scene::DestroyCamera(const String& name)
	{
		if (const auto cameraIt = m_cameras.find(name); cameraIt != m_cameras.end())
		{
			m_cameras.erase(cameraIt);
		}
	}

	void Scene::DestroyEntity(const Entity& entity)
	{
		const auto entityIt = m_entities.find(entity.GetName());
		if (entityIt != m_entities.end())
		{
			m_entities.erase(entityIt);
		}
	}

	void Scene::DestroySceneNode(const SceneNode& sceneNode)
	{
		ASSERT(&sceneNode != m_rootNode);

		const auto nodeIt = m_sceneNodes.find(sceneNode.GetName());
		if (nodeIt != m_sceneNodes.end())
		{
			m_sceneNodes.erase(nodeIt);
		}
	}

	Camera* Scene::GetCamera(const String& name)
	{
		const auto cameraIt = m_cameras.find(name);
		ASSERT(cameraIt != m_cameras.end());
		
		return cameraIt->second.get();
	}

	bool Scene::HasCamera(const String& name)
	{
		return m_cameras.find(name) != m_cameras.end();
	}

	void Scene::DestroyAllCameras()
	{
		m_cameras.clear();
	}

	void Scene::Render(Camera& camera)
	{
		auto& gx = GraphicsDevice::Get();

		m_renderableVisitor.targetScene = this;
		m_renderableVisitor.scissoring = false;

		UpdateSceneGraph();
		PrepareRenderQueue();

		const auto visibleObjectsIt = m_camVisibleObjectsMap.find(&camera);
		ASSERT(visibleObjectsIt != m_camVisibleObjectsMap.end());
		visibleObjectsIt->second.Reset();
		FindVisibleObjects(camera, visibleObjectsIt->second);

		// Clear current render target
		gx.SetFillMode(camera.GetFillMode());

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, camera.GetProjectionMatrix());
		gx.SetTransformMatrix(View, camera.GetViewMatrix());

		RenderVisibleObjects();
	}

	void Scene::UpdateSceneGraph()
	{
		GetRootSceneNode().Update(true, false);
	}

	void Scene::RenderVisibleObjects()
	{
		for (auto& queue = GetRenderQueue(); auto& [groupId, group] : queue)
		{
			RenderQueueGroupObjects(*group);
		}
	}

	void Scene::InitRenderQueue()
	{
		m_renderQueue = std::make_unique<RenderQueue>();

		// TODO: Maybe initialize some properties for special render queues
	}

	void Scene::PrepareRenderQueue()
	{
		auto& renderQueue = GetRenderQueue();
		renderQueue.Clear();
	}

	void Scene::FindVisibleObjects(Camera& camera, VisibleObjectsBoundsInfo& visibleObjectBounds)
	{
		GetRootSceneNode().FindVisibleObjects(camera, GetRenderQueue(), visibleObjectBounds, true);
	}

	void Scene::RenderObjects(const QueuedRenderableCollection& objects)
	{
		objects.AcceptVisitor(m_renderableVisitor);
	}
	
	void Scene::RenderQueueGroupObjects(RenderQueueGroup& group)
	{
		for(const auto& [priority, priorityGroup] : group)
		{
			RenderObjects(priorityGroup->GetSolids());
		}
	}

	void Scene::RenderSingleObject(Renderable& renderable)
	{
		RenderOperation op { };
		renderable.PrepareRenderOperation(op);

		op.vertexBuffer->Set();
		if (op.useIndexes)
		{
			ASSERT(op.indexBuffer);
			op.indexBuffer->Set();
		}

		GraphicsDevice::Get().SetTopologyType(op.topology);
		GraphicsDevice::Get().SetVertexFormat(op.vertexFormat);
		GraphicsDevice::Get().SetTransformMatrix(World, renderable.GetWorldTransform());

		if (op.useIndexes)
		{
			GraphicsDevice::Get().DrawIndexed();
		}
		else
		{
			GraphicsDevice::Get().Draw(op.vertexBuffer->GetVertexCount());
		}
	}

	ManualRenderObject* Scene::CreateManualRenderObject(const String& name)
	{
		ASSERT(m_manualRenderObjects.find(name) == m_manualRenderObjects.end());

		// TODO: No longer use singleton graphics device
		auto [iterator, created] = 
			m_manualRenderObjects.emplace(
				name, 
				std::make_unique<ManualRenderObject>(GraphicsDevice::Get()));
		
		return iterator->second.get();
	}

	SceneNode& Scene::GetRootSceneNode() 
	{
		if (!m_rootNode)
		{
			m_rootNode = &CreateSceneNode("__root__");
			m_rootNode->NotifyRootNode();
		}

		return *m_rootNode;
	}

	SceneNode& Scene::CreateSceneNode()
	{
		auto sceneNode = std::make_unique<SceneNode>(*this);
		SceneNode* rawNode = sceneNode.get();

		ASSERT(m_sceneNodes.find(rawNode->GetName()) == m_sceneNodes.end());
		m_sceneNodes[sceneNode->GetName()] = std::move(sceneNode);

		return *rawNode;
	}

	SceneNode& Scene::CreateSceneNode(const String& name)
	{
		ASSERT(m_sceneNodes.find(name) == m_sceneNodes.end());
		
		auto sceneNode = std::make_unique<SceneNode>(*this, name);
		m_sceneNodes[name] = std::move(sceneNode);
		return *m_sceneNodes[name];
	}

	Entity* Scene::CreateEntity(const String& entityName, const String& meshName)
	{
		ASSERT(m_entities.find(entityName) == m_entities.end());

		auto mesh = MeshManager::Get().Load(meshName);
		ASSERT(mesh);
		
		auto [entityIt, created] = m_entities.emplace(entityName, std::make_unique<Entity>(entityName, mesh));
		
		return entityIt->second.get();
	}

	RenderQueue& Scene::GetRenderQueue()
	{
		if (!m_renderQueue)
		{
			InitRenderQueue();
		}

		ASSERT(m_renderQueue);
		return *m_renderQueue;
	}
}
