// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>
#include <string>
#include <vector>


#include "base/non_copyable.h"
#include "graphics/texture.h"

namespace mmo
{
	struct AssetEntry
	{
		std::string fullPath;
		std::map<std::string, AssetEntry> children;
	};
	
	/// Manages the available model files in the asset registry.
	class AssetWindow final
		: public NonCopyable
	{
	public:
		explicit AssetWindow();

	public:
		/// Draws the viewport window.
		bool Draw();
		/// Draws the view menu item for this window.
		bool DrawViewMenuItem();

	public:
		/// Makes the viewport window visible.
		void Show() noexcept { m_visible = true; }
		/// Determines whether the viewport window is currently visible.
		bool IsVisible() const noexcept { return m_visible; }

	private:
		
		void RenderAssetEntry(const std::string& name, const AssetEntry& entry, const std::string& path);
		
		void AddAssetToMap(AssetEntry& parent, const std::string& assetPath);

	private:
		bool m_visible;
		std::map<std::string, AssetEntry> m_assets;
		std::string m_selectedPath;
		TexturePtr m_folderTexture;
		const AssetEntry* m_selectedEntry { nullptr };
	};
}
