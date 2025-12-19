// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "grid_snap_settings.h"
#include <imgui.h>
#include <functional>

namespace mmo
{
	class DeferredRenderer;
	class WorldGrid;
	class TransformWidget;
	class WorldEditMode;
	class Selection;
	class SceneOutlineWindow;
	class Texture;

	/// @brief Manages the 3D viewport panel UI.
	/// Displays the rendered scene, handles viewport interactions, toolbar, and drag-drop.
	class ViewportPanel final : public NonCopyable
	{
	public:
		/// @brief Constructs the viewport panel.
		/// @param deferredRenderer Reference to the deferred renderer.
		/// @param worldGrid Reference to the world grid.
		/// @param transformWidget Reference to the transform widget.
		/// @param gridSnapSettings Reference to grid snap settings.
		/// @param selection Reference to the current selection.
		/// @param sceneOutlineWindow Reference to the scene outline window.
		/// @param hovering Reference to the hovering state flag.
		/// @param leftButtonPressed Reference to left button pressed state.
		/// @param rightButtonPressed Reference to right button pressed state.
		/// @param cameraSpeed Reference to camera speed value.
		/// @param lastAvailViewportSize Reference to last viewport size.
		/// @param lastContentRectMin Reference to last content rect min.
		/// @param renderCallback Callback to trigger rendering.
		/// @param generateMinimapsCallback Callback to generate minimaps.
		explicit ViewportPanel(
			DeferredRenderer& deferredRenderer,
			WorldGrid& worldGrid,
			TransformWidget& transformWidget,
			GridSnapSettings& gridSnapSettings,
			Selection& selection,
			SceneOutlineWindow& sceneOutlineWindow,
			bool& hovering,
			bool& leftButtonPressed,
			bool& rightButtonPressed,
			float& cameraSpeed,
			ImVec2& lastAvailViewportSize,
			ImVec2& lastContentRectMin,
			std::function<void()> renderCallback,
			std::function<void()> generateMinimapsCallback);

		~ViewportPanel() override = default;

	public:
		/// @brief Draws the viewport panel.
		/// @param id The ImGui window ID for the panel.
		/// @param currentEditMode The currently active edit mode (can be nullptr).
		void Draw(const String& id, WorldEditMode* currentEditMode);

		/// @brief Sets the transform mode button icons.
		/// @param translateIcon Translate mode icon texture.
		/// @param rotateIcon Rotate mode icon texture.
		/// @param scaleIcon Scale mode icon texture.
		static void SetTransformIcons(
			Texture* translateIcon,
			Texture* rotateIcon,
			Texture* scaleIcon);

	private:
		void HandleViewportInteractions(const ImVec2& availableSpace);
		void DrawViewportToolbar(const ImVec2& availableSpace);
		void DrawSnapSettings();
		void DrawTransformButtons(const ImVec2& availableSpace);
		void HandleViewportDragDrop(WorldEditMode* currentEditMode);

	private:
		DeferredRenderer& m_deferredRenderer;
		WorldGrid& m_worldGrid;
		TransformWidget& m_transformWidget;
		GridSnapSettings& m_gridSnapSettings;
		Selection& m_selection;
		SceneOutlineWindow& m_sceneOutlineWindow;
		bool& m_hovering;
		bool& m_leftButtonPressed;
		bool& m_rightButtonPressed;
		float& m_cameraSpeed;
		ImVec2& m_lastAvailViewportSize;
		ImVec2& m_lastContentRectMin;
		std::function<void()> m_renderCallback;
		std::function<void()> m_generateMinimapsCallback;

		static Texture* s_translateIcon;
		static Texture* s_rotateIcon;
		static Texture* s_scaleIcon;
	};
}
