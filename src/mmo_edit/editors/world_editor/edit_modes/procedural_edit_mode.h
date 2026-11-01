// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/path_spline.h"
#include "math/ray.h"
#include "math/vector3.h"
#include "pcg/oasis.h"
#include "pcg/oasis_record.h"
#include "pcg/section_asset.h"
#include "pcg/section_path_record.h"
#include "pcg/graph/controls.h"
#include "../selected_control_transform.h"
#include "../selected_spline_point.h"
#include "../spline_edit_controller.h"
#include "../pcg/control_library.h"
#include "../pcg/oasis_builder.h"
#include "../pcg/section_path_builder.h"

#include <imgui.h>

#include <filesystem>
#include <map>
#include <utility>
#include <vector>

namespace mmo
{
	class Camera;

	/// @brief Edit mode for procedurally generated content. Three kinds of control objects:
	///        - an oasis: a closed spline, a seed and parameters (terrain, paint, water and objects);
	///        - a section path: an open 3D spline that lays a baked section asset (.hsec) along it;
	///        - a placed control: a loop, spline, volume or point of a data-driven control type (.hctl of a
	///          biome) with extra tags and property overrides. It generates nothing itself; graph instances
	///          read it (ResolvePlacedControls).
	///        Rebuilding writes the generated content into the world; oasis and section path records are
	///        persisted next to the world as .hpcg (the record kind is peeked from the file), placed
	///        controls as .hpcgc (JSON) in the same folder.
	///        A clicked spline point becomes the editor selection (SelectedSplinePoint), so the transform
	///        widget moves it and turns its tangents; its tangent handles are dragged in the viewport. A
	///        placed volume or point becomes the editor selection as a SelectedControlTransform.
	class ProceduralEditMode final : public WorldEditMode, public ISplinePointOwner, public IControlTransformOwner
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

		/// @brief Loads every *.hpcg record of the folder (oases and section paths), every *.hpcgc placed
		///        control (kept also when its control type is missing) and the shared oasis recipe
		///        (Editor/Pcg/OasisRecipe.json). A missing or invalid recipe falls back to OasisParams
		///        defaults and empty asset lists, and oasis rebuilds are refused until a valid recipe is
		///        loaded. Replaces the current controls.
		/// @param pcgFolder Asset path of the world's folder, e.g. Editor/Pcg/Worlds/Development.
		void Load(const std::filesystem::path& pcgFolder);

		/// @brief Writes changed records and removes the files of deleted controls.
		/// @param pcgFolder Asset path of the world's folder.
		/// @return False if a file could not be written.
		bool Save(const std::filesystem::path& pcgFolder);

		/// @brief Whether the recipe was loaded and parsed. Oasis rebuilds are refused otherwise.
		bool IsRecipeLoaded() const { return m_recipeLoaded; }

		/// @brief Refuses (status, log, error) when the recipe is not loaded.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the recipe is loaded.
		bool CheckRecipe(String& error);

		/// @brief Rebuilds every control (oases and section paths). Continues after a failure; all
		///        errors are joined.
		/// @param error Receives the reason when the call returns false.
		/// @return True if all controls were rebuilt (also when there are none).
		bool RebuildAll(String& error);

		/// @brief Rebuilds one control. An oasis is refused if the recipe is not loaded; a section
		///        path if its section asset is missing or unreadable (a stale asset only warns). Both
		///        are refused if their influence overlaps another built control. A placed control has
		///        nothing to build (graph instances read it); the call succeeds without changes.
		/// @param controlId Id of the control.
		/// @param error Receives the reason when the call returns false.
		/// @return True on success.
		bool Rebuild(uint64 controlId, String& error);

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

		/// @brief Adds a section path through the given x/z points (heights from the terrain, which is
		///        loaded under the points first). Does not build it. Refused if the section asset is
		///        missing or unreadable, or with fewer than 2 points (3 for a loop).
		/// @param points World x/z of the control points, in order.
		/// @param sectionAsset Asset path of the baked section (.hsec).
		/// @param options Seed, loop flag (closed spline, floor side inside), height mode (SmoothedGround for a loop
		///        unless set), mass, facade variety and name=value settings (see ApplyNewSectionPathOptions).
		/// @param name Name of the path; empty picks "Section Path <n>" ("Section Loop <n>" for a loop).
		/// @param id Receives the id of the new path.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the path was added.
		bool CreateSectionPath(const std::vector<std::pair<float, float>>& points, const String& sectionAsset, const NewSectionPathOptions& options,
			const String& name, uint64& id, String& error);

