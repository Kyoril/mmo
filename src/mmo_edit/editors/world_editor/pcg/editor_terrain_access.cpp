// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_terrain_access.h"

#include "log/default_log_levels.h"
#include "math/vector3.h"
#include "scene_graph/material_manager.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/terrain.h"
#include "terrain/tile.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Outer (and water) vertex steps per page side.
		constexpr int32 VertexStepsPerPage = static_cast<int32>(terrain::constants::OuterVerticesPerPageSide - 1);
		/// Outer vertex steps per tile side; also the water quads per tile side.
		constexpr int32 VertexStepsPerTile = static_cast<int32>(terrain::constants::OuterVerticesPerTileSide - 1);
		/// Splat pixel steps per page side.
		constexpr int32 PixelStepsPerPage = static_cast<int32>(terrain::constants::PixelsPerPage - 1);
		constexpr int32 TilesPerPage = static_cast<int32>(terrain::constants::TilesPerPage);
		/// Water quads per page side (one per outer vertex step).
		constexpr int32 QuadsPerPage = VertexStepsPerPage;

		/// Whether any water quad of the page is set.
		bool HasWaterQuads(const terrain::Page& page)
		{
			for (uint32 z = 0; z < terrain::constants::TilesPerPage; ++z)
			{
				for (uint32 x = 0; x < terrain::constants::TilesPerPage; ++x)
				{
					if (page.GetWaterQuadMask(x, z) != 0)
					{
						return true;
					}
				}
			}

			return false;
		}

		/// Splits a global tile index into page and page-local tile.
		void SplitTile(const int32 tile, int32& page, int32& local)
		{
			page = tile / TilesPerPage;
			local = tile % TilesPerPage;
		}
	}

	EditorTerrainAccess::EditorTerrainAccess(terrain::Terrain& terrain)
		: m_terrain(terrain)
		, m_pagesX(static_cast<int32>(terrain.GetWidth()))
		, m_pagesZ(static_cast<int32>(terrain.GetHeight()))
	{
		// Same origin as Terrain::GetGlobalVertexWorldPosition / GetGlobalPixelWorldPosition:
		// global index 0 sits at -pages/2 * PageSize (page 32 starts at world 0 on a 64 page map).
		m_layout.originX = -static_cast<double>(m_pagesX) / 2.0 * terrain::constants::PageSize;
		m_layout.originZ = -static_cast<double>(m_pagesZ) / 2.0 * terrain::constants::PageSize;
		m_layout.vertexSpacing = terrain::constants::PageSize / static_cast<double>(VertexStepsPerPage);
		m_layout.pixelSpacing = terrain::constants::PageSize / static_cast<double>(PixelStepsPerPage);
		m_layout.quadsPerTile = static_cast<uint32>(VertexStepsPerTile);
		m_layout.verticesPerTile = static_cast<uint32>(VertexStepsPerTile);
	}

	terrain::Page* EditorTerrainAccess::GetPreparedPage(const int32 pageX, const int32 pageZ) const
	{
		if (pageX < 0 || pageZ < 0 || pageX >= m_pagesX || pageZ >= m_pagesZ)
		{
			return nullptr;
		}

		terrain::Page* page = m_terrain.GetPage(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
		if (!page || !page->IsPrepared())
		{
			return nullptr;
		}

		return page;
	}

	uint32 EditorTerrainAccess::SharedIndexCopies(const int32 index, const int32 stepsPerPage, const int32 pageCount, std::pair<int32, int32> (&out)[2])
	{
		int32 page = index / stepsPerPage;
		int32 local = index - page * stepsPerPage;
		if (page >= pageCount)
		{
			// The very last index only exists as the last row/column of the last page.
			page = pageCount - 1;
			local = stepsPerPage;
		}

		out[0] = { page, local };
		if (local == 0 && page > 0)
		{
			out[1] = { page - 1, stepsPerPage };
			return 2;
		}

		return 1;
	}

	void EditorTerrainAccess::Touch(const int32 pageX, const int32 pageZ)
	{
		if (pageX >= 0 && pageZ >= 0 && pageX < m_pagesX && pageZ < m_pagesZ)
		{
			m_touchedPages.emplace(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
		}
	}

	float EditorTerrainAccess::GetOuterHeight(const int32 vx, const int32 vz) const
	{
		if (vx < 0 || vz < 0 || vx > m_pagesX * VertexStepsPerPage || vz > m_pagesZ * VertexStepsPerPage)
		{
			return 0.0f;
		}

		return m_terrain.GetAt(static_cast<uint32>(vx), static_cast<uint32>(vz));
	}

	void EditorTerrainAccess::SetOuterHeight(const int32 vx, const int32 vz, const float h)
	{
		if (vx < 0 || vz < 0 || vx > m_pagesX * VertexStepsPerPage || vz > m_pagesZ * VertexStepsPerPage)
		{
			return;
		}

		// SetHeightAt writes every page copy of a seam vertex.
		m_terrain.SetHeightAt(vx, vz, h);

		std::pair<int32, int32> xs[2], zs[2];
		const uint32 nx = SharedIndexCopies(vx, VertexStepsPerPage, m_pagesX, xs);
		const uint32 nz = SharedIndexCopies(vz, VertexStepsPerPage, m_pagesZ, zs);
		for (uint32 i = 0; i < nx; ++i)
		{
			for (uint32 j = 0; j < nz; ++j)
			{
				Touch(xs[i].first, zs[j].first);
			}
		}
	}

	float EditorTerrainAccess::GetInnerHeight(const int32 ix, const int32 iz) const
	{
		return m_terrain.GetInnerHeightAt(ix, iz);
	}

	void EditorTerrainAccess::SetInnerHeight(const int32 ix, const int32 iz, const float h)
	{
		if (ix < 0 || iz < 0 || ix >= m_pagesX * VertexStepsPerPage || iz >= m_pagesZ * VertexStepsPerPage)
		{
			return;
		}

		// Inner vertices are owned by exactly one page.
		m_terrain.SetInnerHeightAt(ix, iz, h);
		Touch(ix / VertexStepsPerPage, iz / VertexStepsPerPage);
	}

	uint32 EditorTerrainAccess::GetSplat(const int32 px, const int32 pz) const
	{
		if (px < 0 || pz < 0)
		{
			return 0;
		}

		return m_terrain.GetLayersAt(static_cast<uint32>(px), static_cast<uint32>(pz));
	}

	void EditorTerrainAccess::SetSplat(const int32 px, const int32 pz, const uint32 packed)
	{
		if (px < 0 || pz < 0 || px > m_pagesX * PixelStepsPerPage || pz > m_pagesZ * PixelStepsPerPage)
		{
			return;
		}

		m_terrain.SetLayersAt(static_cast<uint32>(px), static_cast<uint32>(pz), packed);

		std::pair<int32, int32> xs[2], zs[2];
		const uint32 nx = SharedIndexCopies(px, PixelStepsPerPage, m_pagesX, xs);
		const uint32 nz = SharedIndexCopies(pz, PixelStepsPerPage, m_pagesZ, zs);
		for (uint32 i = 0; i < nx; ++i)
		{
			for (uint32 j = 0; j < nz; ++j)
			{
				Touch(xs[i].first, zs[j].first);
			}
		}
	}

	uint8 EditorTerrainAccess::GetWaterQuad(const int32 qx, const int32 qz) const
	{
		if (qx < 0 || qz < 0 || qx >= m_pagesX * QuadsPerPage || qz >= m_pagesZ * QuadsPerPage)
		{
			return 0;
		}

		int32 pageX, pageZ, tileX, tileZ;
		SplitTile(qx / VertexStepsPerTile, pageX, tileX);
		SplitTile(qz / VertexStepsPerTile, pageZ, tileZ);
		const int32 localQx = qx % VertexStepsPerTile;
		const int32 localQz = qz % VertexStepsPerTile;

		const terrain::Page* page = GetPreparedPage(pageX, pageZ);
		if (!page)
		{
			return 0;
		}

		const uint64 mask = page->GetWaterQuadMask(static_cast<uint32>(tileX), static_cast<uint32>(tileZ));
		if (((mask >> (localQx + localQz * VertexStepsPerTile)) & 1ULL) == 0)
		{
			return 0;
		}

		return static_cast<uint8>(page->GetWaterType(static_cast<uint32>(tileX), static_cast<uint32>(tileZ)));
	}

	void EditorTerrainAccess::SetWaterQuad(const int32 qx, const int32 qz, const uint8 waterType)
	{
		if (qx < 0 || qz < 0 || qx >= m_pagesX * QuadsPerPage || qz >= m_pagesZ * QuadsPerPage)
		{
			return;
		}

		if (waterType >= static_cast<uint8>(terrain::WaterType::Count_))
		{
			WLOG("PCG: ignoring unknown water type " << static_cast<uint32>(waterType));
			return;
		}

		int32 pageX, pageZ, tileX, tileZ;
		SplitTile(qx / VertexStepsPerTile, pageX, tileX);
		SplitTile(qz / VertexStepsPerTile, pageZ, tileZ);
		const uint32 localQx = static_cast<uint32>(qx % VertexStepsPerTile);
		const uint32 localQz = static_cast<uint32>(qz % VertexStepsPerTile);
		const uint32 lx = static_cast<uint32>(tileX);
		const uint32 lz = static_cast<uint32>(tileZ);

		terrain::Page* page = GetPreparedPage(pageX, pageZ);
		if (!page)
		{
			return;
		}

		const auto type = static_cast<terrain::WaterType>(waterType);
		if (type != terrain::WaterType::None)
		{
			page->SetWaterQuadBit(lx, lz, localQx, localQz, true);

			// Water types are stored per tile, so the quad's type becomes the tile's type.
			if (page->GetWaterType(lx, lz) != type)
			{
				page->SetWaterType(lx, lz, type);
			}

			// Only a page without any water material gets one; an authored choice is kept.
			if (page->GetWaterMaterialName().empty() && !m_defaultWaterMaterial.empty())
			{
				page->SetWaterMaterialName(m_defaultWaterMaterial);
			}
		}
		else
		{
			page->SetWaterQuadBit(lx, lz, localQx, localQz, false);

			// A tile without any water quad left has no water type either (as ClearWater leaves it).
			if (page->GetWaterQuadMask(lx, lz) == 0 && page->GetWaterType(lx, lz) != terrain::WaterType::None)
			{
				page->SetWaterType(lx, lz, terrain::WaterType::None);
			}

			// A page without any water quad left needs no water material either. Clearing it undoes
			// the material a build gave a dry page, so Delete restores the page's saved bytes.
			if (!page->GetWaterMaterialName().empty() && page->GetWaterQuadMask(lx, lz) == 0 && !HasWaterQuads(*page))
			{
				page->SetWaterMaterialName(String());
			}
		}

		Touch(pageX, pageZ);
		m_waterDirtyPages.emplace(static_cast<uint32>(pageX), static_cast<uint32>(pageZ));
	}

	float EditorTerrainAccess::GetWaterHeight(const int32 vx, const int32 vz) const
	{
		if (vx < 0 || vz < 0 || vx > m_pagesX * VertexStepsPerPage || vz > m_pagesZ * VertexStepsPerPage)
		{
			return 0.0f;
		}

		std::pair<int32, int32> xs[2], zs[2];
		SharedIndexCopies(vx, VertexStepsPerPage, m_pagesX, xs);
		SharedIndexCopies(vz, VertexStepsPerPage, m_pagesZ, zs);

		const terrain::Page* page = GetPreparedPage(xs[0].first, zs[0].first);
		if (!page)
		{
			return 0.0f;
		}

		return page->GetWaterVertexHeight(static_cast<uint32>(xs[0].second), static_cast<uint32>(zs[0].second));
	}

	void EditorTerrainAccess::SetWaterHeight(const int32 vx, const int32 vz, const float h)
	{
		if (vx < 0 || vz < 0 || vx > m_pagesX * VertexStepsPerPage || vz > m_pagesZ * VertexStepsPerPage)
		{
			return;
		}

		// Water heights are stored per page (129 x 129), so a vertex on a page seam has a copy in
		// every page sharing it, like terrain heights. Write all of them.
		std::pair<int32, int32> xs[2], zs[2];
		const uint32 nx = SharedIndexCopies(vx, VertexStepsPerPage, m_pagesX, xs);
		const uint32 nz = SharedIndexCopies(vz, VertexStepsPerPage, m_pagesZ, zs);
		for (uint32 i = 0; i < nx; ++i)
		{
			for (uint32 j = 0; j < nz; ++j)
			{
				terrain::Page* page = GetPreparedPage(xs[i].first, zs[j].first);
				if (!page)
				{
					continue;
				}

				page->SetWaterVertexHeight(static_cast<uint32>(xs[i].second), static_cast<uint32>(zs[j].second), h);
				Touch(xs[i].first, zs[j].first);
				m_waterDirtyPages.emplace(static_cast<uint32>(xs[i].first), static_cast<uint32>(zs[j].first));
			}
		}
	}

	String EditorTerrainAccess::GetTileMaterial(const int32 tx, const int32 tz) const
	{
		if (tx < 0 || tz < 0 || tx >= m_pagesX * TilesPerPage || tz >= m_pagesZ * TilesPerPage)
		{
			return String();
		}

		int32 pageX, pageZ, localX, localZ;
		SplitTile(tx, pageX, localX);
		SplitTile(tz, pageZ, localZ);

		const terrain::Page* page = GetPreparedPage(pageX, pageZ);
		if (!page)
		{
			return String();
		}

		// Page::GetTileMaterial falls back to the default material; the default reads as "".
		const MaterialPtr material = page->GetTileMaterial(static_cast<uint32>(localX), static_cast<uint32>(localZ));
		if (!material || material == m_terrain.GetDefaultMaterial())
		{
			return String();
		}

		return String(material->GetName());
	}

	void EditorTerrainAccess::SetTileMaterial(const int32 tx, const int32 tz, const String& material)
	{
		if (tx < 0 || tz < 0 || tx >= m_pagesX * TilesPerPage || tz >= m_pagesZ * TilesPerPage)
		{
			return;
		}

		// The tile object only exists while its page is loaded; the material table of the page is
		// updated from it (Tile::SetMaterial -> Page::NotifyTileMaterialChanged), exactly like the
		// tile details panel does it.
		terrain::Tile* tile = m_terrain.GetTile(tx, tz);
		if (!tile)
		{
			WLOG("PCG: tile " << tx << "x" << tz << " is not loaded, its material was not changed");
			return;
		}

		const MaterialPtr target = material.empty() ? m_terrain.GetDefaultMaterial() : MaterialManager::Get().Load(material);
		if (!target)
		{
			WLOG("PCG: failed to load terrain material " << material);
			return;
		}

		tile->SetMaterial(target);
		Touch(tx / TilesPerPage, tz / TilesPerPage);
	}

	void EditorTerrainAccess::FinishEdits(const pcg::IntRect& outerVertexRect)
	{
		const int32 maxVx = m_pagesX * VertexStepsPerPage;
		const int32 maxVz = m_pagesZ * VertexStepsPerPage;

		// The touched rect plus one vertex for the normals of the neighbours, clamped to the terrain.
		pcg::IntRect r;
		r.minX = std::max(0, outerVertexRect.minX - 1);
		r.minZ = std::max(0, outerVertexRect.minZ - 1);
		r.maxX = std::min(maxVx, outerVertexRect.maxX + 1);
		r.maxZ = std::min(maxVz, outerVertexRect.maxZ + 1);

		if (r.minX <= r.maxX && r.minZ <= r.maxZ)
		{
			// Outer normals of every page copy. Page::SetHeightAt only refreshes its own page, so a
			// seam vertex's copy in the neighbour page would otherwise keep a stale normal.
			for (int32 vz = r.minZ; vz <= r.maxZ; ++vz)
			{
				std::pair<int32, int32> zs[2];
				const uint32 nz = SharedIndexCopies(vz, VertexStepsPerPage, m_pagesZ, zs);
				for (int32 vx = r.minX; vx <= r.maxX; ++vx)
				{
					std::pair<int32, int32> xs[2];
					const uint32 nx = SharedIndexCopies(vx, VertexStepsPerPage, m_pagesX, xs);
					for (uint32 i = 0; i < nx; ++i)
					{
						for (uint32 j = 0; j < nz; ++j)
						{
							if (terrain::Page* page = GetPreparedPage(xs[i].first, zs[j].first))
							{
								page->CalculateNormalAt(static_cast<uint32>(xs[i].second), static_cast<uint32>(zs[j].second));
							}
						}
					}
				}
			}

			// Inner normals are the average of the four outer normals around them (as
			// Page::SetInnerHeightAt computes them). Inner heights are NOT re-derived from the
			// corners (Terrain::UpdateInnerVertices would overwrite the inner heights the
			// contribution wrote and break its compare-and-restore). Setting an inner normal also
			// marks the page changed, which outer height writes alone do not do.
			const int32 innerMaxX = std::min(r.maxX, maxVx - 1);
			const int32 innerMaxZ = std::min(r.maxZ, maxVz - 1);
			for (int32 iz = r.minZ; iz <= innerMaxZ; ++iz)
			{
				for (int32 ix = r.minX; ix <= innerMaxX; ++ix)
				{
					terrain::Page* page = GetPreparedPage(ix / VertexStepsPerPage, iz / VertexStepsPerPage);
					if (!page)
					{
						continue;
					}

					const uint32 lx = static_cast<uint32>(ix % VertexStepsPerPage);
					const uint32 lz = static_cast<uint32>(iz % VertexStepsPerPage);
					const Vector3 sum = page->GetNormalAt(lx, lz) + page->GetNormalAt(lx + 1, lz) +
						page->GetNormalAt(lx, lz + 1) + page->GetNormalAt(lx + 1, lz + 1);
					page->SetInnerNormalAt(lx, lz, (sum * 0.25f).NormalizedCopy());
				}
			}

			if (m_deferGpu)
			{
				if (!m_gpuPending)
				{
					m_gpuRect = r;
					m_gpuPending = true;
				}
				else
				{
					m_gpuRect.minX = std::min(m_gpuRect.minX, r.minX);
					m_gpuRect.minZ = std::min(m_gpuRect.minZ, r.minZ);
					m_gpuRect.maxX = std::max(m_gpuRect.maxX, r.maxX);
					m_gpuRect.maxZ = std::max(m_gpuRect.maxZ, r.maxZ);
				}
			}
			else
			{
				UpdateGpu(r);
			}
		}

		if (!m_deferGpu)
		{
			RebuildDirtyWaterMeshes();
		}
	}

	void EditorTerrainAccess::FlushGpuUpdates()
	{
		if (m_gpuPending)
		{
			m_gpuPending = false;
			UpdateGpu(m_gpuRect);
		}
		RebuildDirtyWaterMeshes();
	}

	void EditorTerrainAccess::UpdateGpu(const pcg::IntRect& r)
	{
		// Tile vertex buffers, render batches, page bounds and (for those pages) water meshes.
		m_terrain.UpdateTiles(r.minX, r.minZ, r.maxX, r.maxZ);

		// Splat coverage textures over the same area in pixel space.
		const double pixelsPerVertex = static_cast<double>(PixelStepsPerPage) / static_cast<double>(VertexStepsPerPage);
		const int32 pMinX = std::max(0, static_cast<int32>(std::floor(r.minX * pixelsPerVertex)));
		const int32 pMinZ = std::max(0, static_cast<int32>(std::floor(r.minZ * pixelsPerVertex)));
		const int32 pMaxX = std::min(m_pagesX * PixelStepsPerPage, static_cast<int32>(std::ceil(r.maxX * pixelsPerVertex)));
		const int32 pMaxZ = std::min(m_pagesZ * PixelStepsPerPage, static_cast<int32>(std::ceil(r.maxZ * pixelsPerVertex)));
		// Same per-page split as Terrain::UpdateTileCoverage (private), via the public page call.
		for (int32 pageZ = pMinZ / PixelStepsPerPage; pageZ <= std::min(m_pagesZ - 1, pMaxZ / PixelStepsPerPage); ++pageZ)
		{
			for (int32 pageX = pMinX / PixelStepsPerPage; pageX <= std::min(m_pagesX - 1, pMaxX / PixelStepsPerPage); ++pageX)
			{
				terrain::Page* page = GetPreparedPage(pageX, pageZ);
				if (!page || !page->IsLoaded())
				{
					continue;
				}

				page->UpdateTileCoverage(
					std::max(pMinX - pageX * PixelStepsPerPage, 0),
					std::max(pMinZ - pageZ * PixelStepsPerPage, 0),
					pMaxX - pageX * PixelStepsPerPage,
					pMaxZ - pageZ * PixelStepsPerPage);
			}
		}
	}

	void EditorTerrainAccess::RebuildDirtyWaterMeshes()
	{
		// Water-only changes (water heights are not part of the touched rect) still need their
		// meshes rebuilt. RebuildWaterMesh does nothing for a page without a water render object.
		for (const auto& pagePos : m_waterDirtyPages)
		{
			terrain::Page* page = GetPreparedPage(static_cast<int32>(pagePos.first), static_cast<int32>(pagePos.second));
			if (page && page->IsLoaded())
			{
				page->RebuildWaterMesh();
			}
		}
		m_waterDirtyPages.clear();
	}
}
