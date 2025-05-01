// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <string>

#include "base/non_copyable.h"
#include "graphics/texture.h"

#include "editor_window_base.h"
#include "preview_providers/preview_provider_manager.h"

#include "editor_host.h"

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
		void RebuildAssetList();
		explicit AssetWindow(const String& name, PreviewProviderManager& previewProviderManager, EditorHost& host);
		~AssetWindow() override = default;

	public:
		/// Draws the viewport window.
		bool Draw() override;

	public:
		bool IsDockable() const noexcept override { return true; }

	private:
		void RenderAssetEntry(const std::string& name, const AssetEntry& entry, const std::string& path);
		
		void AddAssetToMap(AssetEntry& parent, const std::string& assetPath);
		
		bool SearchEntryByPath(const AssetEntry& entry, const std::string& path)
		{
			// Check if this is the entry we're looking for
			if (entry.fullPath == path)
			{
				m_selectedEntry = &entry;
				m_host.SetCurrentPath(entry.fullPath);
				return true;
			}
			
			// Recursively search in children
			for (const auto& [childName, childEntry] : entry.children)
			{
				if (SearchEntryByPath(childEntry, path))
				{
					return true;
				}
			}
			
			return false;
		}
		
		bool FolderContainsSearchString(const AssetEntry& entry, const std::string& searchString)
		{
			// Check if any of the children match the search string
			for (const auto& [childName, childEntry] : entry.children)
			{
				std::string lowerName = childName;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				
				// If this child matches or any of its children match, return true
				if (lowerName.find(searchString) != std::string::npos || 
				    FolderContainsSearchString(childEntry, searchString))
				{
					return true;
				}
			}
			
			return false;
		}

	private:
		PreviewProviderManager& m_previewProviderManager;
		EditorHost& m_host;
		bool m_visible { true };
		std::map<std::string, AssetEntry> m_assets;
		TexturePtr m_folderTexture;
		const AssetEntry* m_selectedEntry { nullptr };
		float m_columnWidth { 150.0f };
	};
}
