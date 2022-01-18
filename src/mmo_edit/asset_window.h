// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>
#include <string>

#include "base/non_copyable.h"
#include "graphics/texture.h"

#include "editor_window_base.h"
#include "preview_provider_manager.h"

namespace mmo
{
	struct AssetEntry
	{
		std::string fullPath;
		std::map<std::string, AssetEntry> children;
	};
	
	/// Manages the available model files in the asset registry.
	class AssetWindow final
		: public EditorWindowBase
		, public NonCopyable
	{
	public:
		explicit AssetWindow(const String& name, PreviewProviderManager& previewProviderManager);
		~AssetWindow() override = default;

	public:
		/// Draws the viewport window.
		bool Draw() override;

	public:
		bool IsDockable() const noexcept override { return true; }

	private:
		
		void RenderAssetEntry(const std::string& name, const AssetEntry& entry, const std::string& path);
		
		void AddAssetToMap(AssetEntry& parent, const std::string& assetPath);

	private:
		PreviewProviderManager& m_previewProviderManager;
		bool m_visible { true };
		std::map<std::string, AssetEntry> m_assets;
		TexturePtr m_folderTexture;
		const AssetEntry* m_selectedEntry { nullptr };
		float m_columnWidth { 350.0f };

	};
}