		/// @brief Adds a placed control of a control type. Does not build anything. Loops get a closed and
		///        splines an open Catmull-Rom spline through the points (heights from the terrain, which is
		///        loaded under the points first), one point id per point; volumes (half extents 16 m) and
		///        points sit at the single point, on the ground. The "New Control" menu uses this with the
		///        viewport centre (loop: 4 point square of 32 m; spline: 2 points 32 m apart along the view;
		///        volume and point: the centre).
		/// @param typePath Asset path of the control type (.hctl).
		/// @param points World x/z of the points: a loop needs at least 3, a spline at least 2, a volume or
		///        point exactly one.
		/// @param name Name of the control; empty picks "<type name> <n>".
		/// @param id Receives the id of the new control.
		/// @param error Receives the reason when the call returns false (e.g. "control type '...' not found").
		/// @return True if the control was added.
		bool CreatePlacedControl(const String& typePath, const std::vector<std::pair<float, float>>& points, const String& name, uint64& id, String& error);

		/// @brief Checks property settings against a control type without changing anything.
		/// @param typePath Asset path of the control type.
		/// @param settings Property names and value texts (see ParseControlPropertyValue).
		/// @param error Receives the reason when the call returns false.
		/// @return True if the type exists and every setting names one of its properties with a valid value.
		bool CheckPlacedControlSettings(const String& typePath, const std::vector<std::pair<String, String>>& settings, String& error);

		/// @brief Sets property overrides of a placed control from text.
		/// @param id Id of the placed control.
		/// @param settings Property names and value texts, applied in order.
		/// @param error Receives the reason when the call returns false (nothing is changed then).
		/// @return True if every setting was applied.
		bool SetPlacedControlProperties(uint64 id, const std::vector<std::pair<String, String>>& settings, String& error);

		/// @brief Counter bumped on any change of a placed control (geometry, name, tags, overrides), on every
		///        structural change of the control list of any kind (creation, deletion, load) and on any
		///        change of a control type in the library.
		[[nodiscard]] uint64 GetControlsRevision() const { return m_placedRevision + m_controlLibrary.Revision(); }

		/// @brief Resolves every placed control against its control type, in list order. Controls whose
		///        type is missing or invalid, or whose geometry does not fit the type's shape, are skipped
		///        with a warning. The resolved controls point into this mode's controls and library; they
		///        are valid until the next change of either (see GetControlsRevision).
		/// @param out Receives the resolved controls (cleared first).
		/// @param warnings Receives one message per problem (appended).
		void ResolvePlacedControls(std::vector<pcg::graph::ResolvedControl>& out, std::vector<String>& warnings);

		/// @brief The control types this world's placed controls use (the control type editor invalidates
		///        types here when it saves).
		[[nodiscard]] ControlLibrary& GetControlLibrary() { return m_controlLibrary; }

		/// @brief Removes the control's content from the world and the control from the list (its
		///        record file is removed on the next save). The "Delete" button uses this. A placed control
		///        has no content; it is only removed from the list.
		/// @param id Id of the control.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the control was deleted.
		bool DeleteControl(uint64 id, String& error);

		/// @brief Whether a record changed since it was loaded or saved, or a control was deleted.
		bool HasUnsavedChanges() const;

		/// @brief Number of oases in the world.
		size_t GetOasisCount() const;

		/// @brief Number of controls (oases, section paths and placed controls) in the world.
		size_t GetControlCount() const { return m_controls.size(); }

		/// @brief One-line summary of the last operation, for logs.
		const String& GetStatus() const { return m_status; }

	public:
		/// @brief Gets the spline of the control with the id, or null.
		PathSpline* FindSelectableSpline(uint64 splineId) override;

		/// @brief Gets the terrain height at x/z (zero without terrain).
		float GetSplinePointGroundHeight(float x, float z) const override;

		/// @brief Marks the control changed.
		void OnSplinePointEdited(uint64 splineId) override;

