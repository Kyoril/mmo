// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "proto_data/project.h"
#include "math/vector3.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"
#include "graphics/texture.h"
#include "terrain/brush_stroke.h"
#include "terrain/flatten_plane.h"
#include "terrain/terrain_region_snapshot.h"

#include "editors/world_editor/terrain_undo_stack.h"

#include <vector>
#include <optional>

namespace mmo
{
	class Camera;
	class WaterEditMode;

	namespace terrain
	{
		class Terrain;
	}

	/// Enumerates possible terrain editing types.
	enum class TerrainEditType : uint8
	{
		/// Allows you to select and manage certain terrain tiles and view or adjust their.
		Select,

		/// Rectangle-select terrain regions for copy/cut/paste/move operations.
		Region,

		/// Allows you to deform the terrain geometry in certain ways.
		Deform,

		/// Allows you to paint the terrain tiles with one of four layers.
		Paint,

		/// Allows you to assign area ids to terrain tiles by painting them.
		Area,

		VertexShading,

		/// Allows you to create or remove holes in the terrain.
		Holes,

		/// Paint, erase, and adjust water on terrain tiles (sub-mode of terrain editing).
		Water,

		/// The total number of terrain editing types. Always the last element!
		Count_
	};

	/// Enumerates the possible terrain deform modes.
	enum class TerrainDeformMode : uint8
	{
		/// Sculpt the terrain, increasing or lowering its height values.
		Sculpt,

		/// Smoothes the terrain, averaging the height values of the terrain tiles.
		Smooth,

		/// Flatten the terrain, making all terrain tiles have the same height.
		Flatten,

		/// Apply Perlin noise to the terrain, varying height naturally.
		Noise,

		/// Applies a one-click height stamp shaped by the imported brush mask (or noise).
		Stamp,

		/// The total number of terrain deform modes. Always the last element!
		Count_
	};

	/// Enumerates the possible terrain paint modes.
	enum class TerrainPaintMode : ::uint8
	{
		/// Paint a specific layer on terrain tiles.
		Paint,

		/// Smooth out painted terrain layers, blending them together smoothly.
		Smooth,


		/// The total number of terrain paint modes. Always the last element!
		Count_
	};

	/// Enumerates the possible vertex shading modes.
	enum class TerrainVertexShadingMode : ::uint8
	{
		/// Paint the selected colour with the brush.
		Paint,

		/// Replace every vertex colour of the page under the cursor with the selected colour.
		Fill,

		/// The total number of vertex shading modes. Always the last element!
		Count_
	};

	/// Enumerates the possible terrain hole modes.
	enum class TerrainHoleMode : ::uint8
	{
		/// Add holes to the terrain.
		Add,

		/// Remove holes from the terrain (fill them in).
		Remove,


		/// The total number of terrain hole modes. Always the last element!
		Count_
	};


	/// This class handles certain world editor operations while in terrain editing mode.
	class TerrainEditMode final : public WorldEditMode
	{
	public:
		explicit TerrainEditMode(IWorldEditor& worldEditor, terrain::Terrain& terrain, const proto::ZoneManager& zones, Camera& camera);
		~TerrainEditMode() override;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnMouseDown(float x, float y) override;

		void OnMouseHold(float deltaSeconds) override;

		void OnStrokeInterrupted() override { m_strokeActive = false; m_pendingStrokePoints.clear(); }

		void OnMouseMoved(float x, float y) override;

		void OnMouseUp(float x, float y) override;

		void OnMouseWheel(float delta) override;

		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize) override;

	public:
		void SetTerrainEditType(const TerrainEditType type) { m_type = type; }

		void SetWaterEditMode(WaterEditMode* waterMode) { m_waterEditMode = waterMode; }

		[[nodiscard]] TerrainEditType GetTerrainEditType() const { return m_type; }

		void SetDeformMode(const TerrainDeformMode mode) { m_deformMode = mode; }

		[[nodiscard]] TerrainDeformMode GetDeformMode() const { return m_deformMode; }

		void SetPaintMode(const TerrainPaintMode mode) { m_paintMode = mode; }

		[[nodiscard]] TerrainPaintMode GetPaintMode() const { return m_paintMode; }

		void SetVertexShadingMode(const TerrainVertexShadingMode mode) { m_vertexShadingMode = mode; }

