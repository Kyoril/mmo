// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "model_editor.h"

#include "model_editor_instance.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ModelEditor::ModelEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	void ModelEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	bool ModelEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".hmsh";
	}

	std::shared_ptr<EditorInstance> ModelEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<ModelEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open model editor instance");
			return nullptr;
		}

		return instance.first->second;
	}
}
