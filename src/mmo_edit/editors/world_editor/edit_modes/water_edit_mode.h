// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"
#include "terrain/constants.h"
#include "terrain_io/water_flow.h"
#include "math/matrix4.h"

#include <imgui.h>

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

	private:
		/// A river the flow brush edits: a name for the list and its path in downstream order.
		struct River
		{
			String name;
			terrain_io::water_flow::FlowPath path;
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

		[[nodiscard]] std::vector<terrain_io::water_flow::FlowPath> CollectFlowPaths() const;

		[[nodiscard]] bool ProjectToScreen(const Vector3& worldPosition, ImVec2& screenPosition) const;

		/// Height to draw a flow path point at: the water surface, else the ground.
		[[nodiscard]] float GetFlowPointHeight(float x, float z) const;

		/// Finds the path point under the mouse cursor.
		[[nodiscard]] bool HitTestFlowPoint(int32& riverIndex, int32& pointIndex) const;

		/// Finds the path segment under the mouse cursor (its first point).
		[[nodiscard]] bool HitTestFlowSegment(int32& riverIndex, int32& pointIndex) const;

	private:
		terrain::Terrain& m_terrain;
		Camera& m_camera;

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

		/// Paths and settings of the last apply: pages they reached and the current paths do not
		/// lose their flow on the next apply.
		std::vector<terrain_io::water_flow::FlowPath> m_appliedFlowPaths;
		terrain_io::water_flow::FlowSettings m_appliedFlowSettings;

		int32 m_selectedRiver = -1;
		int32 m_selectedPoint = -1;
		bool  m_flowDragging = false;
		bool  m_flowDragMoved = false;

		/// Half width given to the first point of a new river.
		float m_newRiverHalfWidth = 12.0f;

		bool  m_flowAutoApply = true;
		bool  m_flowAssignRiverMaterial = true;
		bool  m_flowShowArrows = true;
		float m_flowApplyCountdown = -1.0f;
		bool  m_flowFileDirty = false;
		String m_flowStatus;

		// Viewport state of the last overlay draw, used to pick path points with the mouse.
		Matrix4 m_overlayViewProj = Matrix4::Identity;
		ImVec2  m_overlayViewportMin{};
		ImVec2  m_overlayViewportSize{};
	};
}
