// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_preview_provider.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	ParticlePreviewProvider::ParticlePreviewProvider(EditorHost& host)
		: m_host(host)
	{
		// Set up the camera and scene
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 8.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-15.0f), Vector3::UnitX));
		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		// Node the preview particle system gets attached to
		m_systemNode = m_scene.GetRootSceneNode().CreateChildSceneNode("ParticlePreviewNode");

		// Create the render texture for the preview
		if (m_viewportRT == nullptr)
		{
			m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("ParticlePreview_RenderTexture",
				128, 128,
				RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
		}

		// Connect to the render event
		m_renderConnection = m_host.beforeUiUpdate.connect(this, &ParticlePreviewProvider::RenderParticlePreview);
	}

	void ParticlePreviewProvider::InvalidatePreview(const String& assetPath)
	{
		if (m_previewTextures.contains(assetPath))
		{
			m_previewTextures.erase(assetPath);
		}
	}

	ImTextureID ParticlePreviewProvider::GetAssetPreview(const String& assetPath)
	{
		const auto it = m_previewTextures.find(assetPath);
		if (it != m_previewTextures.end())
		{
			return it->second ? it->second->GetTextureObject() : nullptr;
		}

		// Queue the particle system for rendering
		m_previewRenderQueue.push(assetPath);

		return nullptr;
	}

	const std::set<String>& ParticlePreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions{ ".hpar" };
		return extensions;
	}

	void ParticlePreviewProvider::RenderParticlePreview()
	{
		if (!m_viewportRT) return;
		if (m_previewRenderQueue.empty()) return;

		// Grab next asset to render (one per frame to keep the editor responsive)
		const std::string assetPath = m_previewRenderQueue.front();
		m_previewRenderQueue.pop();

		// Check if already rendered before
		if (m_previewTextures.contains(assetPath))
		{
			return;
		}

		// Load the particle system parameters
		ParticleSystemParameters params;
		{
			const auto file = AssetRegistry::OpenFile(assetPath);
			if (!file)
			{
				m_previewTextures[assetPath] = nullptr;
				return;
			}

			io::StreamSource source(*file);
			io::Reader reader(source);

			ParticleSystemSerializer serializer;
			if (!serializer.Deserialize(params, reader) || params.emitters.empty())
			{
				WLOG("Failed to load particle system for preview: " << assetPath);
				m_previewTextures[assetPath] = nullptr;
				return;
			}
		}

		// Create a temporary system and warm it up with fixed steps so the
		// thumbnail shows the effect mid-flight instead of an empty frame.
		ParticleSystem* system = m_scene.CreateParticleEmitter("ParticlePreviewSystem");
		if (!system)
		{
			m_previewTextures[assetPath] = nullptr;
			return;
		}

		system->SetAutoUpdate(false);
		system->SetSystemParameters(params);
		m_systemNode->AttachObject(*system);
		system->Play();

		// Looping systems settle after a fixed warmup; one-shot systems (bursts)
		// look best around half of their longest emitter duration.
		bool allLoop = true;
		float maxDuration = 0.0f;
		for (const auto& emitter : params.emitters)
		{
			allLoop = allLoop && emitter.loop;
			maxDuration = std::max(maxDuration, emitter.duration + emitter.startDelay);
		}

		const float warmupTime = allLoop ? 1.5f : std::max(0.05f, maxDuration * 0.5f);
		const float step = 1.0f / 30.0f;
		float t = 0.0f;
		int safety = 300;
		while (t < warmupTime && safety-- > 0)
		{
			const float dt = std::min(step, warmupTime - t);
			system->Update(dt);
			t += dt;
		}

		auto& gx = GraphicsDevice::Get();

		// Render the scene
		gx.Reset();
		gx.SetClearColor(Color(0.2f, 0.2f, 0.2f)); // Mid gray keeps additive and alpha particles visible
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, 128, 128, 0.0f, 1.0f);
		m_camera->SetAspectRatio(1.0f);

		m_scene.Render(*m_camera, PixelShaderType::Forward);
		m_viewportRT->Update();

		// Store the render to a texture for caching
		TexturePtr tex = m_viewportRT->StoreToTexture();
		m_previewTextures[assetPath] = tex;

		// Destroy the temporary system again
		m_systemNode->DetachObject(*system);
		m_scene.DestroyParticleEmitter(*system);
	}
}
