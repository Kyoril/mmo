// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <functional>
#include <vector>

namespace mmo
{
	/// @brief Bakes a map's world map (Textures/WorldMap/<world>/): the zone cell grid (WorldMap.wmap) and
	///        the pyramid of map art tiles the client's WorldMap draws (see game/world_map.h).
	/// @details Runs on the CPU only and needs no loaded pages: the zones are read from every page's .tile
	///	         file, and the art is assembled from the mip levels of the minimap tiles, so bake the minimaps
	///	         first. Level L art tiles cover 2^L x 2^L pages at 256 pixels; each is graded towards a printed
	///	         map look (softer colour, parchment tint, paper grain) with ink lines along zone borders.
	class WorldMapBaker final : public NonCopyable
	{
	public:
		/// @brief Returns the zone's parent zone id, or 0 if it has none.
		using ParentZoneResolver = std::function<uint32(uint32 zoneId)>;

		/// @brief Returns a page's .tile file name.
		using PageFilenameResolver = std::function<String(uint32 x, uint32 z)>;

		/// @param worldName The world's name: the folder of its minimap tiles and of the world map.
		WorldMapBaker(String worldName, PageFilenameResolver pageFilename, ParentZoneResolver parentZone);

	public:
		/// @brief Bakes everything. Returns false if the world has no terrain or a file could not be written.
		bool Bake();

	public:
		/// @brief Decodes DXT1 (BC1) blocks into RGBA8 (alpha 255, or 0 for transparent texels).
		/// @param blocks size / 4 * size / 4 blocks of 8 bytes.
		/// @param size Edge length of the square image (a multiple of 4).
		static std::vector<uint8> DecodeDxt1(const uint8* blocks, uint32 size);

	private:
		/// @brief The decoded mip levels of one page's minimap tile, index = mip level (empty if missing).
		struct PageImage
		{
			std::vector<std::vector<uint8>> mips;
		};

		bool LoadMinimapMips(uint32 x, uint32 z, PageImage& image) const;

		uint32 GetRootZone(uint32 zoneId) const;

	private:
		String m_worldName;
		PageFilenameResolver m_pageFilename;
		ParentZoneResolver m_parentZone;
	};
}