		[[nodiscard]] TerrainVertexShadingMode GetVertexShadingMode() const { return m_vertexShadingMode; }

		void SetHoleMode(const TerrainHoleMode mode) { m_holeMode = mode; }

		[[nodiscard]] TerrainHoleMode GetHoleMode() const { return m_holeMode; }

		void SetBrushPosition(const Vector3& position);

	private:
		/// Applies the active brush operation once, swept along a stroke.
		/// @param stroke Segment the cursor covered since the last application. Zero-length for a
		///        stationary brush, which makes the swept footprint a plain circle.
		/// @param innerRadius Radius of the brush's full-strength core.
		/// @param outerRadius Radius at which the brush falls off to nothing.
		/// @param factor Direction multiplier: -1 while shift inverts the operation.
		/// @param deltaSeconds Duration of the frame this application covers. The rate at which
		///        the accumulating operations work, so dwell time controls how much they deposit.
		/// @param passFraction How much of one full brush pass this application contributes,
		///        derived from the distance covered, so the saturating operations deposit the same
		///        amount per unit of length whatever the cursor speed or frame rate.
		///
		/// Which of the two an operation is driven by depends on whether it converges. Painting
		/// and vertex shading blend toward a target, so overlapping applications saturate and a
		/// normalised pass is what keeps a stroke from blotching. The deform operations do not:
		/// sculpt and noise add without bound, and smooth and flatten interpolate by a coefficient
		/// that overshoots its target above 1. Those are driven by time, which is also the control
		/// an artist expects from them — dwell longer, deform more.
		void ApplyBrushStroke(const terrain::BrushStroke& stroke, float innerRadius, float outerRadius, float factor, float deltaSeconds, float passFraction);

		/// Number of overlapping stamps needed to cover a stroke, for the operations that cannot
		/// be swept: a brush mask is anchored to one footprint, and area IDs are set per tile.
		/// @param stroke The stroke to cover.
		/// @param spacing Distance between consecutive stamps.
		/// @return At least 1, capped so a very fast movement cannot stall the frame.
		static uint32 StampCountForStroke(const terrain::BrushStroke& stroke, float spacing);

		void UpdateBrushOverlay();

		/// Rebuilds the area-ID overlay that colours terrain tiles by their assigned zone.
		/// Shows coloured tile outlines when in Area edit mode; clears them otherwise.
		void UpdateAreaOverlay();

		/// Returns a stable ARGB colour (0xAARRGGBB) for the given area ID.
		/// Area ID 0 → neutral grey.  IDs 1+ cycle through a 16-colour palette.
		static uint32 GetColorForAreaId(uint32 areaId);

		/// Loads a brush mask image (PNG/JPG/BMP/TGA/PSD) from disk. The red channel is used as
		/// a greyscale mask. Returns true on success and refreshes the preview texture.
		bool LoadBrushMask(const String& path);

		/// Rebuilds the small preview texture shown for the currently loaded brush mask.
		void UpdateBrushMaskPreview();

		/// Samples the loaded brush mask at normalized coordinates (u, v) in [0, 1], applying the
		/// current rotation and invert settings. Returns a value in [0, 1]; out-of-range coords → 0.
		[[nodiscard]] float SampleBrushMask(float u, float v) const;

		/// Converts two world-space drag corners into a clamped, vertex-snapped selection rect.
		[[nodiscard]] terrain::region_math::VertexRect SelectionFromWorldCorners(const Vector3& a, const Vector3& b) const;

		/// Rebuilds the draped selection-rectangle outline (or clears it when idle).
		void UpdateRegionOverlay();

		/// Clears the selection and any ghost drag, hiding both overlays.
		void ClearRegionSelection();

		/// True when a committed (non-empty, non-dragging) selection exists.
		[[nodiscard]] bool HasRegionSelection() const
		{
			return (m_regionState == RegionEditState::Selected || m_regionState == RegionEditState::GhostDrag)
				&& !m_selection.IsEmpty();
		}

		/// Captures the current selection into the clipboard.
		void CopySelection();

		/// Captures the selection into the clipboard, records undo, and edge-fills the source.
		void CutSelection();

		/// Enters ghost-drag mode. isMove additionally captures the selection as the move source
		/// so the commit can edge-fill it.
		void BeginGhostDrag(bool isMove);

