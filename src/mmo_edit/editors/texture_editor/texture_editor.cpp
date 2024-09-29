// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "texture_editor.h"

#include "editors/material_editor/item_builder.h"

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
		// TODO
		return nullptr;
	}

	void TextureEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
	}
}
