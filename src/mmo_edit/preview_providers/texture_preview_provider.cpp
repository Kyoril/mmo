// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "texture_preview_provider.h"

#include "graphics/texture_mgr.h"

namespace mmo
{
	ImTextureID TexturePreviewProvider::GetAssetPreview(const String& assetPath)
	{
		const auto it = m_previewTextures.find(assetPath);
		if (it != m_previewTextures.end())
		{
			return it->second ? it->second->GetTextureObject() : nullptr;
		}
		
		auto texture = TextureManager::Get().CreateOrRetrieve(assetPath);
		m_previewTextures.emplace(assetPath, texture);

		return texture ? texture->GetTextureObject() : nullptr;
	}

	const std::set<String>& TexturePreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions { ".htex" };
		return extensions;
	}
}
