// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_preview_provider.h"

#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/material_manager.h"

namespace mmo
{
	MeshPreviewProvider::MeshPreviewProvider(EditorHost& host)
		: m_host(host)
	{
		// Set up the camera and scene
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 5.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-15.0f), Vector3::UnitX));
		m_cameraAnchor->Yaw(Degree(-45.0f), TransformSpace::World);

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		// Create a node for the mesh
		m_meshNode = &m_scene.CreateSceneNode("MeshNode");
		m_scene.GetRootSceneNode().AddChild(*m_meshNode);
		
		// Create the render texture for the preview
		if (m_viewportRT == nullptr)
		{
			m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("MeshPreview_RenderTexture", 
				128, 128, 
				RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
		}

		// Connect to the render event
		m_renderConnection = m_host.beforeUiUpdate.connect(this, &MeshPreviewProvider::RenderMeshPreview);
	}

	void MeshPreviewProvider::InvalidatePreview(const String& assetPath)
	{
		if (m_previewTextures.contains(assetPath))
		{
			m_previewTextures.erase(assetPath);
		}
	}

	ImTextureID MeshPreviewProvider::GetAssetPreview(const String& assetPath)
	{
		const auto it = m_previewTextures.find(assetPath);
		if (it != m_previewTextures.end())
		{
			return it->second ? it->second->GetTextureObject() : nullptr;
		}

		// Queue the mesh for rendering
		m_previewRenderQueue.push(assetPath);

		return nullptr;
	}

	const std::set<String>& MeshPreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions { ".hmsh" };
		return extensions;
	}

	void MeshPreviewProvider::RenderMeshPreview()
	{
		if (!m_viewportRT) return;
		if (m_previewRenderQueue.empty()) return;

		// Grab next asset to render
		const std::string assetPath = m_previewRenderQueue.front();
		m_previewRenderQueue.pop();

		// Check if already rendered before
		if(m_previewTextures.contains(assetPath))
		{
			return;
		}

		// Clean up previous entity if it exists
		if (m_currentEntity)
		{
			m_meshNode->DetachObject(*m_currentEntity);
			m_scene.DestroyEntity(*m_currentEntity);
			m_currentEntity = nullptr;
		}

		// Create a new entity for this mesh
		m_currentEntity = m_scene.CreateEntity("MeshPreviewEntity", assetPath);
		
		if (!m_currentEntity)
		{
			// If mesh couldn't be loaded, we'll still cache a null result
			m_previewTextures[assetPath] = nullptr;
			return;
		}
		
		// Attach to the scene
		m_meshNode->AttachObject(*m_currentEntity);
		
		// Position camera based on mesh bounds
		float radius = m_currentEntity->GetBoundingRadius();
		if (radius <= 0.0f) radius = 1.0f; // Fallback if radius is invalid
		
		// Position the camera at an appropriate distance
		m_cameraNode->SetPosition(Vector3::UnitZ * radius * 2.5f);
		
		// Reset the mesh node orientation and center the mesh
		m_meshNode->SetOrientation(Quaternion::Identity);
		m_meshNode->SetPosition(-m_currentEntity->GetBoundingBox().GetCenter());

		auto& gx = GraphicsDevice::Get();

		// Render the scene
		gx.Reset();
		gx.SetClearColor(Color(0.2f, 0.2f, 0.2f)); // Dark gray background for contrast
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, 128, 128, 0.0f, 1.0f);
		m_camera->SetAspectRatio(1.0f);

		// Render the scene with the mesh
		m_scene.Render(*m_camera, PixelShaderType::Forward);
		m_viewportRT->Update();

		// Store the render to a texture for caching
		TexturePtr tex = m_viewportRT->StoreToTexture();
		m_previewTextures[assetPath] = tex;
	}
}