		/// Applies the clipboard at the current ghost position (move also fills the source),
		/// recording a single undo entry.
		void CommitGhostDrag();

		/// Leaves ghost-drag mode without mutating the terrain.
		void CancelGhostDrag();

		/// Destination rect of the ghost, centered on the cursor and clamped so it fits.
		[[nodiscard]] terrain::region_math::VertexRect ComputeGhostDestRect() const;

		/// Rebuilds the translucent clipboard height-grid preview at the cursor.
		void UpdateGhostOverlay();

		/// Keyboard shortcuts: Ctrl+C/X/V, Esc, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y.
		void HandleShortcuts();

		/// Region-mode section of the details panel.
		void DrawRegionDetails();

		/// Applies one stamp at the current brush position (mouse-down driven, undoable).
		void ApplyStamp();

		/// Re-pins the Flatten reference plane from the surface under the cursor.
		/// @param asSlopePoint False to move the anchor, keeping the current tilt. True to keep
		///        the anchor and tilt the plane so it also runs through this second point, which
		///        is how a ramp is drawn from one end of it to the other.
		void PickFlattenPlanePoint(bool asSlopePoint);

		/// Flatten-specific section of the details panel: the plane, its pickers, and the
		/// raise/lower and hard/soft options.
		void DrawFlattenDetails();

		/// Rebuilds the translucent disc showing where the flatten plane sits under the cursor.
		void UpdateFlattenPlaneOverlay();


		/// Shared brush-mask import/invert/rotation/preview controls (used by Paint and Stamp).
		void DrawBrushMaskControls();

		/// @brief Draws the per-layer texture, scale and height blend controls of the terrain
		///        material under the cursor, when that material declares layer bindings.
		/// @details Deliberately minimal for a first cut. Not implemented, in rough order of
		///          usefulness: a thumbnail for the layer's albedo (needs a PreviewProviderManager
		///          threaded into WorldEditor, which is also why the tile material picker has
		///          none), dropping a .htex onto the viewport to assign it (needs
		///          SupportsViewportDrop), a "create instance override" path plus a confirmation
		///          before editing a .hmat directly, and a dirty marker with an explicit save
		///          rather than the standing warning text.
		void DrawLayerPropertiesSection();

		/// @brief Resolves which material the layer property controls should edit.
		/// @details Prefers the material of the tile the brush is over, falling back to the
		///          terrain's default material. Returns null when neither is available.
		///
		///          Note this tracks the BRUSH, not a selection, so the edit target follows the
		///          cursor around the viewport. That is safe during a drag - the panel does not
		///          move the brush, and only TerrainEditMode::OnMouseMoved updates it - but it
		///          does mean the "Editing <name>" label can change between interactions. Latch
		///          it on first edit if that ever becomes confusing.
		[[nodiscard]] MaterialPtr ResolveLayerPropertyMaterial() const;

		/// @brief Writes a scalar parameter to a material AND to every loaded tile instance
		///        parented to it, which is what makes the change visible immediately.
		/// @details Each tile owns a private MaterialInstance whose parameters were copied
		///          from the parent at construction, so writing the parent alone changes
		///          nothing on screen. RefreshParametersFromBase cannot be used here: it
		///          re-copies the parent list but then restores the instance's own value by
		///          name, which would keep the stale value.
		void ApplyLayerScalarParameter(const MaterialPtr& material, const String& parameterName, float value);

	private:
		/// State machine for the Region edit type.
		enum class RegionEditState : uint8
		{
			/// Nothing selected.
			Idle,

			/// Left mouse held, rubber-banding the selection rectangle.
			Dragging,

			/// A selection rectangle exists.
			Selected,

			/// A clipboard ghost follows the cursor awaiting a commit click (move/paste).
			GhostDrag,
		};

		RegionEditState m_regionState = RegionEditState::Idle;
		terrain::region_math::VertexRect m_selection{};
		Vector3 m_regionDragStart{};
		std::optional<terrain::TerrainRegionSnapshot> m_clipboard;
		bool m_ghostIsMove = false;
		float m_ghostHeightOffset = 0.0f;
		ManualRenderObject* m_regionOverlay = nullptr;
		SceneNode* m_regionOverlayNode = nullptr;
		ManualRenderObject* m_ghostOverlay = nullptr;
		SceneNode* m_ghostOverlayNode = nullptr;
		TerrainUndoStack m_undoStack;

