// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <memory>

#include "editors/editor_base.h"

namespace mmo
{
	/// @brief Editor for the project-wide global shader parameter registry (.hgsp).
	///        Lets the user create, edit and remove global shader variables that are shared by
	///        every material.
	class GlobalShaderParametersEditor final : public EditorBase
	{
	public:
		explicit GlobalShaderParametersEditor(EditorHost& host)
			: EditorBase(host)
		{
		}

	public:
		/// @copydoc EditorBase::CanLoadAsset
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		/// @copydoc EditorBase::CanCreateAssets
		[[nodiscard]] bool CanCreateAssets() const override { return true; }

		/// @copydoc EditorBase::AddCreationContextMenuItems
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
		void CreateNewAsset();

	private:
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showNameDialog { false };
		String m_assetName;
	};
}
