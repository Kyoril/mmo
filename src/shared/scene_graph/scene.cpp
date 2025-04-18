// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scene.h"

#include <ranges>

#include "camera.h"
#include "material_manager.h"
#include "mesh_manager.h"
#include "render_operation.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"


namespace mmo
{
	void SceneQueuedRenderableVisitor::Visit(RenderablePass& rp)
	{
	}

	bool SceneQueuedRenderableVisitor::Visit(const Pass& p)
	{
		return true;
	}

	void SceneQueuedRenderableVisitor::Visit(Renderable& r, const uint32 groupId)
	{
		targetScene->RenderSingleObject(r, groupId);
	}

	struct alignas(16) PsCameraConstantBuffer
	{
		Vector3 cameraPosition;
		float fogStart;
		float fogEnd;
		Vector3 fogColor;
		Matrix4 inverseViewMatrix;
	};

	Scene::Scene()
	{
		m_renderQueue = std::make_unique<RenderQueue>();

		m_psCameraBuffer = GraphicsDevice::Get().CreateConstantBuffer(sizeof(PsCameraConstantBuffer), nullptr);
	}

	void Scene::Clear()
	{
		m_cameras.clear();
		m_camVisibleObjectsMap.clear();
		m_entities.clear();
		m_manualRenderObjects.clear();
		m_lights.clear();

		m_sceneNodes.clear();
		m_rootNode = nullptr;
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

	Light& Scene::CreateLight(const String& name, LightType type)
	{
		auto light = std::make_unique<Light>(name, type);
		light->SetScene(this);

		auto [iterator, inserted] = m_lights.emplace(name, std::move(light));
		return *iterator->second.get();
	}

	void Scene::DestroyLight(const Light& light)
	{
		const auto lightIt = m_lights.find(light.GetName());
		if (lightIt != m_lights.end())
		{
			m_lights.erase(lightIt);
		}
	}

	Camera* Scene::GetCamera(const String& name)
	{
		const auto cameraIt = m_cameras.find(name);
		ASSERT(cameraIt != m_cameras.end());
		
		return cameraIt->second.get();
	}

	Camera* Scene::GetCamera(uint32 index) const
	{
		if (index > m_cameras.size())
		{
			return nullptr;
		}

		auto it = m_cameras.begin();
		std::advance(it, index);

		ASSERT(it != m_cameras.end());
		return it->second.get();
	}

	bool Scene::HasCamera(const String& name)
	{
		return m_cameras.find(name) != m_cameras.end();
	}

	void Scene::DestroyAllCameras()
	{
		m_cameras.clear();
	}

	void Scene::Render(Camera& camera, PixelShaderType shaderType)
	{
		m_pixelShaderType = shaderType;

		auto& gx = GraphicsDevice::Get();

		m_activeCamera = &camera;

		ASSERT(m_psCameraBuffer);

		// Update ps constant buffer
		RefreshCameraBuffer(camera);

		m_renderableVisitor.targetScene = this;
		m_renderableVisitor.scissoring = false;

		UpdateSceneGraph();

		if (!m_frozen)
		{
			PrepareRenderQueue();

			const auto visibleObjectsIt = m_camVisibleObjectsMap.find(&camera);
			ASSERT(visibleObjectsIt != m_camVisibleObjectsMap.end());
			visibleObjectsIt->second.Reset();
			FindVisibleObjects(camera, visibleObjectsIt->second);
		}
		
		// Clear current render target
		gx.SetFillMode(camera.GetFillMode());

		// Enable depth test & write & set comparison method to less
		gx.SetDepthEnabled(true);
		gx.SetDepthWriteEnabled(true);
		gx.SetDepthTestComparison(DepthTestMethod::Less);

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, camera.GetProjectionMatrix());
		gx.SetTransformMatrix(View, camera.GetViewMatrix());

		RenderVisibleObjects();

		m_activeCamera = nullptr;
	}

	void Scene::UpdateSceneGraph()
	{
		GetRootSceneNode().Update(true, false);
	}

	MaterialPtr Scene::GetDefaultMaterial()
	{
		// Create default material
		if (!m_defaultMaterial)
		{
			m_defaultMaterial = MaterialManager::Get().Load("Models/Default.hmat");
			ASSERT(m_defaultMaterial);
		}

		return m_defaultMaterial;
	}

	std::vector<Light*> Scene::GetAllLights() const
	{
		std::vector<Light*> lights;
		lights.reserve(m_lights.size());

		for (const auto& [name, light] : m_lights)
		{
			lights.push_back(light.get());
		}

		return lights;
	}

	void Scene::RenderVisibleObjects()
	{
		for (auto& queue = GetRenderQueue(); auto& [groupId, group] : queue)
		{
			if (m_pixelShaderType == PixelShaderType::ShadowMap && groupId < RenderQueueGroupId::WorldGeometry1)
			{
				continue;
			}

			RenderQueueGroupObjects(*group);
		}
	}

