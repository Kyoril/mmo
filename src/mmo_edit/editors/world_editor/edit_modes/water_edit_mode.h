// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"
#include "terrain/constants.h"

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

	private:
		void UpdateBrushOverlay();

		void UpdateWaterWireframe();

		void ApplyBrush();

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

		ManualRenderObject* m_waterWireframe = nullptr;
		SceneNode*          m_waterWireframeNode = nullptr;
	};
}
