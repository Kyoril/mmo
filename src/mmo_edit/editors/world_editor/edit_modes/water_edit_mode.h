// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"
#include "terrain/constants.h"
#include "terrain_io/water_flow.h"
#include "math/path_spline.h"
#include "../spline_edit_controller.h"
#include "math/matrix4.h"

#include <imgui.h>

#include <functional>
#include <vector>

namespace mmo
{
	class Camera;

	namespace terrain
	{
		class Terrain;
	}

	/// Enumerates the available water brush modes.
	enum class WaterBrushMode : uint8
	{
		/// Fill water quads within the brush area at a fixed height.
		Paint,

		/// Erase water quads within the brush area.
		Erase,

		/// Set all water vertex heights within the brush area to a fixed value.
		Flatten,

		/// Apply a directional height ramp along the drag direction.
		Ramp,

		/// Raise water vertex heights within the brush area.
		Raise,

		/// Lower water vertex heights within the brush area.
		Lower,

		/// Author river flow by drawing paths; shows the stored flow as arrows.
		Flow,

		/// The total number of modes. Always the last element!
		Count_
	};

	/// Edit mode for painting, erasing, and adjusting liquid water on the terrain.
	class WaterEditMode final : public WorldEditMode
	{
	public:
		explicit WaterEditMode(IWorldEditor& worldEditor, terrain::Terrain& terrain, Camera& camera);
		~WaterEditMode() override;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnMouseDown(float x, float y) override;

		void OnMouseHold(float deltaSeconds) override;

		void OnMouseMoved(float x, float y) override;

		void OnMouseUp(float x, float y) override;

		void OnMouseWheel(float delta) override;

		void DrawViewportOverlay(ImDrawList* drawList, const ImVec2& viewportMin, const ImVec2& viewportSize) override;

	public:
		/// Called by the world editor instance when the terrain raycast succeeds.
		void SetBrushPosition(const Vector3& position);

		/// Invalidates the brush position and clears the brush circle overlay.
		void ClearBrushPosition();

		/// Loads the world's river paths. A missing file means no rivers.
		/// @param fileName Asset path of the .hflow file.
		void LoadFlowPaths(const String& fileName);

		/// Writes the world's river paths if they changed (removes the file once no river is left).
		/// @param fileName Asset path of the .hflow file.
		void SaveFlowPaths(const String& fileName);

		/// Supplies the river paths of the world's graph instances (ProceduralEditMode::CollectGeneratedFlowPaths).
		using GeneratedFlowPathsProvider = std::function<std::vector<terrain_io::water_flow::FlowPath>()>;

		/// Sets where the generated river paths come from. They take part in every flow apply and are shown
		/// read-only in the overlay; they are never written to the .hflow file.
		void SetGeneratedFlowPathsProvider(GeneratedFlowPathsProvider provider);

		/// The hand-drawn river paths and flow settings that are on the terrain pages now: a pending (deferred)
		/// apply is carried out first, so these equal the rivers and settings currently edited, saved or not.
		/// A graph instance rebuild combines them with the generated paths.
		/// @param paths Receives the usable hand paths (two points or more), in river order.
		/// @param settings Receives the flow settings.
		void GetAppliedHandFlow(std::vector<terrain_io::water_flow::FlowPath>& paths, terrain_io::water_flow::FlowSettings& settings);

	private:
		/// A river the flow brush edits: a name for the list and its path in downstream order.
		struct River
		{
			String name;
			PathSpline path;
		};

		void UpdateBrushOverlay();

		void ApplyBrush();

		void DrawFlowDetails();

		void DrawFlowOverlay(ImDrawList& drawList);

		void FlowMouseDown();

		/// Writes the flow of all rivers into the terrain pages they reach.
		void ApplyFlow();

		/// Applies the flow shortly after the last change, so dragging sliders or the wheel does not
		/// recompute every frame.
		void ScheduleFlowApply();

		void DeleteSelectedFlowPoint();

		/// The usable paths (two points or more) of the hand-drawn rivers, in river order.
		[[nodiscard]] std::vector<terrain_io::water_flow::FlowPath> CollectHandFlowPaths() const;

		/// The river paths of the world's graph instances as they are now (empty without a provider).
		[[nodiscard]] std::vector<terrain_io::water_flow::FlowPath> CollectGeneratedFlowPaths() const;

		/// Pointers to the river splines, in river order, for the spline editor.
		[[nodiscard]] std::vector<PathSpline*> GetRiverSplines();

		[[nodiscard]] bool ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const;

		/// Height to draw a flow path point at: the water surface, else the ground.
		[[nodiscard]] float GetFlowPointHeight(float x, float z) const;


	private:
		terrain::Terrain& m_terrain;
		Camera& m_camera;

		/// Shared viewport editing of the river paths (selection, dragging, inserting, deleting).
		SplineEditController m_splineEditor;

		WaterBrushMode m_brushMode = WaterBrushMode::Paint;

		/// Liquid type painted by the Paint brush mode.
		terrain::WaterType m_waterType = terrain::WaterType::Water;

		/// Water surface height applied when painting.
		float m_waterHeight = 0.0f;

		/// Brush radius in world units. Default ~half a tile so a single tile is the typical target.
		float m_brushRadius = 20.0f;

		/// Height delta applied per second by Raise/Lower modes while mouse is held.
		float m_raiseLowerSpeed = 2.0f;

		/// Material name assigned to all pages touched by the last paint stroke.
		char m_materialName[256] = {};

		/// Ramp start position (set on mouse-down in Ramp mode).
		Vector3 m_rampStartPos{};
		float   m_rampStartHeight = 0.0f;
		float   m_rampEndHeight   = 0.0f;
		bool    m_rampActive      = false;

		Vector3 m_brushPosition{};
		bool    m_brushPositionValid = false;

		ManualRenderObject* m_brushCircle = nullptr;
		SceneNode*          m_brushCircleNode = nullptr;

		// River flow authoring
		std::vector<River> m_rivers;
		terrain_io::water_flow::FlowSettings m_flowSettings;

		/// Hand-drawn paths and settings of the last apply: pages they reached and the current paths do not
		/// lose their flow on the next apply. Only the hand-drawn part is kept: graph instance rebuilds change
		/// the generated paths behind this mode's back, so each apply takes them fresh from the provider for
		/// both the current and the previous set.
		std::vector<terrain_io::water_flow::FlowPath> m_appliedFlowPaths;
		terrain_io::water_flow::FlowSettings m_appliedFlowSettings;

		/// Half width given to the first point of a new river.
		float m_newRiverHalfWidth = 12.0f;

		bool  m_flowAutoApply = true;
		bool  m_flowAssignRiverMaterial = true;
		bool  m_flowShowArrows = true;
		float m_flowApplyCountdown = -1.0f;
		bool  m_flowFileDirty = false;
		String m_flowStatus;

		/// Source of the graph instances' river paths.
		GeneratedFlowPathsProvider m_generatedFlowPaths;

		// Viewport state of the last overlay draw, used to pick path points with the mouse.
		Matrix4 m_overlayViewProj = Matrix4::Identity;
		ImVec2  m_overlayViewportMin{};
		ImVec2  m_overlayViewportSize{};
	};
}
