// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/path_spline.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "pcg/oasis.h"
#include "pcg/oasis_record.h"
#include "../selected_spline_point.h"
#include "../spline_edit_controller.h"
#include "../pcg/oasis_builder.h"

#include <imgui.h>

#include <filesystem>
#include <vector>

namespace mmo
{
	class Camera;

	/// @brief Edit mode for procedurally generated content. An oasis is a control object: a closed
	///        spline, a seed and parameters. Rebuilding writes terrain, paint, water and placed
	///        objects into the world; the record is persisted next to the world as .hpcg.
	///        A clicked loop point becomes the editor selection (SelectedSplinePoint), so the transform
	///        widget moves it and turns its tangents; its tangent handles are dragged in the viewport.
	class ProceduralEditMode final : public WorldEditMode, public ISplinePointOwner
	{
	public:
		/// @brief Creates the mode.
		/// @param editor The world editor this mode edits.
		/// @param camera The camera used to project the spline handles.
		ProceduralEditMode(IWorldEditor& editor, Camera& camera);

	public:
		/// @copydoc WorldEditMode::GetName
		const char* GetName() const override;

		/// @copydoc WorldEditMode::DrawDetails
		void DrawDetails() override;

		/// @copydoc WorldEditMode::OnDeactivate
		void OnDeactivate() override;

		/// @copydoc WorldEditMode::OnMouseDown
		void OnMouseDown(float x, float y) override;

		/// @copydoc WorldEditMode::OnMouseHold
		void OnMouseHold(float deltaSeconds) override;

		/// @copydoc WorldEditMode::OnMouseMoved
		void OnMouseMoved(float x, float y) override;

		/// @copydoc WorldEditMode::OnMouseUp
		void OnMouseUp(float x, float y) override;

		/// @copydoc WorldEditMode::DrawViewportOverlay
		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize) override;

	public:
		/// @brief Called by the world editor instance when the terrain raycast under the cursor succeeds.
		/// @param position World position of the hit.
		void SetBrushPosition(const Vector3& position);

		/// @brief Called by the world editor instance with the camera ray under the cursor.
		/// @param ray The ray from the camera through the cursor.
		void SetCursorRay(const Ray& ray);

		/// @brief Whether a spline point is being dragged (the camera must not rotate then).
		bool IsDragging() const { return m_splineEditor.IsDragging(); }

		/// @brief Loads every *.hpcg record of the folder and the shared recipe
		///        (Editor/Pcg/OasisRecipe.json). A missing or invalid recipe falls back to OasisParams
		///        defaults and empty asset lists, and rebuilds are refused until a valid recipe is
		///        loaded. Replaces the current oases.
		/// @param pcgFolder Asset path of the world's folder, e.g. Editor/Pcg/Worlds/Development.
		void Load(const std::filesystem::path& pcgFolder);

		/// @brief Writes changed records and removes the files of deleted oases.
		/// @param pcgFolder Asset path of the world's folder.
		/// @return False if a file could not be written.
		bool Save(const std::filesystem::path& pcgFolder);

		/// @brief Whether the recipe was loaded and parsed. Rebuilds are refused otherwise.
		bool IsRecipeLoaded() const { return m_recipeLoaded; }

		/// @brief Refuses (status, log, error) when the recipe is not loaded.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the recipe is loaded.
		bool CheckRecipe(String& error);

		/// @brief Rebuilds every oasis. Continues after a failure; all errors are joined. Refused
		///        when the recipe is not loaded.
		/// @param error Receives the reason when the call returns false.
		/// @return True if all oases were rebuilt (also when there are none).
		bool RebuildAll(String& error);

		/// @brief Rebuilds one oasis. Refused if the recipe is not loaded or the loop's influence
		///        overlaps another built oasis.
		/// @param oasisId Id of the oasis.
		/// @param error Receives the reason when the call returns false.
		/// @return True on success.
		bool Rebuild(uint64 oasisId, String& error);

		/// @brief Adds an oasis with the recipe's default parameters: a closed 8 point Catmull-Rom
		///        loop of the given radius around (x, z). Does not build it. The "New Oasis" button
		///        uses this with the viewport centre, radius 40 and seed 1.
		/// @param x World X of the centre.
		/// @param z World Z of the centre.
		/// @param radius Loop radius in world units, must be positive.
		/// @param seed Seed of the oasis.
		/// @param name Name of the oasis; empty picks "Oasis <n>".
		/// @param id Receives the id of the new oasis.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the oasis was added.
		bool CreateOasisAt(float x, float z, float radius, uint64 seed, const String& name, uint64& id, String& error);

