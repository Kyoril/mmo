// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <set>

#include "base/typedefs.h"

namespace mmo
{
	class PreviewProviderManager;

	/// \brief Reusable ImGui widget for picking assets from the asset registry.
	///
	/// Features:
	/// - Filter by file extensions
	/// - Search/filter by text
	/// - Preview using PreviewProviderManager
	/// - Drag & drop support from Asset Browser
	/// - Combo box dropdown with asset list
	class AssetPickerWidget
	{
	public:
		/// \brief Draw an asset picker with preview and dropdown.
		/// \param label ImGui label for the widget.
		/// \param currentAssetPath Current asset path (will be modified if user selects a new asset).
		/// \param extensions Set of allowed file extensions (e.g., {".htex", ".blp"}).
		/// \param previewManager Preview provider manager for showing asset previews (optional).
		/// \param previewSize Size of the preview image in pixels (default 64x64).
		/// \return True if the asset path was changed.
		static bool Draw(
			const char* label,
			std::string& currentAssetPath,
			const std::set<String>& extensions,
			PreviewProviderManager* previewManager = nullptr,
			float previewSize = 64.0f);

	private:
		/// \brief Get list of assets matching the extensions.
		static std::vector<String> GetFilteredAssets(const std::set<String>& extensions);
	};
}
