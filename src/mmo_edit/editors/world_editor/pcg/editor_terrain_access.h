// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "pcg/terrain_access.h"

#include <set>
#include <utility>

namespace mmo
{
	namespace terrain
	{
		class Terrain;
		class Page;
	}

	/// @brief Implements the PCG global-index terrain access over the editor's terrain::Terrain.
	/// @details Global index conventions (all derived from terrain::constants and the terrain's page
	///          count, with page (0,0) at world -width/2 * PageSize, i.e. -32 pages on a 64x64 map):
	///          - outer vertex v = page * 128 + local (129 per page, seams shared),
	///          - inner vertex i = page * 128 + local (128 per page, world position at +0.5),
	///          - splat pixel p = page * 1008 + local (1009 per page, seams shared),
	///          - water quad q = tile * 8 + local quad, tile = page * 16 + local tile; quad q spans
	///            water vertices q..q+1, which use the outer vertex indexing,
	///          - tile t = page * 16 + local tile.
	///          Writes to pages that are not prepared are dropped (the caller loads them first).
	class EditorTerrainAccess final : public pcg::ITerrainAccess, public NonCopyable
	{
	public:
		/// @brief Creates the adapter.
		/// @param terrain The terrain to read and write. Must outlive the adapter.
		explicit EditorTerrainAccess(terrain::Terrain& terrain);

	public:
		/// @copydoc pcg::ITerrainAccess::GetLayout
		const pcg::GridLayout& GetLayout() const override { return m_layout; }

		/// @brief Outer vertex height (Terrain::GetAt). 0 outside the terrain.
		float GetOuterHeight(int32 vx, int32 vz) const override;

		/// @brief Sets an outer vertex height through Terrain::SetHeightAt (mirrors page seams).
		void SetOuterHeight(int32 vx, int32 vz, float h) override;

		/// @brief Inner vertex height (Terrain::GetInnerHeightAt). 0 outside the terrain.
		float GetInnerHeight(int32 ix, int32 iz) const override;

		/// @brief Sets an inner vertex height through Terrain::SetInnerHeightAt.
		void SetInnerHeight(int32 ix, int32 iz, float h) override;

		/// @brief Packed splat weights of a pixel (Terrain::GetLayersAt).
		uint32 GetSplat(int32 px, int32 pz) const override;

		/// @brief Sets packed splat weights through Terrain::SetLayersAt (mirrors page seams).
		void SetSplat(int32 px, int32 pz, uint32 packed) override;

		/// @brief Water type of a quad: the tile's water type if the quad bit is set, else 0.
		uint8 GetWaterQuad(int32 qx, int32 qz) const override;

		/// @brief Sets or clears a water quad. A non-zero type also becomes the tile's water type
		///        (types are stored per tile); clearing the last quad of a tile resets its type.
		void SetWaterQuad(int32 qx, int32 qz, uint8 waterType) override;

		/// @brief Water surface height at a water vertex (outer vertex indexing).
		float GetWaterHeight(int32 vx, int32 vz) const override;

		/// @brief Sets a water vertex height in every page holding a copy of the vertex.
		void SetWaterHeight(int32 vx, int32 vz, float h) override;

		/// @brief Name of the tile's material, or an empty string for the terrain default material.
		String GetTileMaterial(int32 tx, int32 tz) const override;

		/// @brief Sets the tile's material the way the tile details panel does (Tile::SetMaterial).
		///        An empty name restores the terrain default material. Needs the page loaded.
		void SetTileMaterial(int32 tx, int32 tz, const String& material) override;

		/// @brief Recomputes outer and inner normals, rebuilds tile geometry, coverage textures and
		///        water meshes for the rect plus a one-vertex border (clamped to the terrain), and
		///        rebuilds the water mesh of every page whose water changed since the last call. While GPU
		///        updates are deferred only the normals are recomputed; the rest waits for FlushGpuUpdates.
		void FinishEdits(const pcg::IntRect& outerVertexRect) override;

	public:
		/// @brief Sets the water material given to a page that receives water but has none yet.
		/// @param materialName The material asset name. Empty disables the assignment.
		void SetDefaultWaterMaterial(const String& materialName) { m_defaultWaterMaterial = materialName; }

		/// @brief Defers what FinishEdits does for the renderer (tile geometry, coverage textures, water meshes) until
		///        FlushGpuUpdates: the terrain data (heights, normals, splat, water) changes at once while the world keeps
		///        showing what it showed before.
		void SetDeferGpuUpdates(bool defer) { m_deferGpu = defer; }

		/// @brief Does the deferred renderer updates of every FinishEdits since the last flush (nothing if none).
		void FlushGpuUpdates();

		/// @brief Pages (x, z) any setter wrote to since the adapter was created.
		[[nodiscard]] const std::set<std::pair<uint32, uint32>>& GetTouchedPages() const { return m_touchedPages; }

	private:
		/// @brief Returns the page at the given page coordinates if it exists and is prepared.
		terrain::Page* GetPreparedPage(int32 pageX, int32 pageZ) const;

		/// @brief Lists every (page, local index) pair holding a copy of a seam-shared index along one
		///        axis (outer vertices, water vertices, splat pixels). The first pair is the page that
		///        owns the index; a second pair exists when the index lies on a page seam.
		/// @param index Global index along the axis (must be in range).
		/// @param stepsPerPage Index steps per page (128 for vertices, 1008 for pixels).
		/// @param pageCount Number of pages along the axis.
		/// @param out Receives up to two (page, local) pairs.
		/// @return The number of pairs written.
		static uint32 SharedIndexCopies(int32 index, int32 stepsPerPage, int32 pageCount, std::pair<int32, int32> (&out)[2]);

		/// @brief Records a page as touched.
		void Touch(int32 pageX, int32 pageZ);

		/// @brief The renderer part of FinishEdits for a clamped outer vertex rect.
		void UpdateGpu(const pcg::IntRect& r);

		/// @brief Rebuilds the water meshes of the pages whose water changed, and forgets them.
		void RebuildDirtyWaterMeshes();

	private:
		terrain::Terrain& m_terrain;
		pcg::GridLayout m_layout;
		int32 m_pagesX = 0;
		int32 m_pagesZ = 0;
		String m_defaultWaterMaterial;
		std::set<std::pair<uint32, uint32>> m_touchedPages;
		/// Pages whose water mask, type or heights changed since the last FinishEdits (or flush, while deferring).
		std::set<std::pair<uint32, uint32>> m_waterDirtyPages;
		/// Whether FinishEdits leaves the renderer updates to FlushGpuUpdates.
		bool m_deferGpu = false;
		/// Union of the clamped rects of the deferred renderer updates (valid while m_gpuPending).
		pcg::IntRect m_gpuRect{};
		bool m_gpuPending = false;
	};
}
