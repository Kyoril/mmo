// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "proto_data/project.h"
#include "math/vector3.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"
#include "graphics/texture.h"

#include <vector>

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

		void SetHoleMode(const TerrainHoleMode mode) { m_holeMode = mode; }

		[[nodiscard]] TerrainHoleMode GetHoleMode() const { return m_holeMode; }

		void SetBrushPosition(const Vector3& position);

	private:
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

	private:
		terrain::Terrain& m_terrain;

		const proto::ZoneManager& m_zones;

		Camera& m_camera;

		TerrainEditType m_type = TerrainEditType::Select;

		TerrainDeformMode m_deformMode = TerrainDeformMode::Sculpt;

		TerrainPaintMode m_paintMode = TerrainPaintMode::Paint;

		TerrainHoleMode m_holeMode = TerrainHoleMode::Add;

		float m_deformFlattenHeight = 0.0f;

		float m_terrainBrushSize = 0.5f;

		float m_terrainBrushHardness = 0.5f;

		float m_terrainBrushPower = 10.0f;

		float m_noiseFrequency = 0.01f;
		float m_noiseAmplitude = 5.0f;
		int   m_noiseOctaves = 4;
		float m_noisePersistence = 0.5f;

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

		uint32 m_selectedArea = 0;

		uint32 m_selectedColor = 0xFFFFFFF;

		ManualRenderObject* m_brushCircles = nullptr;
		SceneNode*          m_brushCirclesNode = nullptr;
		ManualRenderObject* m_vertexDots = nullptr;
		SceneNode*          m_vertexDotsNode = nullptr;
		bool                m_brushPositionValid = false;

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
