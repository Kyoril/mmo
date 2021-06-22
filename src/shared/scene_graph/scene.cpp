// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#include "scene.h"
#include "camera.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"


namespace mmo
{
	void Scene::Clear()
	{
		m_cameras.clear();
	}

	Camera* Scene::CreateCamera(const String& name)
	{
		ASSERT(!name.empty());
		
		if (m_cameras.find(name) != m_cameras.end())
		{
			return nullptr;
		}

		auto camera = std::make_unique<Camera>(*this, name);
		const auto insertedCamIt = 
			m_cameras.emplace(Cameras::value_type(name, std::move(camera)));

		return insertedCamIt.first->second.get();
	}

	void Scene::DestroyCamera(Camera& camera)
	{
		DestroyCamera(camera.GetName());
	}

	void Scene::DestroyCamera(const String& name)
	{
		const auto cameraIt = m_cameras.find(name);
		if (cameraIt != m_cameras.end())
		{
			m_cameras.erase(cameraIt);
		}
	}

	Camera* Scene::GetCamera(const String& name)
	{
		const auto cameraIt = m_cameras.find(name);
		if (cameraIt != m_cameras.end())
		{
			return cameraIt->second.get();
		}

		return nullptr;
	}

	void Scene::DestroyAllCameras()
	{
		m_cameras.clear();
	}

	void Scene::Render(Camera& camera)
	{
		auto& gx = GraphicsDevice::Get();

		gx.CaptureState();

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, camera.GetProjectionMatrix());
		gx.SetTransformMatrix(View, camera.GetViewMatrix());

		// TODO: Render all objects
		
		gx.RestoreState();
	}
}
