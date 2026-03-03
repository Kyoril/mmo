// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>

#include "editors/editor_instance.h"
#include "graphics/texture.h"

namespace mmo
{
	class TextureEditor;

	/// @brief Editor instance for viewing and inspecting textures with zoom and pan support.
	class TextureEditorInstance final : public EditorInstance
	{
	public:
		/// @brief Constructs a new texture editor instance.
		/// @param host The editor host.
		/// @param textureEditor The parent texture editor.
		/// @param assetPath The path to the texture asset.
		explicit TextureEditorInstance(EditorHost& host, TextureEditor& textureEditor, Path assetPath);

		/// @brief Default destructor.
		~TextureEditorInstance() override = default;

	public:
		/// @brief Draws the editor UI.
		void Draw() override;

		/// @brief Saves the asset (currently a no-op for textures).
		bool Save() override;

	private:
		/// @brief Draws the details/properties panel.
		/// @param id The ImGui window ID.
		void DrawDetails(const String& id);

		/// @brief Draws the viewport with the texture preview.
		/// @param id The ImGui window ID.
		void DrawViewport(const String& id);

		/// @brief Draws a checkerboard background pattern to visualize transparency.
		/// @param drawList The ImGui draw list to draw into.
		/// @param topLeft The top-left corner of the area.
		/// @param bottomRight The bottom-right corner of the area.
		/// @param cellSize The size of each checkerboard cell in pixels.
		void DrawCheckerboard(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, float cellSize) const;

		/// @brief Resets zoom and pan to fit the texture in the viewport.
		void ResetView();

	private:
		TextureEditor& m_textureEditor;
		TexturePtr m_texture;
		bool m_initDockLayout{ true };

		float m_zoom{ 1.0f };
		ImVec2 m_panOffset{ 0.0f, 0.0f };
		bool m_isPanning{ false };
		ImVec2 m_lastMousePos{ 0.0f, 0.0f };
		bool m_fitToWindow{ true };

		bool m_showAlpha{ true };
		bool m_showCheckerboard{ true };
	};
}
