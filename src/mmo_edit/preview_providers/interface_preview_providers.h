// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "static_texture_preview_provider.h"

namespace mmo
{
	/// @brief Implementation of the AssetPreviewProvider class which provides previews of lua files.
	class LuaPreviewProvider final : public StaticTexturePreviewProvider
	{
	public:
		LuaPreviewProvider();
		~LuaPreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;
	};

	/// @brief Implementation of the AssetPreviewProvider class which provides previews of xml files.
	class XmlPreviewProvider final : public StaticTexturePreviewProvider
	{
	public:
		XmlPreviewProvider();
		~XmlPreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;
	};

	/// @brief Implementation of the AssetPreviewProvider class which provides previews of xml files.
	class TocPreviewProvider final : public StaticTexturePreviewProvider
	{
	public:
		TocPreviewProvider();
		~TocPreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;
	};

	/// @brief Implementation of the AssetPreviewProvider class which provides previews of xml files.
	class AudioPreviewProvider final : public StaticTexturePreviewProvider
	{
	public:
		AudioPreviewProvider();
		~AudioPreviewProvider() override = default;

	public:
		/// @copydoc AssetPreviewProvider::GetSupportedExtensions
		[[nodiscard]] const std::set<String>& GetSupportedExtensions() const override;
	};
}