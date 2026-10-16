// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_instance.h"
#include "editors/mesh_editor/collision_overlay.h"
#include "selection.h"
#include "transform_widget.h"

#include "base/signal.h"
#include "deferred_shading/deferred_renderer.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/world_model.h"

#include <imgui.h>

#include <memory>
#include <vector>

namespace mmo
{
	class PropKitEditor;
	class Camera;
	class SceneNode;
	class Light;
	class ManualRenderObject;
	class WorldModelInstance;

	/// @brief What is selected in the prop kit editor.
	enum class PropKitSelectionType
	{
		/// @brief Nothing is selected.
		None,
		/// @brief A prop of the kit is selected.
		Prop,
		/// @brief A light of the kit is selected.
		Light
	};

	/// @brief Editor instance for one prop kit asset (.hkit).
	///
	/// The kit is shown inside its preview building through a WorldModelInstance that has the edited
	/// kit as its only kit, so the preview is realized exactly like in the game. Every edit marks the
	/// preview dirty, and it is rebuilt at most once per frame.
	class PropKitEditorInstance final : public EditorInstance
	{
	public:
		/// @brief Opens a prop kit for editing.
		/// @param host The editor host.
		/// @param editor The owning prop kit editor.
		/// @param asset The asset path of the kit.
		explicit PropKitEditorInstance(EditorHost& host, PropKitEditor& editor, Path asset);

		/// @brief Destroys the preview and all scene objects.
		~PropKitEditorInstance() override;

	public:
		/// @brief Renders the viewport content. Connected to EditorHost::beforeUiUpdate.
		void Render();

		/// @copydoc EditorInstance::Draw
		void Draw() override;

		/// @copydoc EditorInstance::OnMouseButtonDown
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::OnMouseButtonUp
		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::OnMouseMoved
		void OnMouseMoved(uint16 x, uint16 y) override;

		/// @brief Writes the kit to its file, then replaces the kit cached in PropKitManager so that
		///        worlds pick up the change when they reload their placements.
		/// @copydoc EditorInstance::Save
		bool Save() override;

		/// @brief Marks the kit content as changed so that the preview, markers and collision display
		///        are rebuilt on the next frame.
		void MarkKitChanged();

	private:
		/// @brief Loads the kit file into m_kit. Leaves an empty kit if the file cannot be read.
		void LoadKit();

		/// @brief Recreates the preview building instance from m_kit->previewWorldModel.
		void RecreatePreviewInstance();

		/// @brief Destroys the preview building instance.
		void DestroyPreviewInstance();

		/// @brief Moves the camera so that the preview building (or the kit props) are in view.
		void FocusPreview();

		/// @brief Rebuilds the light and selection markers.
		void RebuildMarkers();

		/// @brief Rebuilds the collision display of the kit props.
		void RebuildCollisionOverlay();

		/// @brief Adds a prop to the kit and selects it.
		/// @param meshPath The mesh (.hmsh) of the prop.
		/// @param position Position in world model space.
		void AddProp(const String& meshPath, const Vector3& position);

		/// @brief Adds a light to the kit and selects it.
		/// @param type The light type.
		void AddLight(WorldModelLight::LightType type);

		/// @brief Removes a prop from the kit.
		void RemoveProp(size_t index);

		/// @brief Removes a light from the kit.
		void RemoveLight(size_t index);

		/// @brief Selects a prop or light and puts the gizmo on it.
		/// @param type What to select.
		/// @param index Index of the prop or light; ignored for None.
		void Select(PropKitSelectionType type, int32 index);

		/// @brief Selects the prop or light under a viewport position.
		/// @param viewportX Horizontal viewport position in 0..1.
		/// @param viewportY Vertical viewport position in 0..1.
		void PickAt(float viewportX, float viewportY);

		/// @brief Gets the point where a viewport position hits the ground plane (y = 0).
		Vector3 GetDropPosition(float viewportX, float viewportY) const;

		/// @brief Draws the viewport window.
		void DrawViewport(const String& id);

		/// @brief Draws the kit window (name, preview building, prop and light lists).
		void DrawKitPanel(const String& id);

		/// @brief Draws the details window of the current selection.
		void DrawDetailsPanel(const String& id);

	private:
		PropKitEditor& m_editor;
		scoped_connection m_renderConnection;

		/// @brief The kit being edited. The preview instance shares it.
		PropKitPtr m_kit;

		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Camera* m_camera { nullptr };
		SceneNode* m_lightNode { nullptr };
		Light* m_mainLight { nullptr };
		std::unique_ptr<DeferredRenderer> m_deferredRenderer;
		std::unique_ptr<WorldGrid> m_worldGrid;

		/// @brief Node and instance of the preview building. The building is placed at the origin
		///        so that world model space equals world space.
		SceneNode* m_previewNode { nullptr };
		std::shared_ptr<WorldModelInstance> m_previewInstance;

		/// @brief Path the preview instance was created from, to notice picker changes.
		String m_previewPath;

		/// @brief Line markers for lights and the selected prop.
		ManualRenderObject* m_markers { nullptr };

		/// @brief Collision display of the kit props.
		std::unique_ptr<CollisionOverlay> m_collisionOverlay;
		std::vector<Vector3> m_collisionVertices;
		std::vector<uint32> m_collisionIndices;
		bool m_showCollision { false };

		bool m_previewDirty { true };
		bool m_markersDirty { true };
		bool m_collisionDirty { true };

		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;
		PropKitSelectionType m_selectedType { PropKitSelectionType::None };
		int32 m_selectedIndex { -1 };

		/// @brief Set when the selected prop or light should be removed at the end of the frame.
		bool m_deleteRequested { false };

		ImVec2 m_lastAvailViewportSize { 0.0f, 0.0f };
		ImVec2 m_lastContentRectMin { 0.0f, 0.0f };
		int16 m_lastMouseX { 0 };
		int16 m_lastMouseY { 0 };
		int32 m_mouseDownX { 0 };
		int32 m_mouseDownY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		bool m_hovering { false };
		bool m_initDockLayout { true };
		Vector3 m_cameraVelocity { Vector3::Zero };
		float m_cameraSpeed { 10.0f };
	};
}