		/// @brief Deletes a control point (and its point id on a section path), keeping at least the
		///        minimum point count. Does not touch the editor selection.
		bool RemoveSplinePoint(uint64 splineId, size_t pointIndex) override;

		/// @brief Drops the point selection when the editor selection drops the point.
		void OnSplinePointDeselected(const SelectedSplinePoint& selectable) override;

	public:
		/// @brief Gets the placed volume or point control with the id, or null.
		pcg::graph::ControlInstance* FindTransformControl(uint64 controlId) override;

		/// @brief Marks the control changed.
		void OnControlTransformEdited(uint64 controlId) override;

		/// @brief Drops the transform selection when the editor selection drops it.
		void OnControlTransformDeselected(const SelectedControlTransform& selectable) override;

	private:
		/// @brief Kind of a control object.
		enum class ControlKind : uint8
		{
			Oasis,
			SectionPath,
			Placed
		};

		/// @brief A control in the list and whether it must be written on the next save.
		struct Control
		{
			ControlKind kind = ControlKind::Oasis;
			/// Used when kind is Oasis.
			pcg::OasisRecord oasis;
			/// Used when kind is SectionPath.
			pcg::SectionPathRecord path;
			/// Used when kind is Placed.
			pcg::graph::ControlInstance placed;
			bool dirty = false;
			/// Failed attempts to load the terrain under a loop without heights (see EnsureHeights).
			uint32 heightAttempts = 0;
			/// Overlay frame of the next attempt.
			uint64 nextHeightAttemptFrame = 0;

			/// @brief Id of the control.
			uint64 Id() const { return kind == ControlKind::Oasis ? oasis.id : (kind == ControlKind::SectionPath ? path.id : placed.id); }
			/// @brief Display name.
			const String& Name() const { return kind == ControlKind::Oasis ? oasis.name : (kind == ControlKind::SectionPath ? path.name : placed.name); }
			/// @brief Display name.
			String& Name() { return kind == ControlKind::Oasis ? oasis.name : (kind == ControlKind::SectionPath ? path.name : placed.name); }
			/// @brief The edited spline (oasis loop, section path, placed loop or spline; empty for a placed
			///        volume or point).
			PathSpline& Spline() { return kind == ControlKind::Oasis ? oasis.loop : (kind == ControlKind::SectionPath ? path.spline : placed.spline); }
			/// @brief The edited spline (see above).
			const PathSpline& Spline() const { return kind == ControlKind::Oasis ? oasis.loop : (kind == ControlKind::SectionPath ? path.spline : placed.spline); }
			/// @brief Stable point ids of the spline (section paths and placed controls), or null for an oasis.
			std::vector<uint64>* PointIds() { return kind == ControlKind::SectionPath ? &path.pointIds : (kind == ControlKind::Placed ? &placed.pointIds : nullptr); }
			/// @brief Next point id to hand out (section paths and placed controls), or null for an oasis.
			uint64* NextPointId() { return kind == ControlKind::SectionPath ? &path.nextPointId : (kind == ControlKind::Placed ? &placed.nextPointId : nullptr); }
			/// @brief What the control has in the world (always empty for a placed control).
			const pcg::OwnedContent& Content() const { return kind == ControlKind::Oasis ? oasis.content : (kind == ControlKind::SectionPath ? path.content : NoContent()); }
			/// @brief Whether this is a placed volume or point (no spline; edited through a SelectedControlTransform).
			bool HasTransform() const { return kind == ControlKind::Placed && placed.spline.points.empty(); }

			/// @brief Shared empty content of placed controls.
			static const pcg::OwnedContent& NoContent();
		};

		/// @brief A section asset as read for the details panel, with the read error.
		struct CachedSectionAsset
		{
			bool valid = false;
			pcg::SectionAsset asset;
			String error;
		};

		/// @brief Severity of the status line.
		enum class StatusLevel : uint8
		{
			Ok,
			Warning,
			Error
		};

		/// @brief Asset path of the file of a control: "<folder>/<id as 16 hex digits>.hpcg".
		static String MakeFileName(const std::filesystem::path& folder, uint64 id);

		/// @brief Asset path of the file of a placed control: "<folder>/<id as 16 hex digits>.hpcgc".
		static String MakePlacedFileName(const std::filesystem::path& folder, uint64 id);

