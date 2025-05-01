// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "static_texture_preview_provider.h"
#include "graphics/texture_mgr.h"

namespace mmo
{
	StaticTexturePreviewProvider::StaticTexturePreviewProvider(const String& texturePath)
	{
		// Load the specified texture to be used for all previews
		m_previewTexture = TextureManager::Get().CreateOrRetrieve(texturePath);
	}

	ImTextureID StaticTexturePreviewProvider::GetAssetPreview(const String& assetPath)
	{
		// Just return the pre-loaded texture for all assets
		return m_previewTexture ? m_previewTexture->GetTextureObject() : nullptr;
	}
}