	private:
		terrain::Terrain& m_terrain;

		const proto::ZoneManager& m_zones;

		Camera& m_camera;

		TerrainEditType m_type = TerrainEditType::Select;

		TerrainDeformMode m_deformMode = TerrainDeformMode::Sculpt;

		TerrainPaintMode m_paintMode = TerrainPaintMode::Paint;

		TerrainHoleMode m_holeMode = TerrainHoleMode::Add;

		TerrainVertexShadingMode m_vertexShadingMode = TerrainVertexShadingMode::Paint;

		/// The reference surface the Flatten brush drives terrain toward. A slope of zero makes
		/// it the plain target height this replaced.
		terrain::FlattenPlane m_flattenPlane;

		/// Whether Flatten may only raise, only lower, or move terrain either way.
		terrain::flatten_mode::Type m_flattenMode = terrain::flatten_mode::Both;

		/// True to land on the plane in one pass at full brush strength rather than easing.
		bool m_flattenHard = false;


		float m_terrainBrushSize = 0.5f;

		float m_terrainBrushHardness = 0.5f;

		float m_terrainBrushPower = 10.0f;

		float m_noiseFrequency = 0.01f;
		float m_noiseAmplitude = 5.0f;
		int   m_noiseOctaves = 4;
		float m_noisePersistence = 0.5f;

		float m_stampStrength = 10.0f;

		uint8 m_terrainPaintLayer = 0;

		// Brush mask: an imported greyscale image used as a paint stencil/pattern.
		bool                m_useBrushMask = false;
		bool                m_brushMaskInvert = false;
		float               m_brushMaskRotation = 0.0f;   ///< Degrees, applied around the mask center.
		int                 m_brushMaskWidth = 0;
		int                 m_brushMaskHeight = 0;
		std::vector<float>  m_brushMaskData;              ///< Row-major normalized [0,1] mask values.
		String              m_brushMaskName;              ///< File name shown in the UI.
		TexturePtr          m_brushMaskPreviewTex;
		bool                m_brushMaskPreviewInvert = false; ///< Invert state baked into the preview.

		Vector3 m_brushPosition{};

		/// World position the brush was last applied at. Together with m_strokeActive this
		/// turns a per-frame point application into a continuous stroke.
		Vector3 m_lastStrokePosition{};

		/// True once a stroke has applied at least once, so there is a segment to interpolate
		/// along. Cleared whenever the stroke is interrupted or ends.
		bool m_strokeActive = false;

		/// Brush positions seen since the last application. Pointer events usually arrive
		/// several times per frame, so collecting them keeps a fast curve a curve instead of
		/// the single chord a per-frame sample would reduce it to.
		std::vector<Vector3> m_pendingStrokePoints;

		uint32 m_selectedArea = 0;

		uint32 m_selectedColor = 0xFFFFFFF;

		ManualRenderObject* m_brushCircles = nullptr;
		SceneNode*          m_brushCirclesNode = nullptr;
		ManualRenderObject* m_vertexDots = nullptr;
		SceneNode*          m_vertexDotsNode = nullptr;
		bool                m_brushPositionValid = false;

		// Translucent preview of the Flatten reference plane, drawn as a disc the size of the
		// brush at the height the plane has under the cursor.
		ManualRenderObject* m_flattenPlaneOverlay = nullptr;
		SceneNode*          m_flattenPlaneOverlayNode = nullptr;


		// Area-ID tile overlay — coloured outlines for each assigned terrain tile.
		ManualRenderObject* m_areaOverlay = nullptr;
		SceneNode*          m_areaOverlayNode = nullptr;
		bool                m_areaOverlayDirty = false;      ///< Rebuilt on next mouse-up after painting.
		TerrainEditType     m_lastTerrainType = TerrainEditType::Count_; ///< Detects mode switches.

		WaterEditMode*      m_waterEditMode = nullptr;

		// Noise preview texture (128×128 R8 grayscale, rebuilt when noise params change)
		TexturePtr          m_noisePreviewTex;
		float               m_noisePreviewFrequency = -1.0f; // sentinel: force first build
		float               m_noisePreviewAmplitude = -1.0f;
		int                 m_noisePreviewOctaves   = -1;
		float               m_noisePreviewPersistence = -1.0f;
	};
}
