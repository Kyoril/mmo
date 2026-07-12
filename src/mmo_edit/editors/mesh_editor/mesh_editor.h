// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"
#include "proto_data/project.h"

#include <map>

namespace mmo
{
	class PreviewProviderManager;
	class IAudio;

	/// @brief Implementation of the EditorBase class for being able to edit model files.
	class MeshEditor final : public EditorBase
	{
	public:
		explicit MeshEditor(EditorHost& host, PreviewProviderManager& previewManager, proto::Project& project, IAudio* audio = nullptr);

		~MeshEditor() override = default;

	public:
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

		PreviewProviderManager& GetPreviewManager() const { return m_previewManager; }

		proto::Project& GetProject() const { return m_project; }

		IAudio* GetAudio() const { return m_audio; }

	protected:
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:
		PreviewProviderManager& m_previewManager;
		proto::Project& m_project;
		IAudio* m_audio;
		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
	};
}