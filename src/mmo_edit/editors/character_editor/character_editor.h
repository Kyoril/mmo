// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	/// @brief Implementation of the EditorBase class for being able to edit model files.
	class CharacterEditor final : public EditorBase
	{
	public:
		explicit CharacterEditor(EditorHost& host);

		~CharacterEditor() override = default;

	public:
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		/// @brief EditorBase::CanCreateAssets 
		[[nodiscard]] bool CanCreateAssets() const override { return true; }

		/// @brief Adds creation items to a context menu.
		void AddCreationContextMenuItems() override;

		void AddAssetActions(const String& asset) override;

	private:
		void CreateNewCharacterData();

	protected:
		/// @copydoc EditorBase::DrawImpl
		void DrawImpl() override;

		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:

		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showNameDialog{ false };
		String m_dataName;
	};
}
