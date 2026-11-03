// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "evaluator.h"

#include "base/typedefs.h"
#include "pcg/terrain_access.h"
#include "pcg/terrain_contribution.h"

#include <functional>
#include <unordered_map>

namespace mmo::pcg::graph
{
	/// @brief Bit i set = tile layer i under (x, z) carries the texture (the terrain's paint matching rule).
	using LayerMatchFunction = std::function<uint8(const String& textureKey, float x, float z)>;

	/// @brief Reads the terrain as it would be after pcg::Unapply of a contribution, so an evaluation sees the base
	///        terrain a rebuild evaluates on while the live terrain still carries the previous build.
	/// @details Per recorded sample it uses the value Unapply would restore: heights (outer vertices, water vertices)
	///          become base where the live value equals what was written, else live - (written - base); splat pixels
	///          and water quads become base where the live value equals what was written, else stay live. What the
	///          contribution did not record reads live, and every query whose samples are all unrecorded passes
	///          through to the live sampler unchanged. Queries follow the editor terrain:
	///          - Height: live smooth height minus the bilinear (live - base) of the four outer vertices around
	///            (x, z) (the smooth height is that bilinear interpolation of the outer vertices).
	///          - Normal: per corner vertex of the cell, central differences of the base heights over +-1 vertex
	///            (no left / up neighbour at a page-local index 0, like Page::CalculateNormalAt), scaled by
	///            vertexSpacing x stepsPerPage / (stepsPerPage + 1) (PageSize / OuterVerticesPerPageSide), quantised to
	///            SNorm8 like the stored normals, blended bilinearly and normalised (Page::GetSmoothNormalAt). Only where
	///            a corner or one of its neighbours is recorded; elsewhere the live normal.
	///          - SplatWeight: the splat pixel truncated from (x - origin) / pixelSpacing (Terrain::GetLayerValueAt);
	///            the base packed weights of the layers the layer match function gives, summed and clamped to 1.
	///          - WaterLevel: the water quad under (x, z) on the quad grid (vertexSpacing x verticesPerTile /
	///            quadsPerTile, as Apply writes it); its base type (0 = no water) and the bilinear base water heights of
	///            its four vertices (Terrain::GetWaterHeightAtWorldPos).
	///          Tile materials are not undone (the layer match uses the live tile layers); HasMaterialChanges tells.
	///          Main thread only when the live accessors are.
	class BaseTerrainView final : public ITerrainSampler
	{
	public:
		/// @brief Creates the view.
		/// @param live The live sampler. Must outlive the view.
		/// @param access Live grid reads (outer heights, splat, water) in the contribution's indices. Must outlive the view.
		/// @param contribution The contribution to undo; its entries are copied.
		/// @param layers Layer matching of the live tiles (SplatWeight of recorded pixels reads 0 without it).
		/// @param vertexStepsPerPage Outer vertex steps per terrain page (page-local index 0 has no left / up neighbour
		///        in the normal).
		BaseTerrainView(const ITerrainSampler& live, const ITerrainAccess& access, const TerrainContribution& contribution,
			LayerMatchFunction layers, int32 vertexStepsPerPage);

	public:
		/// @copydoc ITerrainSampler::Height
		bool Height(float x, float z, float& h) const override;

		/// @copydoc ITerrainSampler::Normal
		Vector3 Normal(float x, float z) const override;

		/// @copydoc ITerrainSampler::SplatWeight
		float SplatWeight(const String& textureKey, float x, float z) const override;

		/// @copydoc ITerrainSampler::WaterLevel
		bool WaterLevel(float x, float z, float& level) const override;

		/// @brief The live revision mixed with HashContribution of the contribution.
		uint64 Revision() const override;

		/// @brief Bilinear (live - base) of the outer vertices around a world position (0 where nothing is recorded).
		float GetHeightChange(float x, float z) const;

		/// @brief True if the contribution changed tile materials (they are not undone).
		bool HasMaterialChanges() const
		{
			return m_materialChanges;
		}

		/// @brief Hash of every entry of a contribution (heights, splat, water, materials).
		static uint64 HashContribution(const TerrainContribution& contribution);

	private:
		template<typename T>
		struct Recorded
		{
			T base;
			T written;
		};

		using Key = uint64;

		static Key Pack(int64 x, int64 z);

		/// @brief Base height of an outer vertex (Unapply rule), live if not recorded.
		float BaseOuterHeight(int64 vx, int64 vz) const;

		/// @brief live - base of an outer vertex, 0 if not recorded.
		float OuterChange(int64 vx, int64 vz) const;

		/// @brief Base water height of a water vertex (Unapply rule), live if not recorded.
		float BaseWaterHeight(int64 vx, int64 vz) const;

		/// @brief Normal of an outer vertex from base heights, as the page with the given first vertex computes it.
		Vector3 BaseVertexNormal(int64 vx, int64 vz, int64 pageStartX, int64 pageStartZ) const;

		/// @brief Cell of the outer vertex grid under a world position and the fractions inside it.
		bool Cell(float x, float z, int64& vx, int64& vz, float& fx, float& fz) const;

		/// @brief Cell of a grid with the given spacing (from the layout origin) under a world position and the fractions
		///        inside it. False for a spacing that is not positive or a position far outside any grid.
		bool GridCell(float x, float z, double spacing, int64& vx, int64& vz, float& fx, float& fz) const;

	private:
		const ITerrainSampler& m_live;
		const ITerrainAccess& m_access;
		LayerMatchFunction m_layers;
		GridLayout m_layout;
		int32 m_stepsPerPage;

		std::unordered_map<Key, Recorded<float>> m_outer;
		std::unordered_map<Key, Recorded<float>> m_waterHeights;
		std::unordered_map<Key, Recorded<uint32>> m_splat;
		std::unordered_map<Key, Recorded<uint8>> m_waterQuads;
		bool m_materialChanges = false;
		uint64 m_contributionHash = 0;
	};
}