	void Scene::InitRenderQueue()
	{
		m_renderQueue = std::make_unique<RenderQueue>();

		// Create default material
		(void)GetDefaultMaterial();

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

	void Scene::NotifyLightsDirty()
	{
		++m_lightsDirtyCounter;
	}

	void Scene::FindLightsAffectingCamera(const Camera& camera)
	{
		m_testLightInfos.clear();
		m_testLightInfos.reserve(m_lights.size());

		for (const auto& [name, light] : m_lights)
		{
			if (!light->IsVisible())
			{
				continue;
			}

			LightInfo lightInfo;
			lightInfo.light = light.get();
			lightInfo.type = light->GetType();
			lightInfo.lightMask = 0;	// TODO
			lightInfo.castsShadow = false;	// TODO

			// Directional lights don't have a position and thus are always visible
			if (lightInfo.type == LightType::Directional)
			{
				lightInfo.position = Vector3::Zero;
				lightInfo.range = 0.0f;
			}
			else
			{
				// Do a visibility check (culling) for each non directional light
				lightInfo.range = light->GetRange();
				lightInfo.position = light->GetDerivedPosition();

				const Sphere sphere{lightInfo.position, lightInfo.range};
				if (!camera.IsVisible(sphere))
				{
					continue;
				}
			}
			
			m_testLightInfos.emplace_back(std::move(lightInfo));
		}

		if (m_cachedLightInfos != m_testLightInfos)
		{
			m_cachedLightInfos = m_testLightInfos;
			NotifyLightsDirty();
		}
	}

	void Scene::RenderSingleObject(Renderable& renderable, uint32 groupId)
	{
		RenderOperation op { groupId };
		renderable.PrepareRenderOperation(op);
		op.pixelShaderType = m_pixelShaderType;

		if (op.vertexData == nullptr || op.vertexData->vertexCount == 0)
		{
			return;
		}

		auto& gx = GraphicsDevice::Get();

		// Grab material with fallback to default material of the scene
		if (!op.material)
		{
			auto material = renderable.GetMaterial();
			if (!material)
			{
				material = m_defaultMaterial;
			}

			op.material = material;
		}

		gx.SetTransformMatrix(World, renderable.GetWorldTransform());

		ASSERT(m_psCameraBuffer);
		op.pixelConstantBuffers.push_back(m_psCameraBuffer.get());

		// Bind vertex layout
		renderable.PreRender(*this, gx);
		gx.Render(op);
		renderable.PostRender(*this, gx);
	}

	ManualRenderObject* Scene::CreateManualRenderObject(const String& name)
	{
		ASSERT(m_manualRenderObjects.find(name) == m_manualRenderObjects.end());

		// TODO: No longer use singleton graphics device
		auto [iterator, created] = 
			m_manualRenderObjects.emplace(
				name, 
				std::make_unique<ManualRenderObject>(GraphicsDevice::Get(), name));
		
		return iterator->second.get();
	}

	void Scene::DestroyManualRenderObject(const ManualRenderObject& object)
	{
		auto it = m_manualRenderObjects.find(object.GetName());
		if (it != m_manualRenderObjects.end())
		{
			m_manualRenderObjects.erase(it);
		}
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
		auto sceneNode = CreateSceneNodeImpl();
		SceneNode* rawNode = sceneNode.get();

		ASSERT(m_sceneNodes.find(rawNode->GetName()) == m_sceneNodes.end());
		m_sceneNodes[sceneNode->GetName()] = std::move(sceneNode);

		return *rawNode;
	}

	SceneNode& Scene::CreateSceneNode(const String& name)
	{
		ASSERT(m_sceneNodes.find(name) == m_sceneNodes.end());
		
		auto sceneNode = CreateSceneNodeImpl(name);
		m_sceneNodes[name] = std::move(sceneNode);
		return *m_sceneNodes[name];
	}

	Entity* Scene::CreateEntity(const String& entityName, const String& meshName)
	{
		const auto mesh = MeshManager::Get().Load(meshName);
        if (!mesh)
        {
            ELOG("Failed to load mesh " << meshName);
        }
        
		return CreateEntity(entityName, mesh);
	}

	Entity* Scene::CreateEntity(const String& entityName, const MeshPtr& mesh)
	{
		ASSERT(m_entities.find(entityName) == m_entities.end());
    
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

	std::vector<Entity*> Scene::GetAllEntities() const
	{
		// TODO: this is bad performance-wise but should serve for now
		std::vector<Entity*> result;
		result.reserve(m_entities.size());

		for (const auto& pair : m_entities)
		{
			result.push_back(pair.second.get());
		}

		return result;
	}

	std::unique_ptr<AABBSceneQuery> Scene::CreateAABBQuery(const AABB& box)
	{
		auto query = std::make_unique<AABBSceneQuery>(*this);
		query->SetBox(box);

		return std::move(query);
	}

	std::unique_ptr<SphereSceneQuery> Scene::CreateSphereQuery(const Sphere& sphere)
	{
		auto query = std::make_unique<SphereSceneQuery>(*this);
		query->SetSphere(sphere);

		return std::move(query);
	}

	std::unique_ptr<RaySceneQuery> Scene::CreateRayQuery(const Ray& ray)
	{
		auto query = std::make_unique<RaySceneQuery>(*this);
		query->SetRay(ray);

		return std::move(query);
	}

	void Scene::SetFogRange(float start, float end)
	{
		ASSERT(end >= start);

		m_fogStart = start;
		m_fogEnd = end;
	}

	void Scene::SetFogColor(const Vector3& color)
	{
		m_fogColor = color;
	}

	void Scene::RefreshCameraBuffer(const Camera& camera)
	{
		PsCameraConstantBuffer buffer;
		buffer.cameraPosition = camera.GetDerivedPosition();
		buffer.fogStart = m_fogStart;
		buffer.fogEnd = m_fogEnd;
		buffer.fogColor = m_fogColor;
		buffer.inverseViewMatrix = camera.GetViewMatrix().Inverse();
		m_psCameraBuffer->Update(&buffer);
	}

	std::unique_ptr<SceneNode> Scene::CreateSceneNodeImpl()
	{
		return std::make_unique<SceneNode>(*this);
	}

	std::unique_ptr<SceneNode> Scene::CreateSceneNodeImpl(const String& name)
	{
		return std::make_unique<SceneNode>(*this, name);
	}

	RaySceneQuery::RaySceneQuery(Scene& scene)
		: SceneQuery(scene)
	{
	}

	void RaySceneQuery::NotifyObjectChecked(MovableObject& obj)
	{
		if (!m_debugHitTests)
		{
			return;
		}

		m_debugHitTestResults.push_back(&obj);
	}

	const RaySceneQueryResult& RaySceneQuery::Execute()
	{
		if (m_debugHitTests)
		{
			m_debugHitTestResults.clear();
		}
		
		Execute(*this);

		return m_result;
	}

	void RaySceneQuery::Execute(RaySceneQueryListener& listener)
	{
		// TODO: Instead of iterating over ALL objects in the scene, be smarter (for example octree)

		for (const auto& entity : m_scene.GetAllEntities())
		{
			// We check all
			if (m_debugHitTests)
			{
				listener.NotifyObjectChecked(*entity);
			}

			// Filtered due to type flags
			if ((entity->GetTypeFlags() & m_queryTypeMask) == 0)
			{
				continue;
			}

			// Filtered due to query flags
			if ((entity->GetQueryFlags() & m_queryMask) == 0)
			{
				continue;
			}

			const auto hitResult = m_ray.IntersectsAABB(entity->GetWorldBoundingBox(true));
			if (!hitResult.first)
			{
				continue;
			}

			if (!listener.QueryResult(*entity, hitResult.second))
			{
				return;
			}
		}
	}

	bool RaySceneQuery::QueryResult(MovableObject& obj, float distance)
	{
		RaySceneQueryResultEntry result;
		result.movable = &obj;
		result.distance = distance;
		
		const bool response = m_maxResults == 0 || m_result.size() + 1 < m_maxResults;

		if (m_sortByDistance)
		{
			auto it = m_result.begin();

			while (it != m_result.end())
			{
				if (it->distance > distance)
				{
					m_result.insert(it, std::move(result));
					return response;
				}

				it++;
			}
		}

		m_result.emplace_back(std::move(result));
		return response;
	}

	AABBSceneQuery::AABBSceneQuery(Scene& scene)
		: RegionSceneQuery(scene)
	{
	}

	void AABBSceneQuery::Execute(SceneQueryListener& listener)
	{
		// TODO
	}

	RegionSceneQuery::RegionSceneQuery(Scene& scene)
		: SceneQuery(scene)
	{
	}

	const SceneQueryResult& RegionSceneQuery::Execute()
	{
		Execute(*this);

		return m_lastResult;
	}

	bool RegionSceneQuery::QueryResult(MovableObject& first)
	{
		m_lastResult.push_back(&first);
		return true;
	}

	SceneQuery::SceneQuery(Scene& scene)
		: m_scene(scene)
	{
	}

	SphereSceneQuery::SphereSceneQuery(Scene& scene)
		: RegionSceneQuery(scene)
	{
	}

	void SphereSceneQuery::Execute(SceneQueryListener& listener)
	{
		// TODO
	}
}
