// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_map.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <algorithm>
#include <limits>
#include <map>

namespace mmo
{
	namespace world_map
	{
		uint32 SelectArtLevel(const double unitsPerPixel, const uint32 maxLevel)
		{
			const uint32 coarsest = std::clamp(maxLevel, MinArtLevel, MaxArtLevel);

			// A level a little softer than the screen is fine; it saves a quarter of the tiles.
			const double ratio = unitsPerPixel * 1.25 / ArtUnitsPerPixel(0);
			if (ratio <= 1.0)
			{
				return MinArtLevel;
			}

			const int32 level = static_cast<int32>(std::floor(std::log2(ratio)));
			return static_cast<uint32>(std::clamp(level, static_cast<int32>(MinArtLevel), static_cast<int32>(coarsest)));
		}

		float SmoothStep(const float edge0, const float edge1, const float x)
		{
			if (edge0 == edge1)
			{
				return x < edge0 ? 0.0f : 1.0f;
			}

			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		float MinimapBlend(const double unitsPerPixel)
		{
			// The finest art level is native at ~4.2 units per pixel: the minimap takes over before
			// the art gets visibly magnified, and is fully there at about three screen pixels per texel.
			return SmoothStep(3.2f, 1.6f, static_cast<float>(unitsPerPixel));
		}

		String GetInfoFilename(const String& mapDirectory)
		{
			return "Textures/WorldMap/" + mapDirectory + "/WorldMap.wmap";
		}

		String GetArtTileFilename(const String& mapDirectory, const uint32 level, const uint32 x, const uint32 z)
		{
			return "Textures/WorldMap/" + mapDirectory + "/L" + std::to_string(level) + "_" + std::to_string(x) + "_" + std::to_string(z) + ".htex";
		}

		String GetMinimapTileFilename(const String& mapDirectory, const uint32 pageX, const uint32 pageZ)
		{
			// Page index: X in the high byte, Z in the low byte.
			const uint16 pageIndex = static_cast<uint16>(((pageX & 0xff) << 8) | (pageZ & 0xff));
			return "Textures/Minimaps/" + mapDirectory + "/" + std::to_string(pageIndex) + ".htex";
		}

		void WorldMapInfo::ResetCells()
		{
			m_cells.assign(static_cast<size_t>(GetCellsX()) * GetCellsZ(), 0);
		}

		void WorldMapInfo::SetCell(const uint32 globalCellX, const uint32 globalCellZ, const uint32 zoneId)
		{
			const int64 x = static_cast<int64>(globalCellX) - static_cast<int64>(minPageX) * CellsPerPage;
			const int64 z = static_cast<int64>(globalCellZ) - static_cast<int64>(minPageZ) * CellsPerPage;
			if (x < 0 || z < 0 || x >= GetCellsX() || z >= GetCellsZ() || m_cells.empty())
			{
				return;
			}

			m_cells[static_cast<size_t>(z) * GetCellsX() + static_cast<size_t>(x)] = zoneId;
		}

		uint32 WorldMapInfo::GetCell(const int64 globalCellX, const int64 globalCellZ) const
		{
			const int64 x = globalCellX - static_cast<int64>(minPageX) * CellsPerPage;
			const int64 z = globalCellZ - static_cast<int64>(minPageZ) * CellsPerPage;
			if (x < 0 || z < 0 || x >= GetCellsX() || z >= GetCellsZ() || m_cells.empty())
			{
				return 0;
			}

			return m_cells[static_cast<size_t>(z) * GetCellsX() + static_cast<size_t>(x)];
		}

		uint32 WorldMapInfo::GetZoneAt(const double x, const double z) const
		{
			const double cellX = std::floor((x - GridOrigin()) / CellSize);
			const double cellZ = std::floor((z - GridOrigin()) / CellSize);
			return GetCell(static_cast<int64>(cellX), static_cast<int64>(cellZ));
		}

		bool WorldMapInfo::HasPage(const uint32 pageX, const uint32 pageZ) const
		{
			return pageX < MaxPages && pageZ < MaxPages && pages[pageZ * MaxPages + pageX];
		}

		WorldRect WorldMapInfo::GetBounds() const
		{
			WorldRect rect;
			rect.minX = GridOrigin() + minPageX * PageSize;
			rect.minZ = GridOrigin() + minPageZ * PageSize;
			rect.maxX = GridOrigin() + (maxPageX + 1) * PageSize;
			rect.maxZ = GridOrigin() + (maxPageZ + 1) * PageSize;
			return rect;
		}

		const ZoneInfo* WorldMapInfo::FindZone(const uint32 zoneId) const
		{
			const auto it = std::find_if(zones.begin(), zones.end(), [zoneId](const ZoneInfo& zone) { return zone.zoneId == zoneId; });
			return it != zones.end() ? &*it : nullptr;
		}

		void WorldMapInfo::RebuildZones()
		{
			struct Accumulator
			{
				uint32 minX = std::numeric_limits<uint32>::max();
				uint32 minZ = std::numeric_limits<uint32>::max();
				uint32 maxX = 0;
				uint32 maxZ = 0;
				double sumX = 0.0;
				double sumZ = 0.0;
				uint32 count = 0;
			};

			std::map<uint32, Accumulator> accumulators;
			const uint32 cellsX = GetCellsX();
			const uint32 cellsZ = GetCellsZ();
			for (uint32 z = 0; z < cellsZ && !m_cells.empty(); ++z)
			{
				for (uint32 x = 0; x < cellsX; ++x)
				{
					const uint32 zoneId = m_cells[static_cast<size_t>(z) * cellsX + x];
					if (zoneId == 0)
					{
						continue;
					}

					Accumulator& acc = accumulators[zoneId];
					acc.minX = std::min(acc.minX, x);
					acc.minZ = std::min(acc.minZ, z);
					acc.maxX = std::max(acc.maxX, x);
					acc.maxZ = std::max(acc.maxZ, z);
					acc.sumX += x + 0.5;
					acc.sumZ += z + 0.5;
					++acc.count;
				}
			}

			const double originX = GridOrigin() + minPageX * PageSize;
			const double originZ = GridOrigin() + minPageZ * PageSize;

			zones.clear();
			for (const auto& [zoneId, acc] : accumulators)
			{
				ZoneInfo zone;
				zone.zoneId = zoneId;
				zone.cellCount = acc.count;
				zone.bounds.minX = originX + acc.minX * CellSize;
				zone.bounds.minZ = originZ + acc.minZ * CellSize;
				zone.bounds.maxX = originX + (acc.maxX + 1) * CellSize;
				zone.bounds.maxZ = originZ + (acc.maxZ + 1) * CellSize;

				// The cell of the zone closest to its centroid
				const double centroidX = acc.sumX / acc.count;
				const double centroidZ = acc.sumZ / acc.count;
				double bestDistance = std::numeric_limits<double>::max();
				double bestX = centroidX;
				double bestZ = centroidZ;
				for (uint32 z = acc.minZ; z <= acc.maxZ; ++z)
				{
					for (uint32 x = acc.minX; x <= acc.maxX; ++x)
					{
						if (m_cells[static_cast<size_t>(z) * cellsX + x] != zoneId)
						{
							continue;
						}

						const double dx = x + 0.5 - centroidX;
						const double dz = z + 0.5 - centroidZ;
						const double distance = dx * dx + dz * dz;
						if (distance < bestDistance)
						{
							bestDistance = distance;
							bestX = x + 0.5;
							bestZ = z + 0.5;
						}
					}
				}

				zone.labelX = originX + bestX * CellSize;
				zone.labelZ = originZ + bestZ * CellSize;
				zones.push_back(zone);
			}

			std::stable_sort(zones.begin(), zones.end(), [](const ZoneInfo& a, const ZoneInfo& b) { return a.cellCount > b.cellCount; });
		}

		bool WorldMapInfo::HasArtTile(const uint32 level, const uint32 x, const uint32 z) const
		{
			const uint32 span = 1u << level;
			for (uint32 pz = z * span; pz < (z + 1) * span && pz < MaxPages; ++pz)
			{
				for (uint32 px = x * span; px < (x + 1) * span && px < MaxPages; ++px)
				{
					if (pages[pz * MaxPages + px])
					{
						return true;
					}
				}
			}

			return false;
		}

		io::Writer& operator<<(io::Writer& writer, const WorldMapInfo& info)
		{
			writer
				<< io::write<uint32>(FormatVersion)
				<< io::write<uint8>(info.minPageX)
				<< io::write<uint8>(info.minPageZ)
				<< io::write<uint8>(info.maxPageX)
				<< io::write<uint8>(info.maxPageZ)
				<< io::write<uint8>(info.maxArtLevel);

			// Page presence, one bit per page
			std::vector<uint8> pageBits(MaxPages * MaxPages / 8, 0);
			for (size_t i = 0; i < info.pages.size() && i < MaxPages * MaxPages; ++i)
			{
				if (info.pages[i])
				{
					pageBits[i / 8] |= static_cast<uint8>(1u << (i % 8));
				}
			}
			writer << io::write_range(pageBits);

			// Cells, run-length encoded: zones are large, contiguous areas
			std::vector<std::pair<uint32, uint32>> runs;
			for (const uint32 zoneId : info.m_cells)
			{
				if (!runs.empty() && runs.back().first == zoneId)
				{
					++runs.back().second;
				}
				else
				{
					runs.emplace_back(zoneId, 1);
				}
			}

			writer << io::write<uint32>(runs.size());
			for (const auto& [zoneId, count] : runs)
			{
				writer << io::write<uint32>(zoneId) << io::write<uint32>(count);
			}

			writer << io::write<uint32>(info.zones.size());
			for (const ZoneInfo& zone : info.zones)
			{
				writer
					<< io::write<uint32>(zone.zoneId)
					<< io::write<uint32>(zone.cellCount)
					<< io::write<float>(static_cast<float>(zone.bounds.minX))
					<< io::write<float>(static_cast<float>(zone.bounds.minZ))
					<< io::write<float>(static_cast<float>(zone.bounds.maxX))
					<< io::write<float>(static_cast<float>(zone.bounds.maxZ))
					<< io::write<float>(static_cast<float>(zone.labelX))
					<< io::write<float>(static_cast<float>(zone.labelZ));
			}

			return writer;
		}

		io::Reader& operator>>(io::Reader& reader, WorldMapInfo& info)
		{
			uint32 version = 0;
			uint8 minPageX = 0, minPageZ = 0, maxPageX = 0, maxPageZ = 0, maxLevel = 0;
			if (!(reader >> io::read<uint32>(version)) || version != FormatVersion)
			{
				reader.setFailure();
				return reader;
			}

			if (!(reader
				>> io::read<uint8>(minPageX)
				>> io::read<uint8>(minPageZ)
				>> io::read<uint8>(maxPageX)
				>> io::read<uint8>(maxPageZ)
				>> io::read<uint8>(maxLevel)))
			{
				return reader;
			}

			if (minPageX > maxPageX || minPageZ > maxPageZ || maxPageX >= MaxPages || maxPageZ >= MaxPages)
			{
				reader.setFailure();
				return reader;
			}

			info.minPageX = minPageX;
			info.minPageZ = minPageZ;
			info.maxPageX = maxPageX;
			info.maxPageZ = maxPageZ;
			info.maxArtLevel = std::clamp<uint32>(maxLevel, MinArtLevel, MaxArtLevel);

			std::vector<uint8> pageBits(MaxPages * MaxPages / 8, 0);
			if (!(reader >> io::read_range(pageBits)))
			{
				return reader;
			}

			info.pages.assign(MaxPages * MaxPages, false);
			for (size_t i = 0; i < info.pages.size(); ++i)
			{
				info.pages[i] = (pageBits[i / 8] & (1u << (i % 8))) != 0;
			}

			info.ResetCells();
			uint32 runCount = 0;
			if (!(reader >> io::read<uint32>(runCount)))
			{
				return reader;
			}

			size_t cell = 0;
			for (uint32 i = 0; i < runCount; ++i)
			{
				uint32 zoneId = 0, count = 0;
				if (!(reader >> io::read<uint32>(zoneId) >> io::read<uint32>(count)))
				{
					return reader;
				}

				if (count > info.m_cells.size() - cell)
				{
					reader.setFailure();
					return reader;
				}

				std::fill_n(info.m_cells.begin() + static_cast<std::ptrdiff_t>(cell), count, zoneId);
				cell += count;
			}

			uint32 zoneCount = 0;
			if (!(reader >> io::read<uint32>(zoneCount)) || zoneCount > info.m_cells.size())
			{
				reader.setFailure();
				return reader;
			}

			info.zones.resize(zoneCount);
			for (ZoneInfo& zone : info.zones)
			{
				float minX = 0, minZ = 0, maxX = 0, maxZ = 0, labelX = 0, labelZ = 0;
				if (!(reader
					>> io::read<uint32>(zone.zoneId)
					>> io::read<uint32>(zone.cellCount)
					>> io::read<float>(minX)
					>> io::read<float>(minZ)
					>> io::read<float>(maxX)
					>> io::read<float>(maxZ)
					>> io::read<float>(labelX)
					>> io::read<float>(labelZ)))
				{
					return reader;
				}

				zone.bounds = WorldRect{ minX, minZ, maxX, maxZ };
				zone.labelX = labelX;
				zone.labelZ = labelZ;
			}

			return reader;
		}

		WorldRect View::GetVisibleRect() const
		{
			return WorldRect{ ScreenToWorldX(0.0), ScreenToWorldZ(0.0), ScreenToWorldX(width), ScreenToWorldZ(height) };
		}

		void View::ZoomAt(const double sx, const double sy, const double newUnitsPerPixel)
		{
			const double worldX = ScreenToWorldX(sx);
			const double worldZ = ScreenToWorldZ(sy);
			unitsPerPixel = newUnitsPerPixel;
			centerX = worldX - (sx - width * 0.5) * unitsPerPixel;
			centerZ = worldZ - (sy - height * 0.5) * unitsPerPixel;
		}

		double View::FitUnitsPerPixel(const WorldRect& rect, const double margin) const
		{
			const double w = std::max(width, 1.0);
			const double h = std::max(height, 1.0);
			return std::max(rect.Width() / w, rect.Height() / h) * std::max(margin, 1.0);
		}

		void ClampView(View& view, const WorldRect& mapBounds, const double minUnitsPerPixel)
		{
			const double maxUnitsPerPixel = std::max(view.FitUnitsPerPixel(mapBounds, 1.1), minUnitsPerPixel);
			view.unitsPerPixel = std::clamp(view.unitsPerPixel, minUnitsPerPixel, maxUnitsPerPixel);

			const auto clampAxis = [](double& center, const double minValue, const double maxValue, const double halfExtent)
			{
				const double size = maxValue - minValue;
				if (size <= halfExtent * 2.0)
				{
					// The whole map fits on this axis: keep it centred
					center = (minValue + maxValue) * 0.5;
					return;
				}

				// Let the map edge come a fifth of the view into it, so edge zones can be centred a bit
				const double slack = halfExtent * 0.4;
				center = std::clamp(center, minValue + halfExtent - slack, maxValue - halfExtent + slack);
			};

			clampAxis(view.centerX, mapBounds.minX, mapBounds.maxX, view.width * view.unitsPerPixel * 0.5);
			clampAxis(view.centerZ, mapBounds.minZ, mapBounds.maxZ, view.height * view.unitsPerPixel * 0.5);
		}

		bool ClampToCircle(float& dx, float& dz, const float radius)
		{
			const float distanceSq = dx * dx + dz * dz;
			if (distanceSq <= radius * radius || distanceSq <= 0.0f)
			{
				return false;
			}

			const float scale = radius / std::sqrt(distanceSq);
			dx *= scale;
			dz *= scale;
			return true;
		}
	}
}
