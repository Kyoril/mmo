// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include "math/vector3.h"

#include <map>
#include <optional>
#include <utility>

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

		/// @copydoc EditorBase::SetPendingCameraTarget
		void SetPendingCameraTarget(const Path& asset, const Vector3& worldLocation) override;

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

		/// @brief A pending camera target (asset path + world location) applied to the next matching
		///        instance that is opened. Set via SetPendingCameraTarget.
		std::optional<std::pair<Path, Vector3>> m_pendingCameraTarget;
	};
}