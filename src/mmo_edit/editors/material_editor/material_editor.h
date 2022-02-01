// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <map>

#include "editors/editor_base.h"

namespace mmo
{
	class MaterialEditor final : public EditorBase
	{
	public:
		explicit MaterialEditor(EditorHost& host)
			: EditorBase(host)
		{
		}
		
		/// @copydoc EditorBase::CanLoadAsset 
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;
		
		/// @brief EditorBase::CanCreateAssets 
		[[nodiscard]] bool CanCreateAssets() const override { return true; }
		
		/// @brief Adds creation items to a context menu.
		void AddCreationContextMenuItems() override;

	protected:
		void DrawImpl() override;

	protected:
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
