// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_preview_provider.h"

#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/material_manager.h"

namespace mmo
{
	MaterialPreviewProvider::MaterialPreviewProvider(EditorHost& host)
		: m_host(host)
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 35.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));
		m_cameraAnchor->Yaw(Degree(-45.0f), TransformSpace::World);

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_entity = m_scene.CreateEntity("MaterialPreviewProviderSphere", "Editor/Sphere.hmsh");
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius() * 2.0f);
		}

		if (m_viewportRT == nullptr)
		{
			m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("MaterialPreview_RenderTexture", 128, 128, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
		}

		m_renderConnection = m_host.beforeUiUpdate.connect(this, &MaterialPreviewProvider::RenderMaterialPreview);
	}

	void MaterialPreviewProvider::InvalidatePreview(const String& assetPath)
	{
		if (m_previewTextures.contains(assetPath))
		{
			m_previewTextures.erase(assetPath);
		}
	}

	ImTextureID MaterialPreviewProvider::GetAssetPreview(const String& assetPath)
	{
		const auto it = m_previewTextures.find(assetPath);
		if (it != m_previewTextures.end())
		{
			return it->second ? it->second->GetTextureObject() : nullptr;
		}

		m_previewRenderQueue.push(assetPath);

		return nullptr;
	}

	const std::set<String>& MaterialPreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions { ".hmat", ".hmi" };
		return extensions;
	}

	void MaterialPreviewProvider::RenderMaterialPreview()
	{
		if (!m_viewportRT) return;
		if (!m_entity) return;

		if (m_previewRenderQueue.empty()) return;

		// Grab next asset to render
		const std::string assetPath = m_previewRenderQueue.front();
		m_previewRenderQueue.pop();

		// Check if already rendered before?
		if(m_previewTextures.contains(assetPath))
		{
			return;
		}

		m_entity->SetMaterial(MaterialManager::Get().Load(assetPath));

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::White);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, 128, 128, 0.0f, 1.0f);
		m_camera->SetAspectRatio(1.0f);

		m_scene.Render(*m_camera, PixelShaderType::Forward);

		m_viewportRT->Update();

		TexturePtr tex = m_viewportRT->StoreToTexture();
		m_previewTextures[assetPath] = tex;
	}
}
