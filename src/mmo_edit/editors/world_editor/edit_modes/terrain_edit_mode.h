// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "base/typedefs.h"
#include "proto_data/project.h"
#include "math/vector3.h"

namespace mmo
{
	class Camera;

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


	/// This class handles certain world editor operations while in terrain editing mode.
	class TerrainEditMode final : public WorldEditMode
	{
	public:
		explicit TerrainEditMode(IWorldEditor& worldEditor, terrain::Terrain& terrain, const proto::ZoneManager& zones, Camera& camera);
		~TerrainEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnMouseHold(float deltaSeconds) override;

		void OnMouseMoved(float x, float y) override;

		void OnMouseUp(float x, float y) override;

	public:
		void SetTerrainEditType(const TerrainEditType type) { m_type = type; }

		[[nodiscard]] TerrainEditType GetTerrainEditType() const { return m_type; }

		void SetDeformMode(const TerrainDeformMode mode) { m_deformMode = mode; }

		[[nodiscard]] TerrainDeformMode GetDeformMode() const { return m_deformMode; }

		void SetPaintMode(const TerrainPaintMode mode) { m_paintMode = mode; }

		[[nodiscard]] TerrainPaintMode GetPaintMode() const { return m_paintMode; }

		void SetBrushPosition(const Vector3& position);

	private:
		terrain::Terrain& m_terrain;

		const proto::ZoneManager& m_zones;

		Camera& m_camera;

		TerrainEditType m_type = TerrainEditType::Select;

		TerrainDeformMode m_deformMode = TerrainDeformMode::Sculpt;

		TerrainPaintMode m_paintMode = TerrainPaintMode::Paint;

		float m_deformFlattenHeight = 0.0f;

		float m_terrainBrushSize = 0.5f;

		float m_terrainBrushHardness = 0.5f;

		float m_terrainBrushPower = 10.0f;

		uint8 m_terrainPaintLayer = 0;

		Vector3 m_brushPosition{};

		uint32 m_selectedArea = 0;

		uint32 m_selectedColor = 0xFFFFFFF;
	};
}
