// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/path_spline.h"
#include "math/vector3.h"
#include "pcg/oasis.h"
#include "pcg/oasis_record.h"
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
	class ProceduralEditMode final : public WorldEditMode
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

		/// @brief Whether a record changed since it was loaded or saved, or an oasis was deleted.
		bool HasUnsavedChanges() const;

		/// @brief Number of oases in the world.
		size_t GetOasisCount() const { return m_oases.size(); }

		/// @brief One-line summary of the last operation, for logs.
		const String& GetStatus() const { return m_status; }

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
		void CreateOasis();

		/// @brief Removes the oasis' content from the world and the oasis from the list.
		void DeleteOasis(uint64 id);

		/// @brief Selects an oasis (-1 = none).
		void SelectOasis(int32 index);

		/// @brief Refuses (status, log, error) when the recipe is not loaded.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the recipe is loaded.
		bool CheckRecipe(String& error);

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

	private:
		Camera& m_camera;
		SplineEditController m_splineEditor;

		std::vector<Oasis> m_oases;
		std::vector<uint64> m_deletedIds;
		int32 m_selected = -1;

		/// Defaults of the recipe, used for new oases.
		pcg::OasisParams m_defaultParams;
		pcg::OasisAssets m_assets;
		/// Whether the recipe was loaded and parsed.
		bool m_recipeLoaded = false;

		Vector3 m_brushPosition{};
		bool m_brushPositionValid = false;

		String m_status;
		bool m_statusIsError = false;
		uint64 m_pendingDeleteId = 0;

		Matrix4 m_overlayViewProj = Matrix4::Identity;
		ImVec2 m_overlayViewportMin{};
		ImVec2 m_overlayViewportSize{};
	};
}
