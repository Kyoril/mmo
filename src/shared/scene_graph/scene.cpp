// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#include "scene.h"
#include "camera.h"

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
	}

	Scene::Scene()
	{
		m_renderQueue = std::make_unique<RenderQueue>();
	}

	void Scene::Clear()
	{
		m_rootNode->RemoveAllChildren();
		m_cameras.clear();
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

	void Scene::Render(const Camera& camera)
	{
		auto& gx = GraphicsDevice::Get();
		
		// TODO: Render all objects
		UpdateSceneGraph();

		// Clear current render target
		gx.SetFillMode(FillMode::Solid);

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
		
	}

	void Scene::RenderQueueGroupObjects(RenderQueueGroup& group, QueuedRenderableCollection::OrganizationMode organizationMode)
	{
		//	for (const auto &groupIt : group)
		//	{
	    //		RenderPriorityGroup* pPriorityGrp = groupIt.getNext();
		// 
	    //		// Sort the queue first
	    //		pPriorityGrp->sort(mCameraInProgress);
		//		
	    //		// Do solids
	    //		RenderObjects(pPriorityGrp->getSolidsBasic(), om, true, true);
		// 
		//		// Do unsorted transparents
		//		RenderObjects(pPriorityGrp->getTransparentsUnsorted(), om, true, true);
		// 
	    //		// Do transparents (always descending)
	    //		RenderObjects(pPriorityGrp->getTransparents(), 
		//			QueuedRenderableCollection::OM_SORT_DESCENDING, true, true);
		//	}
	}

	void Scene::RenderObjects(const QueuedRenderableCollection& objects, QueuedRenderableCollection::OrganizationMode organizationMode)
	{

	}

	SceneNode& Scene::GetRootSceneNode() 
	{
		if (!m_rootNode)
		{
			m_rootNode = std::make_unique<SceneNode>("__root__");
		}

		return *m_rootNode;
	}
}
