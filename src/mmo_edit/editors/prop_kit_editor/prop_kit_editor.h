// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	class PreviewProviderManager;

	/// @brief Editor for prop kit assets (.hkit): props and lights placed inside a preview building.
	class PropKitEditor final : public EditorBase
	{
	public:
		/// @brief Creates the prop kit editor.
		/// @param host The editor host.
		/// @param previewManager Preview provider used by the asset pickers (may be null).
		explicit PropKitEditor(EditorHost& host, PreviewProviderManager* previewManager);

		~PropKitEditor() override = default;

	public:
		/// @copydoc EditorBase::CanLoadAsset
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		/// @copydoc EditorBase::CanCreateAssets
		[[nodiscard]] bool CanCreateAssets() const override { return true; }

		/// @copydoc EditorBase::AddCreationContextMenuItems
		void AddCreationContextMenuItems() override;

		/// @brief Gets the preview provider used by the asset pickers (may be null).
		[[nodiscard]] PreviewProviderManager* GetPreviewManager() const { return m_previewManager; }

	protected:
		/// @copydoc EditorBase::DrawImpl
		void DrawImpl() override;

		/// @copydoc EditorBase::OpenAssetImpl
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;

		/// @copydoc EditorBase::CloseInstanceImpl
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		/// @brief Writes a valid, empty prop kit named m_kitName into the current asset browser folder
		///        and opens it. Refuses to overwrite an existing file.
		void CreateNewPropKit();

		/// @brief The asset path (forward slashes) a kit named m_kitName would be created at.
		[[nodiscard]] String GetNewKitPath() const;

	private:
		PreviewProviderManager* m_previewManager { nullptr };
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showKitNameDialog { false };
		String m_kitName;
	};
}
