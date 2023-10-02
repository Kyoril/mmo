// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>

#include "editors/editor_base.h"

namespace mmo
{
	/// @brief Editor implementation to support creation and editing of materials.
	class MaterialEditor final : public EditorBase
	{
	public:
		/// @brief Default constructor.
		/// @param host Host which provides support for stuff editors might require.
		explicit MaterialEditor(EditorHost& host)
			: EditorBase(host)
		{
		}

	public:
		/// @copydoc EditorBase::CanLoadAsset 
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;
		
		/// @brief EditorBase::CanCreateAssets 
		[[nodiscard]] bool CanCreateAssets() const override { return true; }
		
		/// @brief Adds creation items to a context menu.
		void AddCreationContextMenuItems() override;

		void AddAssetActions(const String& asset) override;

	protected:
		/// @copydoc EditorBase::DrawImpl
		void DrawImpl() override;
		
		/// @copydoc EditorBase::OpenAssetImpl 
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		
		/// @copydoc EditorBase::CloseInstanceImpl
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		/// @brief Called when a new material should be created.
		void CreateNewMaterial();

	private:
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showMaterialNameDialog { false };
		String m_materialName;
	};
}
