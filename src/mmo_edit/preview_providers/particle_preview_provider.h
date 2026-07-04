// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <queue>

#include "asset_preview_provider.h"
#include "editor_host.h"
#include "graphics/render_texture.h"
#include "graphics/texture.h"
#include "scene_graph/scene.h"

namespace mmo
{
	/// @brief Implementation of the AssetPreviewProvider class which provides previews of particle system assets (.hpar).
	///
	/// For each queued asset the particle system is loaded, warmed up with fixed simulation
	/// steps and rendered once to a small render texture which is then cached.
	class ParticlePreviewProvider final : public AssetPreviewProvider
	{
	public:
		explicit ParticlePreviewProvider(EditorHost& host);
		~ParticlePreviewProvider() override = default;

	public:
		void InvalidatePreview(const String& assetPath) override;

		/// @copydoc AssetPreviewProvider::GetAssetPreview
		[[nodiscard]] ImTextureID GetAssetPreview(const String& assetPath) override;

		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;

	private:
		void RenderParticlePreview();

	private:
		EditorHost& m_host;
		scoped_connection m_renderConnection;
		std::map<String, TexturePtr> m_previewTextures;
		RenderTexturePtr m_viewportRT;
		std::queue<std::string> m_previewRenderQueue;

		Scene m_scene;
		SceneNode* m_cameraAnchor{ nullptr };
		SceneNode* m_cameraNode{ nullptr };
		Camera* m_camera{ nullptr };
		SceneNode* m_systemNode{ nullptr };
	};
}
