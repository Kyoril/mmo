
#include "map.h"

#include <random>

#include "tile.h"

#include "stream_source.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"

#include "DetourNavMesh.h"

namespace mmo::nav
{
	static constexpr uint32 FileSignature = 'NAVM';
	static constexpr uint32 FileVersion = '0001';
	static constexpr uint32 FilePage = 'PAGE';
	static constexpr uint32 FileMap = 'MAP1';

	bool MapHeader::Verify() const
	{
		if (sig != FileSignature)
		{
			ELOG("File signature mismatch!");
			return false;
		}

		if (ver != FileVersion)
		{
			ELOG("File version mismatch!");
			return false;
		}

		if (kind != FilePage)
		{
			ELOG("Unsupported file kind");
			return false;
		}

		return true;
	}

	Map::Map(const std::string& mapName)
		: m_mapName(mapName)
	{
		const String filename = mapName + ".map";

		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(filename);
		if (!file)
		{
			ELOG("Failed to open map " << filename);
			return;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		uint32 magic;
		if (!(reader >> io::read<uint32>(magic)))
		{
			ELOG("Failed to read map magic");
			return;
		}

		if (magic != FileMap)
		{
			ELOG("Invalid or corrupted map file!");
			return;
		}

		uint8 hasTerrain;
		if (!(reader >> io::read<uint8>(hasTerrain)))
		{
			ELOG("Failed to read map terrain flag");
			return;
		}

		if (hasTerrain)
		{
			m_hasPages = true;

			std::uint8_t hasPages[terrain::constants::MaxPages * terrain::constants::MaxPages / 8];
			reader.readPOD(hasPages);

			// Extract bitmap
			for (auto y = 0; y < terrain::constants::MaxPages; ++y)
			{
				for (auto x = 0; x < terrain::constants::MaxPages; ++x)
				{
					auto const offset = y * terrain::constants::MaxPages + x;
					auto const byte_offset = offset / 8;
					auto const bit_offset = offset % 8;

					m_hasPage[x][y] = (hasPages[byte_offset] & (1 << bit_offset)) != 0;
				}
			}

			dtNavMeshParams params;

			constexpr float mapOrigin = -32.f * terrain::constants::PageSize;
			constexpr int maxTiles = terrain::constants::MaxPages * terrain::constants::TilesPerPage * terrain::constants::MaxPages * terrain::constants::TilesPerPage;

			params.orig[0] = mapOrigin;
			params.orig[1] = 0.f;
			params.orig[2] = mapOrigin;
			params.tileHeight = params.tileWidth = terrain::constants::TileSize;
			params.maxTiles = maxTiles;
			params.maxPolys = 1 << DT_POLY_BITS;

			auto const result = m_navMesh.init(&params);
			assert(result == DT_SUCCESS);
		}
		else
		{
			m_hasPages = false;
		}

		if (dtStatus result = m_navQuery.init(&m_navMesh, 65535); result != DT_SUCCESS)
		{
			ELOG("Failed to initialize navigation mesh query: " << result);
		}
	}

	bool Map::HasPage(const int32 x, const int32 y) const
	{
		ASSERT(x >= 0 && y >= 0);
		ASSERT(x < terrain::constants::MaxPages && y < terrain::constants::MaxPages);

		return m_hasPage[x][y];
	}

	bool Map::IsPageLoaded(const int32 x, const int32 y) const
	{
		ASSERT(x >= 0 && y >= 0);
		ASSERT(x < terrain::constants::MaxPages && y < terrain::constants::MaxPages);

		return m_loadedPage[x][y];
	}

	bool Map::LoadPage(int32 x, int32 y)
	{
		if (IsPageLoaded(x, y))
		{
			return true;
		}

		if (!HasPage(x, y))
		{
			return false;
		}

		std::stringstream strm;
		strm << std::setfill('0') << std::setw(2) << x << "_" << std::setfill('0') << std::setw(2) << y << ".nav";

		std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(m_mapName + "/" + strm.str());
		if (!file)
		{
			return false;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		MapHeader header;
		if (reader.readPOD(header); !reader)
		{
			ELOG("Failed to read map header for nav page " << x << "x" << y);
			return false;
		}

		if (!header.Verify())
		{
			ELOG("Failed to verify page header!");
			return false;
		}

		if (header.x != static_cast<std::uint32_t>(x) ||
			header.y != static_cast<std::uint32_t>(y))
		{
			ELOG("Map header coordinates mismatch for page " << x << "x" << y << ": File references coordinates " << header.x << "x" << header.y << " instead");
			return false;
		}

		for (auto i = 0u; i < header.tileCount; ++i)
		{
			auto tile = std::make_unique<Tile>(*this, reader, "");
			m_tiles[{tile->GetX(), tile->GetY()}] = std::move(tile);
		}

		DLOG("Loaded page " << x << "x" << y);
		m_loadedPage[x][y] = true;

		return true;
	}

	void Map::UnloadPage(int32 x, int32 y)
	{
		if (!m_loadedPage[x][y])
			return;

		for (auto tileY = y * terrain::constants::TilesPerPage;
			tileY < (y + 1) * terrain::constants::TilesPerPage; ++tileY)
		{
			for (auto tileX = x * terrain::constants::TilesPerPage;
				tileX < (x + 1) * terrain::constants::TilesPerPage; ++tileX)
			{
				auto i = m_tiles.find({ tileX, tileY });

				if (i != m_tiles.end())
				{
					m_tiles.erase(i);
				}
			}
		}

		m_loadedPage[x][y] = false;
	}

	int32 Map::LoadAllPages()
	{
		int32 result = 0;

		for (auto y = 0; y < terrain::constants::MaxPages; ++y)
		{
			for (auto x = 0; x < terrain::constants::MaxPages; ++x)
			{
				if (LoadPage(x, y))
				{
					++result;
				}
			}
		}

		return result;
	}

	bool Map::FindPath(const Vector3& start, const Vector3& end, std::vector<Vector3>& output, bool allowPartial) const
	{
		constexpr float extents[] = { 5., 5.f, 5.f };

		float recastStart[3] = { start.x, start.y, start.z };
		float recastEnd[3] = { end.x, end.y, end.z };

		dtPolyRef startPolyRef, endPolyRef;
		if (!(m_navQuery.findNearestPoly(recastStart, extents, &m_queryFilter,
			&startPolyRef, nullptr) &
			DT_SUCCESS))
		{
			return false;
		}

		if (!startPolyRef)
		{
			return false;
		}

		if (!(m_navQuery.findNearestPoly(recastEnd, extents, &m_queryFilter,
			&endPolyRef, nullptr) &
			DT_SUCCESS))
		{
			return false;
		}

		if (!endPolyRef)
		{
			return false;
		}

		dtPolyRef polyRefBuffer[MaxPathHops];

		int pathLength;
		auto const findPathResult = m_navQuery.findPath(startPolyRef, endPolyRef, recastStart, recastEnd, &m_queryFilter, polyRefBuffer, &pathLength, MaxPathHops);
		if (!(findPathResult & DT_SUCCESS) ||
			(!allowPartial && !!(findPathResult & DT_PARTIAL_RESULT)))
		{
			return false;
		}

		float pathBuffer[MaxPathHops * 3];
		auto const findStraightPathResult = m_navQuery.findStraightPath(recastStart, recastEnd, polyRefBuffer, pathLength, pathBuffer, nullptr, nullptr, &pathLength, MaxPathHops, DT_STRAIGHTPATH_ALL_CROSSINGS);
		if (!(findStraightPathResult & DT_SUCCESS) ||
			(!allowPartial && !!(findStraightPathResult & DT_PARTIAL_RESULT)))
		{
			return false;
		}

		output.resize(pathLength);

		for (auto i = 0; i < pathLength; ++i)
		{
			output[i] = Vector3(pathBuffer[i * 3], pathBuffer[i * 3 + 1], pathBuffer[i * 3 + 2]);
		}

		return true;
	}

	namespace {

		float random_between_0_and_1() {
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_real_distribution<> dis(0.0, 1.0);

			return static_cast<float>(dis(gen));
		}

	} // anonymous namespace

	bool Map::FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const
	{
		float recastCenter[3] = { centerPosition.x, centerPosition.y, centerPosition.z };

		constexpr float extents[] = { 1.f, 1.f, 1.f };

		dtPolyRef startRef;
		if (m_navQuery.findNearestPoly(recastCenter, extents, &m_queryFilter,
			&startRef, nullptr) != DT_SUCCESS) {
			return false;
		}

		float outputPoint[3];

		dtPolyRef randomRef;
		if (m_navQuery.findRandomPointAroundCircle(startRef,
			recastCenter,
			radius,
			&m_queryFilter,
			&random_between_0_and_1,
			&randomRef,
			outputPoint) != DT_SUCCESS) {
			return false;
		}

		randomPoint = Vector3(outputPoint[0], outputPoint[1], outputPoint[2]);
		return true;
	}

	const Tile* Map::GetTile(float x, float y) const
	{
		// find the tile corresponding to this (x, y)
		int tileX, tileY;

		// maps based on a global WMO have their tiles positioned differently
		if (HasPages())
		{
			TODO("Implement coordinate calculation");
			//WorldToTile({ x, y, 0.f }, tileX, tileY);
		}
		else
		{
			TODO("Implement coordinate calculation");
		}

		auto const tile = m_tiles.find({ tileX, tileY });

		return tile == m_tiles.end() ? nullptr : tile->second.get();
	}
}