		/// @brief Removes the oasis' content from the world and the oasis from the list (its record
		///        file is removed on the next save). The "Delete" button uses this.
		/// @param id Id of the oasis.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the oasis was deleted.
		bool DeleteOasis(uint64 id, String& error);

		/// @brief Whether a record changed since it was loaded or saved, or an oasis was deleted.
		bool HasUnsavedChanges() const;

		/// @brief Number of oases in the world.
		size_t GetOasisCount() const { return m_oases.size(); }

		/// @brief One-line summary of the last operation, for logs.
		const String& GetStatus() const { return m_status; }

	public:
		/// @brief Gets the loop of the oasis with the id, or null.
		PathSpline* FindSelectableSpline(uint64 splineId) override;

		/// @brief Gets the terrain height at x/z (zero without terrain).
		float GetSplinePointGroundHeight(float x, float z) const override;

		/// @brief Marks the oasis changed.
		void OnSplinePointEdited(uint64 splineId) override;

		/// @brief Deletes a loop point, keeping at least the minimum point count. Does not touch the
		///        editor selection.
		bool RemoveSplinePoint(uint64 splineId, size_t pointIndex) override;

		/// @brief Drops the point selection when the editor selection drops the point.
		void OnSplinePointDeselected(const SelectedSplinePoint& selectable) override;

	private:
		/// @brief An oasis in the list and whether it must be written on the next save.
		struct Oasis
		{
			pcg::OasisRecord record;
			bool dirty = false;
		};

		/// @brief Asset path of the file of an oasis: "<folder>/<id as 16 hex digits>.hpcg".
		static String MakeFileName(const std::filesystem::path& folder, uint64 id);

		/// @brief Index of the oasis with the id or -1.
		int32 FindOasis(uint64 id) const;

		/// @brief Adds a default 8 point loop of radius 40 around the viewport's ground hit.
		void CreateOasisAtViewportCentre();

		/// @brief Selects an oasis (-1 = none).
		void SelectOasis(int32 index);

		/// @brief Checks the loop's influence against the other built oases.
		/// @param error Names the oasis in the way when the call returns false.
		bool CheckOverlap(const Oasis& oasis, String& error) const;

		/// @brief Describes a report for the status line.
		static String DescribeReport(const OasisBuildReport& report, const String& what);

		/// @brief Spline pointers of all oases, in list order, for the spline editor.
		std::vector<PathSpline*> GetSplines();

		/// @brief Height to draw spline handles at.
		float GetHandleHeight(float x, float z) const;

		/// @brief Projects a world position to the screen with the last drawn viewport.
		bool ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const;

		/// @brief Deletes the selected spline point (keeps at least 3 points).
		void DeleteSelectedPoint();

		/// @brief Makes a loop point the editor selection (replacing whatever was selected).
		/// @param oasisIndex Index of the oasis in the list.
		/// @param pointIndex Index of the point in its loop.
		void SelectPointInEditor(int32 oasisIndex, int32 pointIndex);

		/// @brief Deselects the loop point: in the spline editor, and in the editor selection if the
		///        selection holds it.
		void ClearPointSelection();

		/// @brief Draws the selected point's tangent tools in the details panel.
		void DrawPointDetails(Oasis& oasis);

	private:
		Camera& m_camera;
		SplineEditController m_splineEditor;

		std::vector<Oasis> m_oases;
		std::vector<uint64> m_deletedIds;
		int32 m_selected = -1;

		/// The selected point's handle while the editor selection holds it (owned by the selection;
		/// reset through OnSplinePointDeselected when the selection drops it).
		SelectedSplinePoint* m_pointSelectable = nullptr;

		/// Defaults of the recipe, used for new oases.
		pcg::OasisParams m_defaultParams;
		pcg::OasisAssets m_assets;
		/// Whether the recipe was loaded and parsed.
		bool m_recipeLoaded = false;

		Vector3 m_brushPosition{};
		bool m_brushPositionValid = false;

		// A dragged point moves on the horizontal plane through its ground height, not along the
		// terrain under the cursor: rim rocks hide the ground, so a terrain pick behind them
		// would make the point jump tens of metres.
		/// Records the grabbed point's ground height and the grab offset of the point or tangent
		/// handle when a drag starts. Handles move on the plane through their point's ground height.
		void BeginPlaneDrag();

		Ray m_cursorRay;
		bool m_cursorRayValid = false;
		float m_dragPlaneY = 0.0f;
		float m_dragOffsetX = 0.0f;
		float m_dragOffsetZ = 0.0f;

		String m_status;
		bool m_statusIsError = false;
		uint64 m_pendingDeleteId = 0;

		Matrix4 m_overlayViewProj = Matrix4::Identity;
		ImVec2 m_overlayViewportMin{};
		ImVec2 m_overlayViewportSize{};
	};
}
