// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "editor_instance.h"

namespace mmo
{
	/// @brief An editor instance for editing a material.
	class MaterialEditorInstance final : public EditorInstance
	{
	public:
		MaterialEditorInstance(EditorHost& host, const Path& assetPath)
			: EditorInstance(host, assetPath)
		{
		}

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;

	private:
		float m_previewSize { 400.0f };
		float m_detailsSize { 100.0f };
	};
}
