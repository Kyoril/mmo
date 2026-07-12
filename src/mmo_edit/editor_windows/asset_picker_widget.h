// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <set>
#include <functional>

#include "base/typedefs.h"

namespace mmo
{
	class PreviewProviderManager;
	class IAudio;

	/// \brief Common file-extension sets for asset picker fields.
	///
	/// Prefer these over declaring per-call-site extension sets so that all pickers
	/// for the same kind of asset stay in sync.
	namespace asset_extensions
	{
		/// Sound files playable by the audio system.
		inline const std::set<String> Sounds = { ".wav", ".WAV", ".ogg", ".mp3" };
		/// Materials and material instances.
		inline const std::set<String> Materials = { ".hmat", ".hmi" };
		/// Base materials only (no material instances).
		inline const std::set<String> BaseMaterials = { ".hmat" };
		/// Static meshes.
		inline const std::set<String> Meshes = { ".hmsh" };
		/// Textures (icons etc.).
		inline const std::set<String> Textures = { ".htex", ".blp" };
		/// Particle systems.
		inline const std::set<String> Particles = { ".hpar" };
		/// World model objects.
		inline const std::set<String> WorldModels = { ".hwmo" };
		/// Customizable character definitions.
		inline const std::set<String> CharacterDefinitions = { ".char" };
		/// Unit model files (static meshes or character definitions).
		inline const std::set<String> ModelFiles = { ".hmsh", ".char" };
	}

	/// \brief Reusable ImGui widget for picking assets from the asset registry.
	///
	/// Features:
	/// - Filter by file extensions
	/// - Search/filter by text
	/// - Preview using PreviewProviderManager
	/// - Audio preview button (requires IAudio instance)
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
		/// \param audioSystem Audio system for previewing sound files (optional).
		/// \param previewSize Size of the preview image in pixels (default 64x64).
		/// \param onNavigateCallback Callback invoked when user clicks the navigate button (optional).
		/// \return True if the asset path was changed.
		static bool Draw(
			const char* label,
			std::string& currentAssetPath,
			const std::set<String>& extensions,
			PreviewProviderManager* previewManager = nullptr,
			IAudio* audioSystem = nullptr,
			float previewSize = 64.0f,
			std::function<void(const std::string&)> onNavigateCallback = nullptr);

	private:
		/// \brief Get list of assets matching the extensions.
		static std::vector<String> GetFilteredAssets(const std::set<String>& extensions);
	};
}
