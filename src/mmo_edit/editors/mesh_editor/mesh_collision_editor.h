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
}
