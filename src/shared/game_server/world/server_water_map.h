// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace mmo
{
	/// @brief Loads terrain water data for a map server-side and answers point water queries.
	///
	/// The terrain library itself is graphics-coupled and cannot be loaded on the world server, so
	/// this class parses ONLY the water chunk (`QWCM`) out of each terrain page file
	/// (`Worlds/<map>/<map>/Terrain/<x>_<z>.tile`). It stores the per-tile water quad masks and the
	/// per-page water-surface vertex heights, and replicates the terrain's world→page math to provide
	/// authoritative `HasWater` / `GetWaterSurface` checks for swim validation.
	///
	/// Modeled on ServerCollisionMap: pages are loaded once on construction.
	///
	/// On a map with terrain, a page without a page file is open ocean at the sea level, exactly as
	/// the client's terrain treats it (terrain::Page::IsOceanPlaceholder), and so is everything off
	/// the page grid. A map without any page file (an interior, WMO-only map) has no water at all.
	class ServerWaterMap final : public NonCopyable
	{
	public:
		/// @brief Loads all water data for the given map.
		/// @param mapName The map directory name (same as proto::MapEntry::directory()).
		explicit ServerWaterMap(const std::string& mapName);
		~ServerWaterMap() override = default;

		/// @brief Returns true when the map has terrain (and therefore at least its open ocean) or at
		///        least one page with water.
		[[nodiscard]] bool IsLoaded() const { return m_hasTerrain || !m_pages.empty(); }

		/// @brief Returns true if there is a water quad at the given world XZ position.
		[[nodiscard]] bool HasWater(float x, float z) const;

		/// @brief Gets the bilinearly-interpolated water surface height at the given world XZ position.
		/// @param outSurfaceY Receives the surface height when water is present.
		/// @return True when there is water at the position, false otherwise.
		[[nodiscard]] bool GetWaterSurface(float x, float z, float& outSurfaceY) const;

	private:
		/// @brief Water data for a single terrain page.
		struct PageWater
		{
			/// Quad presence masks keyed by local tile index (tx + tz*TilesPerPage). Bit (qx + qz*8)
			/// set means that 8×8 sub-quad has water.
			std::unordered_map<uint16, uint64> quadMasks;
			/// Water surface heights for the page's outer vertex grid (OuterVerticesPerPageSide^2).
			std::vector<float> vertexHeights;
		};

		/// Loads and parses the water chunk of a single page file. Returns true if water was found.
		bool LoadPage(uint32 pageX, uint32 pageZ, const std::string& mapName);

		/// Returns the page record for the given page indices, or nullptr if not loaded.
		[[nodiscard]] const PageWater* GetPage(uint32 pageX, uint32 pageZ) const;

		/// Returns true if a position lies in open ocean: on a terrain map, over a page without a
		/// page file or off the page grid.
		[[nodiscard]] bool IsOpenOcean(int32 pageX, int32 pageZ) const;

	private:
		/// Loaded page water records keyed by (pageX + pageZ * MaxPages).
		std::unordered_map<uint32, PageWater> m_pages;

		/// Whether a page file exists, indexed (pageX + pageZ * MaxPages).
		std::vector<bool> m_pageFileExists;

		/// Whether any page file exists, i.e. whether the map has terrain at all.
		bool m_hasTerrain = false;

		/// Height of the open ocean, from the world file's terrain chunk.
		float m_seaLevel = 0.0f;
	};
}
