// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <memory>
#include <vector>

#include "asset_preview_provider.h"
#include "base/non_copyable.h"

namespace mmo
{
	/// @brief Class for managing available asset preview providers.
	class PreviewProviderManager final : public NonCopyable
	{
	public:
		PreviewProviderManager() = default;
		~PreviewProviderManager() override = default;

	public:
		void AddPreviewProvider(std::unique_ptr<AssetPreviewProvider> provider);

		AssetPreviewProvider* GetPreviewProviderForExtension(const String& extension);

		void InvalidatePreview(const String& assetPath);

	private:
		std::vector<std::unique_ptr<AssetPreviewProvider>> m_previewProviders;
	};
}
