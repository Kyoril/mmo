// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>

#include "asset_preview_provider.h"
#include "graphics/texture.h"

namespace mmo
{
	/// @brief Implementation of the AssetPreviewProvider class which provides previews of textures.
	class TexturePreviewProvider final : public AssetPreviewProvider
	{
	public:
		TexturePreviewProvider() = default;
		~TexturePreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetAssetPreview
		[[nodiscard]] ImTextureID GetAssetPreview(const String& assetPath) override;
		
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;

	private:
		std::map<String, TexturePtr> m_previewTextures;
	};
}
