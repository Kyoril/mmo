// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	/// @brief Implementation of the EditorBase class for being able to edit model files.
	class WorldEditor final : public EditorBase
	{
	public:
		explicit WorldEditor(EditorHost& host, proto::Project& project);

		~WorldEditor() override = default;

	public:
		/// @brief EditorBase::CanLoadAsset 
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;
		
		/// @brief EditorBase::CanCreateAssets 
		[[nodiscard]] bool CanCreateAssets() const override { return true; }
		
		/// @brief Adds creation items to a context menu.
		void AddCreationContextMenuItems() override;

		proto::Project& GetProject() const { return m_project; }
	protected:
		/// @copydoc EditorBase::DrawImpl
		void DrawImpl() override;
		
		/// @copydoc EditorBase::OpenAssetImpl
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		
		/// @copydoc EditorBase::CloseInstanceImpl
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;
		
	private:
		/// @brief Called when a new world should be created.
		void CreateNewWorld();

	private:
		proto::Project& m_project;
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
		bool m_showWorldNameDialog { false };
		String m_worldName;
	};
}