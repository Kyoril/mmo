// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
	/// @brief Implementation of the AssetPreviewProvider class which provides previews of textures.
	class MaterialPreviewProvider final : public AssetPreviewProvider
	{
	public:
		MaterialPreviewProvider(EditorHost& host);
		~MaterialPreviewProvider() override = default;

	public:
		virtual void InvalidatePreview(const String& assetPath) override;

		/// @copydoc AssetPreviewProvider::GetAssetPreview
		[[nodiscard]] ImTextureID GetAssetPreview(const String& assetPath) override;
		
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;

	private:
		void RenderMaterialPreview();

	private:
		EditorHost& m_host;
		scoped_connection m_renderConnection;
		std::map<String, TexturePtr> m_previewTextures;
		RenderTexturePtr m_viewportRT;
		std::queue<std::string> m_previewRenderQueue;

		Scene m_scene;
		SceneNode* m_cameraAnchor{ nullptr };
		SceneNode* m_cameraNode{ nullptr };
		Entity* m_entity{ nullptr };
		Camera* m_camera{ nullptr };
	};
}
