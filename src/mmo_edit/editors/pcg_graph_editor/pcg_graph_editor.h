// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	/// @brief Editor for PCG graph assets (.hpcgg): a node graph canvas with palette, details and parameters.
	class PcgGraphEditor final : public EditorBase
	{
	public:
		/// @brief Creates the PCG graph editor.
		/// @param host The editor host.
		explicit PcgGraphEditor(EditorHost& host);

		~PcgGraphEditor() override = default;

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
		/// @brief Writes an empty version 1 graph named m_graphName into the current asset browser folder and
		///        opens it. Refuses to overwrite an existing file.
		void CreateNewGraph();

		/// @brief The asset path (forward slashes) a graph named m_graphName would be created at.
		[[nodiscard]] String GetNewGraphPath() const;

	private:
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showGraphNameDialog { false };
		String m_graphName;
	};
}