		/// @brief Marks a control changed (written on the next save; a placed control also bumps the
		///        controls revision).
		void MarkChanged(Control& control);

		/// @brief Bumps the controls revision after any structural change of the control list (add, remove,
		///        reorder, load), whatever the kind: it may move the placed controls resolved controls point at.
		void OnControlsChanged();

		/// @brief Applies an unfinished edit of the extra tags field to the control it was typed for.
		void ApplyPendingTags();

		/// @brief Whether a placed control without a spline is a volume (its type says so; also when the
		///        type is missing, so its box stays visible and editable) rather than a point.
		bool IsPlacedVolume(const Control& control);

		/// @brief "An oasis loop", "A section path" or "A placed control", for messages.
		static const char* DescribeKind(const Control& control);

		/// @brief Adds a placed control of the type at the viewport's ground hit (see CreatePlacedControl).
		void CreatePlacedControlAtViewportCentre(const String& typePath);

		/// @brief Draws the biome combo and the "New Control" menu of the active biome's control types.
		void DrawNewControlMenu();

		/// @brief Draws the details of a selected placed control.
		void DrawPlacedControlDetails(Control& control);

		/// @brief Makes a placed volume or point the editor selection (replacing whatever was selected).
		/// @param controlIndex Index of the control in the list.
		void SelectTransformInEditor(int32 controlIndex);

		/// @brief Drops the transform selection from the editor selection if it holds it.
		void ClearTransformSelection();

		/// @brief Hit-tests the handles of placed volumes and points in the viewport.
		/// @return Index of the control under the cursor, or -1.
		int32 HitTestTransformControl(float screenX, float screenY) const;

		/// @brief Draws placed volumes (rotated boxes) and points (marker with a yaw arrow).
		void DrawPlacedTransforms(ImDrawList& drawList);

		/// @brief Index of the control with the id or -1.
		int32 FindControl(uint64 id) const;

		/// @brief Adds a default 8 point loop of radius 40 around the viewport's ground hit.
		void CreateOasisAtViewportCentre();

		/// @brief Adds a 2 point section path from the viewport's ground hit, 20 m along the camera's
		///        forward direction, with the section asset picked in the panel.
		void CreateSectionPathAtViewportCentre();

		/// @brief Adds an 8 point section loop of radius 60 m around the viewport's ground hit, with the
		///        section asset picked in the panel (height mode SmoothedGround).
		void CreateSectionLoopAtViewportCentre();

		/// @brief Selects a control (-1 = none).
		void SelectControl(int32 index);

		/// @brief Rebuilds one oasis (see Rebuild).
		bool RebuildOasisControl(Control& control, String& error);

		/// @brief Rebuilds one section path (see Rebuild).
		bool RebuildSectionPathControl(Control& control, String& error);

		/// @brief The influence rects a built control occupies in the world: an oasis' last influence; a
		///        section path's per-copy rects of its last build as stored in its record, or its last
		///        influence box when the record has none (version 1 file). Empty if the control has nothing
		///        in the world.
		static std::vector<pcg::WorldRect> GetBuiltRects(const Control& control);

		/// @brief Checks influence rects against every other built control.
		/// @param control The control being rebuilt.
		/// @param rects Its new influence rects.
		/// @param error Names the control in the way when the call returns false.
		bool CheckOverlap(const Control& control, const std::vector<pcg::WorldRect>& rects, String& error) const;

		/// @brief Sets the status line (no logging).
		void SetStatus(const String& text, StatusLevel level);

		/// @brief Gets the section asset for the details panel (read once, refreshed by Rebuild).
		const CachedSectionAsset& GetCachedSectionAsset(const String& assetPath);

		/// @brief Draws the details of a selected oasis.
		void DrawOasisDetails(Control& control);

		/// @brief Draws the details of a selected section path.
		void DrawSectionPathDetails(Control& control);

		/// @brief Draws a combo of the baked section assets.
		/// @return True if the selection changed.
		bool DrawSectionAssetCombo(const char* label, String& assetPath);

		/// @brief Minimum number of control points of a control (3 for an oasis or any closed spline, else 2).
		static uint32 GetMinPoints(const Control& control);

