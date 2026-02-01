// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	class PreviewProviderManager;

	/// @brief Implementation of the EditorBase class for previewing spell effects in 3D.
	/// 
	/// This editor provides a 3D preview environment with character models to visualize
	/// spell casting animations, projectiles, and impact effects. Unlike the data-driven
	/// SpellVisualizationEditorWindow, this editor focuses on real-time 3D preview.
	class SpellEffectEditor final : public EditorBase
	{
	public:
		/// @brief Constructor.
		/// @param host The editor host.
		/// @param previewManager The preview provider manager for asset thumbnails.
		explicit SpellEffectEditor(EditorHost& host, PreviewProviderManager& previewManager);

		/// @brief Destructor.
		~SpellEffectEditor() override = default;

	public:
		/// @brief Checks if this editor can load a specific asset type.
		/// @param extension The file extension to check.
		/// @return True if this editor handles the extension.
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		/// @brief Indicates that this editor can create new assets.
		/// @return True.
		[[nodiscard]] bool CanCreateAssets() const override
		{
			return true;
		}

		/// @brief Adds creation menu items.
		void AddCreationContextMenuItems() override;

	protected:
		/// @brief Draws editor UI elements.
		void DrawImpl() override;

		/// @brief Opens an asset in the editor.
		/// @param asset Path to the asset file.
		/// @return The created editor instance.
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		
		/// @brief Closes an editor instance.
		/// @param instance The instance to close.
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		/// @brief Creates a new spell effect preview file.
		void CreateNewSpellEffectPreview();

	private:
		PreviewProviderManager& m_previewManager;
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showNameDialog{ false };
		String m_previewName;
	};
}
