// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>

#include "asset_preview_provider.h"
#include "graphics/texture.h"

namespace mmo
{
	/// @brief Base class for preview providers that simply display a static texture for assets
	class StaticTexturePreviewProvider : public AssetPreviewProvider
	{
	public:
		StaticTexturePreviewProvider(const String& texturePath);
		~StaticTexturePreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetAssetPreview
		[[nodiscard]] ImTextureID GetAssetPreview(const String& assetPath) override;
		
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] virtual const std::set<String>& GetSupportedExtensions() const override = 0;

	protected:
		TexturePtr m_previewTexture;
	};
}