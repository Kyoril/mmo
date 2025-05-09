// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "texture_editor.h"
#include "texture_editor_instance.h"

#include "editors/material_editor/node_editor/item_builder.h"
#include "log/default_log_levels.h"

namespace mmo
{
	TextureEditor::TextureEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	TextureEditor::~TextureEditor()
	{
	}

	void TextureEditor::AddAssetActions(const String& asset)
	{
		if (ImGui::MenuItem("Create Mip Maps"))
		{

		}
	}

	bool TextureEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".htex";
	}

	std::shared_ptr<EditorInstance> TextureEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<TextureEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open mesh editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void TextureEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
	}
}
