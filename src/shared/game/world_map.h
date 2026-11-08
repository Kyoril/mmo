// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cmath>
#include <optional>
#include <vector>

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	/// @brief Data and math of the world map: one continuous, zoomable view of a whole map.
	/// @details The editor bakes, per map, a pyramid of map art tiles from the minimap tiles and a grid of
	///	         the zone painted on every terrain tile (WorldMapInfo). The client draws the art level that
	///	         matches the zoom and crossfades into the minimap tiles when zoomed in closely. Coordinates
	///	         are world X (left to right) and world Z (top to bottom), the minimap's orientation.
	namespace world_map
	{
		/// @brief Pages per map side (terrain::constants::MaxPages).
		constexpr uint32 MaxPages = 64;

		/// @brief Terrain tiles (zone cells) per page side (terrain::constants::TilesPerPage).
		constexpr uint32 CellsPerPage = 16;

		/// @brief Edge length of a terrain page in world units (terrain::constants::PageSize).
		constexpr double PageSize = static_cast<double>(33.33333f) * CellsPerPage;

		/// @brief Edge length of a zone cell (terrain tile) in world units.
		constexpr double CellSize = PageSize / CellsPerPage;

		/// @brief Edge length of a map art tile in pixels.
		constexpr uint32 ArtTileSize = 256;

		/// @brief The finest art level. Level L tiles cover 2^L x 2^L pages.
		constexpr uint32 MinArtLevel = 1;

		/// @brief The coarsest art level: one tile covers the whole page grid.
		constexpr uint32 MaxArtLevel = 6;

		/// @brief Edge length of a minimap tile in pixels (one page each).
		constexpr uint32 MinimapTileSize = 512;

		/// @brief Version of the .wmap metadata file.
		constexpr uint32 FormatVersion = 1;

		/// @brief World units per pixel of an art tile at the given level.
		inline double ArtUnitsPerPixel(const uint32 level)
		{
			return PageSize * static_cast<double>(1u << level) / ArtTileSize;
		}

		/// @brief World units per pixel of a minimap tile shown at its full resolution.
		inline double MinimapUnitsPerPixel()
		{
			return PageSize / MinimapTileSize;
		}

		/// @brief Picks the art level to draw at a zoom: the coarsest level that is still at least as
		///        sharp as the screen, limited to [MinArtLevel, maxLevel].
		/// @param unitsPerPixel World units per screen pixel of the view.
		/// @param maxLevel The map's coarsest baked level.
		uint32 SelectArtLevel(double unitsPerPixel, uint32 maxLevel);

		/// @brief Opacity of the minimap tiles drawn over the map art: 0 while zoomed out, rising to 1
		///        as the view gets close enough for the minimap's detail to show.
		float MinimapBlend(double unitsPerPixel);

		/// @brief Smooth 0..1 ramp between two edges (which may be given in descending order).
		float SmoothStep(float edge0, float edge1, float x);

		/// @brief File name of a map's metadata (.wmap).
		String GetInfoFilename(const String& mapDirectory);

		/// @brief File name of a map art tile at a level and tile coordinate.
		String GetArtTileFilename(const String& mapDirectory, uint32 level, uint32 x, uint32 z);

		/// @brief File name of the minimap tile of a page (the client's Minimap uses the same files).
		String GetMinimapTileFilename(const String& mapDirectory, uint32 pageX, uint32 pageZ);

		/// @brief World coordinate of the north-west corner of page (or cell) index 0.
		inline double GridOrigin()
		{
			return -static_cast<double>(MaxPages / 2) * PageSize;
		}

		/// @brief A rectangle in world X/Z.
		struct WorldRect
		{
			double minX = 0.0;
			double minZ = 0.0;
			double maxX = 0.0;
			double maxZ = 0.0;

			[[nodiscard]] double Width() const { return maxX - minX; }
			[[nodiscard]] double Height() const { return maxZ - minZ; }
			[[nodiscard]] double CenterX() const { return (minX + maxX) * 0.5; }
			[[nodiscard]] double CenterZ() const { return (minZ + maxZ) * 0.5; }
			[[nodiscard]] bool Contains(const double x, const double z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
		};

		/// @brief A zone as it appears on the map: its extent and where its name is written.
		struct ZoneInfo
		{
			uint32 zoneId = 0;

			/// @brief Bounding rectangle of the zone's cells.
			WorldRect bounds;

			/// @brief Label position: the zone cell closest to the centroid of all its cells, so the
			///        label sits inside the zone even when it is not convex.
			double labelX = 0.0;
			double labelZ = 0.0;

			/// @brief Number of cells painted with the zone (its area in cells).
			uint32 cellCount = 0;
		};

		/// @brief The baked metadata of a map's world map (Textures/WorldMap/<dir>/WorldMap.wmap).
		class WorldMapInfo
		{
		public:
			/// @brief Inclusive page rectangle that holds any terrain.
			uint32 minPageX = 0;
			uint32 minPageZ = 0;
			uint32 maxPageX = 0;
			uint32 maxPageZ = 0;

			/// @brief The coarsest art level that was baked.
			uint32 maxArtLevel = MaxArtLevel;

			/// @brief Zones present on the map, largest first.
			std::vector<ZoneInfo> zones;

			/// @brief One bit per page of the 64 x 64 grid: whether the page has terrain (and a minimap tile).
			std::vector<bool> pages = std::vector<bool>(MaxPages * MaxPages, false);

		public:
			/// @brief Number of zone cells per side of the cell grid (covering the page rectangle).
			[[nodiscard]] uint32 GetCellsX() const { return (maxPageX - minPageX + 1) * CellsPerPage; }
			[[nodiscard]] uint32 GetCellsZ() const { return (maxPageZ - minPageZ + 1) * CellsPerPage; }

			/// @brief Resizes the cell grid to the page rectangle and clears it to zone 0.
			void ResetCells();

			/// @brief Sets the zone of a cell (global cell coordinates of the 1024 x 1024 grid).
			void SetCell(uint32 globalCellX, uint32 globalCellZ, uint32 zoneId);

			/// @brief Gets the zone of a cell (global cell coordinates), 0 outside the rectangle.
			[[nodiscard]] uint32 GetCell(int64 globalCellX, int64 globalCellZ) const;

			/// @brief Gets the zone at a world position, 0 if none.
			[[nodiscard]] uint32 GetZoneAt(double x, double z) const;

			/// @brief Whether a page has terrain.
			[[nodiscard]] bool HasPage(uint32 pageX, uint32 pageZ) const;

			/// @brief The world rectangle covered by the page rectangle.
			[[nodiscard]] WorldRect GetBounds() const;

			/// @brief Finds a zone's info, or nullptr.
			[[nodiscard]] const ZoneInfo* FindZone(uint32 zoneId) const;

			/// @brief Recomputes zones (bounds, label positions, cell counts) from the cell grid.
			void RebuildZones();

			/// @brief Replaces every cell's zone by @p remap(zone), e.g. to merge sub zones into their parents.
			template <typename Remap>
			void RemapCells(const Remap& remap)
			{
				for (uint32& zoneId : m_cells)
				{
					zoneId = remap(zoneId);
				}
			}

			/// @brief Whether art level @p level has a tile at (x, z): any page of its square has terrain.
			[[nodiscard]] bool HasArtTile(uint32 level, uint32 x, uint32 z) const;

			/// @brief Access to the raw cell grid (row by row, GetCellsX() per row).
			[[nodiscard]] const std::vector<uint32>& GetCells() const { return m_cells; }

		private:
			std::vector<uint32> m_cells;

			friend io::Writer& operator<<(io::Writer& writer, const WorldMapInfo& info);
			friend io::Reader& operator>>(io::Reader& reader, WorldMapInfo& info);
		};

		io::Writer& operator<<(io::Writer& writer, const WorldMapInfo& info);
		io::Reader& operator>>(io::Reader& reader, WorldMapInfo& info);

		/// @brief What part of the world a view shows and how large.
		/// @details The view is centred on a world point, with a uniform scale in world units per
		///	         screen pixel. Screen coordinates are pixels from the view's top-left corner.
		struct View
		{
			double centerX = 0.0;
			double centerZ = 0.0;
			double unitsPerPixel = 16.0;
			double width = 1.0;
			double height = 1.0;

			[[nodiscard]] double ScreenToWorldX(const double sx) const { return centerX + (sx - width * 0.5) * unitsPerPixel; }
			[[nodiscard]] double ScreenToWorldZ(const double sy) const { return centerZ + (sy - height * 0.5) * unitsPerPixel; }
			[[nodiscard]] double WorldToScreenX(const double x) const { return (x - centerX) / unitsPerPixel + width * 0.5; }
			[[nodiscard]] double WorldToScreenZ(const double z) const { return (z - centerZ) / unitsPerPixel + height * 0.5; }

			/// @brief The world rectangle the view shows.
			[[nodiscard]] WorldRect GetVisibleRect() const;

			/// @brief Changes the zoom so the world point under the screen point stays under it.
			void ZoomAt(double sx, double sy, double newUnitsPerPixel);

			/// @brief The scale at which a world rectangle fills the view (with a margin factor >= 1).
			[[nodiscard]] double FitUnitsPerPixel(const WorldRect& rect, double margin) const;
		};

		/// @brief Keeps a view's zoom and centre usable for a map: zoom between the closest zoom and
		///        the zoom that fits the whole map, and the centre within the map (with some slack).
		void ClampView(View& view, const WorldRect& mapBounds, double minUnitsPerPixel);

		/// @brief The closest zoom the world map allows (world units per screen pixel).
		constexpr double MinUnitsPerPixel = 0.2;

		/// @brief Clamps an offset from a circle's centre to the circle, for markers on a round minimap.
		/// @param dx, dz Offset in world units, changed in place when it is clamped.
		/// @param radius Radius in world units.
		/// @return Whether the offset was outside the circle (and is now on its edge).
		bool ClampToCircle(float& dx, float& dz, float radius);
	}
}
