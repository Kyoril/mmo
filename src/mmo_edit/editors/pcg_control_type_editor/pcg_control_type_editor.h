// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	/// @brief Editor for PCG control type assets (.hctl): shape, default tags and typed property groups.
	class PcgControlTypeEditor final : public EditorBase
	{
	public:
		/// @brief File extension of control type assets.
		static constexpr const char* AssetExtension = ".hctl";

		/// @brief Creates the control type editor.
		/// @param host The editor host.
		explicit PcgControlTypeEditor(EditorHost& host);

		~PcgControlTypeEditor() override = default;

	public:
		/// @copydoc EditorBase::CanLoadAsset
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		/// @copydoc EditorBase::CanCreateAssets
		[[nodiscard]] bool CanCreateAssets() const override { return true; }

		/// @copydoc EditorBase::AddCreationContextMenuItems
		void AddCreationContextMenuItems() override;

	protected:
		/// @copydoc EditorBase::DrawImpl
		void DrawImpl() override;

		/// @copydoc EditorBase::OpenAssetImpl
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;

		/// @copydoc EditorBase::CloseInstanceImpl
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		/// @brief Writes a default ClosedLoop type without groups named m_typeName into the current asset browser
		///        folder and opens it. Refuses to overwrite an existing file.
		void CreateNewControlType();

		/// @brief The asset path (forward slashes) a type named m_typeName would be created at.
		[[nodiscard]] String GetNewTypePath() const;

	private:
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showNameDialog { false };
		String m_typeName;
	};
}
