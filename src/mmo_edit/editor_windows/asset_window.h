// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <string>
#include <vector>

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
		bool IsDockable() const override { return true; }

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

		/// @brief Returns true if the given asset path is in the favorites list.
		[[nodiscard]] bool IsFavorite(const std::string& path) const;

		/// @brief Adds the given asset path to favorites and persists the change.
		void AddToFavorites(const std::string& path);

		/// @brief Removes the given asset path from favorites and persists the change.
		void RemoveFromFavorites(const std::string& path);

		/// @brief Writes the current favorites list to disk as JSON.
		void SaveFavorites() const;

		/// @brief Loads the favorites list from disk.
		void LoadFavorites();

		/// @brief Renders the thumbnail grid for a given list of asset paths (used for both the
		///        regular folder view and the favorites view).
		void RenderAssetGrid(const std::vector<std::string>& paths, const std::string& searchString, bool isFavoritesView);

	private:
		PreviewProviderManager& m_previewProviderManager;
		EditorHost& m_host;
		bool m_visible { true };
		std::map<std::string, AssetEntry> m_assets;
		TexturePtr m_folderTexture;
		const AssetEntry* m_selectedEntry { nullptr };
		float m_columnWidth { 200.0f };
		std::string m_assetPendingDelete;
		bool m_showDeleteConfirmation { false };
		std::string m_folderPendingDelete;
		bool m_showFolderDeleteConfirmation { false };
		bool m_showFolderDeleteProgress { false };
		std::vector<std::string> m_filesToDelete;
		size_t m_filesDeletedCount { 0 };

		/// @brief Flat list of favorited asset paths, persisted to editor_favorites.json.
		std::vector<std::string> m_favorites;

		/// @brief True when the Favorites view is active in the right panel.
		bool m_showingFavorites { false };
	};
}
