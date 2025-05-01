// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "static_texture_preview_provider.h"

namespace mmo
{
	/// @brief Implementation of the AssetPreviewProvider class which provides previews of skeletal mesh files.
	class SkeletonPreviewProvider final : public StaticTexturePreviewProvider
	{
	public:
		SkeletonPreviewProvider();
		~SkeletonPreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;
	};
}