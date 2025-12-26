// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_editor.h"

#include "mesh_editor_instance.h"
#include "log/default_log_levels.h"

namespace mmo
{
	MeshEditor::MeshEditor(EditorHost& host, PreviewProviderManager& previewManager)
		: EditorBase(host)
		, m_previewManager(previewManager)
	{
	}

	void MeshEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	bool MeshEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".hmsh";
	}

	std::shared_ptr<EditorInstance> MeshEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<MeshEditorInstance>(m_host, *this, m_previewManager, asset));
		if (!instance.second)
		{
			ELOG("Failed to open mesh editor instance");
			return nullptr;
		}

		return instance.first->second;
	}
}
