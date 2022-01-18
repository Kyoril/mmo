// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "preview_provider_manager.h"

#include "base/macros.h"

namespace mmo
{
	void PreviewProviderManager::AddPreviewProvider(std::unique_ptr<AssetPreviewProvider> provider)
	{
		ASSERT(provider);
		m_previewProviders.emplace_back(std::move(provider));
	}

	AssetPreviewProvider* PreviewProviderManager::GetPreviewProviderForExtension(const String& extension)
	{
		const auto it = std::find_if(m_previewProviders.begin(), m_previewProviders.end(), [&extension](const std::unique_ptr<AssetPreviewProvider>& previewProvider)
		{
			return previewProvider->GetSupportedExtensions().contains(extension);
		});

		if (it == m_previewProviders.end())
		{
			return nullptr;
		}

		return it->get();
	}
}