		/// @brief Describes a report for the status line.
		static String DescribeReport(const OasisBuildReport& report, const String& what);

		/// @brief Spline pointers of all controls, in list order, for the spline editor.
		std::vector<PathSpline*> GetSplines();

		/// @brief Height to draw spline handles at.
		float GetHandleHeight(float x, float z) const;

		/// @brief Projects a world position to the screen with the last drawn viewport.
		bool ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const;

		/// @brief Deletes the selected spline point (keeps at least the control's minimum).
		void DeleteSelectedPoint();

		/// @brief Makes a spline point the editor selection (replacing whatever was selected).
		/// @param controlIndex Index of the control in the list.
		/// @param pointIndex Index of the point in its spline.
		void SelectPointInEditor(int32 controlIndex, int32 pointIndex);

		/// @brief Deselects the spline point: in the spline editor, and in the editor selection if the
		///        selection holds it.
		void ClearPointSelection();

		/// @brief Draws the selected point's tangent tools in the details panel.
		void DrawPointDetails(Control& control);

		/// @brief Puts the loop points of an oasis record without heights (read from a version 1/2 file,
		///        or just created) on the terrain, with level tangents, after loading the terrain pages
		///        under the loop. Until then the loop cannot be edited (overwriting edits would lose them).
		///        Section paths always have heights.
		/// @param retry Overlay use: attempts are spaced out, and after HeightAttemptLimit failures the
		///        fallback is used. Otherwise (rebuild, save, creation) a failure falls back at once.
		/// @return True if the record has valid heights afterwards.
		bool EnsureHeights(Control& control, bool retry);

		/// @brief Fills heights from whatever terrain is available (zero elsewhere), with a warning.
		void FillHeightsFallback(Control& control);

		/// @brief Whether the spline of the control at the index can be edited (oasis loop heights are
		///        valid; section paths always).
		bool IsLoopEditable(int32 index) const;

		/// @brief Spline pointers for the spline editor's input handling: controls whose spline cannot be
		///        edited yet stand in as an empty spline (same indices), so nothing on them can be hit.
		std::vector<PathSpline*> GetEditableSplines();

		/// @brief Tells the transform widget that the selected point's tangents changed (its local-space
		///        orientation follows the out tangent).
		void NotifyPointRotated();

	private:
		Camera& m_camera;
		SplineEditController m_splineEditor;

		std::vector<Control> m_controls;
		std::vector<uint64> m_deletedIds;
		int32 m_selected = -1;

		/// The selected point's handle while the editor selection holds it (owned by the selection;
		/// reset through OnSplinePointDeselected when the selection drops it).
		SelectedSplinePoint* m_pointSelectable = nullptr;

		/// The selected volume's or point's handle while the editor selection holds it (owned by the
		/// selection; reset through OnControlTransformDeselected when the selection drops it).
		SelectedControlTransform* m_transformSelectable = nullptr;

		/// Control types of the placed controls, and the types the "New Control" menu offers.
		ControlLibrary m_controlLibrary;

		/// Biome whose control types the "New Control" menu offers (empty: the first biome).
		String m_activeBiome;

		/// Bumped on every placed-control change (see GetControlsRevision).
		uint64 m_placedRevision = 0;

		/// Text of the extra tags field while it is being edited (applied when the edit ends).
		String m_tagsBuffer;
		/// Control whose tags m_tagsBuffer holds, and whether the field was active last frame.
		uint64 m_tagsControlId = 0;
		bool m_tagsEditing = false;

		/// Stand-in for loops that cannot be edited yet (GetEditableSplines). Never has points.
		PathSpline m_lockedSpline;

		/// Counts overlay frames, to space out height attempts.
		uint64 m_overlayFrame = 0;

		/// Section assets read for the details panel, by asset path.
		std::map<String, CachedSectionAsset> m_sectionAssets;

		/// Section asset the "New Section Path" button uses.
		String m_newPathAsset;

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
		StatusLevel m_statusLevel = StatusLevel::Ok;
		uint64 m_pendingDeleteId = 0;

		Matrix4 m_overlayViewProj = Matrix4::Identity;
		ImVec2 m_overlayViewportMin{};
		ImVec2 m_overlayViewportSize{};
	};
}
