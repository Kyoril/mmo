// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>

#include "editors/editor_instance.h"
#include "graphics/material_instance.h"
#include "graphics/render_texture.h"
#include "graphics/vertex_buffer.h"
#include "graphics/vertex_index_data.h"

namespace mmo
{
	class TextureEditor;

	class TextureEditorInstance final : public EditorInstance
	{
	public:
		explicit TextureEditorInstance(EditorHost& host, TextureEditor& textureEditor, Path assetPath);
		~TextureEditorInstance() override = default;

	public:
		void Draw() override;

		void Render();

	private:
		void Save();

		void DrawDetails(const String& id);

		void DrawViewport(const String& id);

	private:
		TextureEditor& m_textureEditor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		bool m_initDockLayout{ true };

		std::shared_ptr<VertexData> m_vertexData;
		std::shared_ptr<MaterialInstance> m_previewMaterialInst;
	};
}
