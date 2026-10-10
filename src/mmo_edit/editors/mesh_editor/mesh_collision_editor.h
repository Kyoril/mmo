// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "collision_overlay.h"
#include "mesh_collision_geometry.h"
#include "selection.h"
#include "transform_widget.h"

#include "base/non_copyable.h"
#include "scene_graph/mesh.h"

#include <memory>

namespace mmo
{
	class Camera;
	class Entity;
	class Scene;
	class SceneNode;

	namespace collision_view_mode
	{
		/// @brief How the mesh editor shows collision.
		enum Type
		{
			/// @brief No collision visualisation; shapes cannot be edited.
			Off,
			/// @brief Collision drawn over the render mesh.
			Overlay,
			/// @brief Render mesh hidden, only collision drawn.
			CollisionOnly
		};
	}

	/// @brief Collision editing of one mesh: recipe, overlay, shapes panel, picking and gizmo.
	class MeshCollisionEditor final : public NonCopyable
	{
	public:
		/// @brief Loads the mesh's collision recipe (if any) and sets up overlay and gizmo.
		MeshCollisionEditor(Scene& scene, Camera& camera, SceneNode& cameraAnchor, MeshPtr mesh, Entity* entity, String assetPath);

		/// @brief Releases overlay and gizmo.
		~MeshCollisionEditor() override;

	public:
		/// @brief Per-frame update before the scene renders: deferred edits, overlay refresh and gizmo.
		void Update();

		/// @brief Draws the collision panel contents (inside the caller's ImGui window).
		void DrawPanel();

		/// @brief The recipe to save, or nullptr if the mesh had none and collision was never edited.
		[[nodiscard]] const CollisionRecipe* GetRecipeForSave() const;

		/// @brief Whether collision is shown (and shapes are editable).
		[[nodiscard]] bool IsActive() const { return m_viewMode != collision_view_mode::Off; }

		/// @brief Shape by index, or nullptr.
		CollisionShape* GetShape(uint32 index);

		/// @brief Called after a shape changed; final=false only refreshes the preview, true also rebuilds the tree.
		void OnShapeChanged(bool final);

		/// @brief Removes a shape and rebakes.
		void DeleteShape(uint32 index);

		/// @brief Copies a shape (offset slightly) and selects the copy.
		void DuplicateShape(uint32 index);

		/// @brief Deletes a shape at the start of the next Update (safe to call from the shape's own selectable).
		void RequestDeleteShape(uint32 index) { m_pendingDelete = static_cast<int32>(index); }

		/// @brief Duplicates a shape at the start of the next Update (safe to call from the shape's own selectable).
		void RequestDuplicateShape(uint32 index) { m_pendingDuplicate = static_cast<int32>(index); }

		/// @brief Mouse button pressed over the viewport; coordinates are 0..1 within it.
		void OnMousePressed(uint32 button, float x, float y);

		/// @brief Mouse button released; `wasClick` if it barely moved since the press (selects under the cursor).
		void OnMouseReleased(uint32 button, float x, float y, bool wasClick);

		/// @brief Mouse moved; coordinates are 0..1 within the viewport.
		void OnMouseMoved(float x, float y);

		/// @brief Whether the gizmo is being dragged (the caller must not orbit the camera).
		[[nodiscard]] bool IsGizmoActive() const { return m_transformWidget->IsActive(); }

		/// @brief Gizmo mode keys 1-4 and Delete; call once per frame while the viewport is hovered.
		void HandleKeys();

	private:
		void EnsureRecipe();
		void Rebake(bool buildTree);
		void AddShape(collision_shape_type::Type type);
		void SelectShape(int32 index);
		void ApplyViewMode();
		void DrawShapeDetails(CollisionShape& shape);
		String GetSubMeshLabel(uint16 index) const;

	private:
		Scene& m_scene;
		Camera& m_camera;
		SceneNode& m_cameraAnchor;
		MeshPtr m_mesh;
		Entity* m_entity { nullptr };
		String m_assetPath;

		std::unique_ptr<CollisionRecipe> m_recipe;
		bool m_recipeTouched { false };
		bool m_recipeLoaded { false };

		RenderCollisionGeometry m_renderGeometry;
		CollisionBakeResult m_lastBake;
		bool m_overlayDirty { true };
		/// @brief A preview bake ran without rebuilding the tree; rebuild once the mouse is released.
		bool m_previewPending { false };
		int32 m_pendingDelete { -1 };
		int32 m_pendingDuplicate { -1 };

		std::unique_ptr<CollisionOverlay> m_overlay;
		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;
		int32 m_selectedShape { -1 };

		collision_view_mode::Type m_viewMode { collision_view_mode::Off };
		bool m_wireframe { true };
		String m_status;
	};

	/// @brief Gizmo handle for one collision shape; writes transforms back into the recipe.
	class SelectedCollisionShape final : public Selectable
	{
	public:
		/// @brief Selects shape `index` of `editor`.
		SelectedCollisionShape(MeshCollisionEditor& editor, uint32 index);

		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override {}

		/// @copydoc Selectable::Duplicate
		void Duplicate() override;

		/// @copydoc Selectable::Translate
		void Translate(const Vector3& delta) override;

		/// @copydoc Selectable::Rotate
		void Rotate(const Quaternion& delta) override;

		/// @copydoc Selectable::Scale
		void Scale(const Vector3& delta) override;

		/// @copydoc Selectable::Remove
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override {}

		/// @copydoc Selectable::SetPosition
		void SetPosition(const Vector3& position) const override;

		/// @copydoc Selectable::SetOrientation
		void SetOrientation(const Quaternion& orientation) const override;

		/// @copydoc Selectable::SetScale
		void SetScale(const Vector3& scale) const override;

		/// @copydoc Selectable::GetPosition
		Vector3 GetPosition() const override;

		/// @copydoc Selectable::GetOrientation
		Quaternion GetOrientation() const override;

		/// @copydoc Selectable::GetScale
		Vector3 GetScale() const override;

	private:
		MeshCollisionEditor& m_editor;
		uint32 m_index;
	};
}
