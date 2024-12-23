// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <set>

#include "base/non_copyable.h"

#include "imgui.h"
#include "base/typedefs.h"

namespace mmo
{
	/// @brief Class for providing a preview image for assets in the asset browser.
	class AssetPreviewProvider : public NonCopyable
	{
	public:
		AssetPreviewProvider() = default;
		virtual ~AssetPreviewProvider() override = default;
		
	public:
		virtual void InvalidatePreview(const String& assetPath) {};

		/// @brief Provides a texture id for a given asset to use for previewing the given asset.
		[[nodiscard]] virtual ImTextureID GetAssetPreview(const String& assetPath) = 0;

		/// @brief Return true if the given asset extension is supported by this preview provider.
		[[nodiscard]] virtual const std::set<String>& GetSupportedExtensions() const = 0;
	};
}
