
#include "map.h"

#include <DetourCommon.h>
#include <random>
#include <recastnavigation/DetourCrowd/Include/DetourPathCorridor.h>

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

	void Map::UnloadAllPages()
	{
		for (auto y = 0; y < terrain::constants::MaxPages; ++y)
		{
			for (auto x = 0; x < terrain::constants::MaxPages; ++x)
			{
				m_loadedPage[x][y] = false;
			}
		}

		m_tiles.clear();
	}


	namespace
	{
		inline bool InRange(const float* v1, const float* v2, const float r, const float h)
		{
			const float dx = v2[0] - v1[0];
			const float dy = v2[1] - v1[1];
			const float dz = v2[2] - v1[2];
			return (dx * dx + dz * dz) < r * r && fabsf(dy) < h;
		}

		int FixupShortcuts(dtPolyRef* path, int npath, const dtNavMeshQuery* navQuery)
		{
			if (npath < 3)
				return npath;

			// Get connected polygons
			static const int maxNeis = 16;
			dtPolyRef neis[maxNeis];
			int nneis = 0;

			const dtMeshTile* tile = 0;
			const dtPoly* poly = 0;
			if (dtStatusFailed(navQuery->getAttachedNavMesh()->getTileAndPolyByRef(path[0], &tile, &poly)))
				return npath;

			for (unsigned int k = poly->firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
			{
				const dtLink* link = &tile->links[k];
				if (link->ref != 0)
				{
					if (nneis < maxNeis)
						neis[nneis++] = link->ref;
				}
			}

			// If any of the neighbour polygons is within the next few polygons
			// in the path, short cut to that polygon directly.
			static const int maxLookAhead = 6;
			int cut = 0;
			for (int i = dtMin(maxLookAhead, npath) - 1; i > 1 && cut == 0; i--) {
				for (int j = 0; j < nneis; j++)
				{
					if (path[i] == neis[j]) {
						cut = i;
						break;
					}
				}
			}
			if (cut > 1)
			{
				int offset = cut - 1;
				npath -= offset;
				for (int i = 1; i < npath; i++)
					path[i] = path[i + offset];
			}

			return npath;
		}

		bool GetSteerTarget(const dtNavMeshQuery* navQuery, const float* startPos, const float* endPos,
			const float minTargetDist,
			const dtPolyRef* path, const int pathSize,
			float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef,
			float* outPoints = 0, int* outPointCount = 0)
		{
			// Find steer target.
			static const int MAX_STEER_POINTS = 10;
			float steerPath[MAX_STEER_POINTS * 3];
			unsigned char steerPathFlags[MAX_STEER_POINTS];
			dtPolyRef steerPathPolys[MAX_STEER_POINTS];
			int nsteerPath = 0;
			navQuery->findStraightPath(startPos, endPos, path, pathSize,
				steerPath, steerPathFlags, steerPathPolys, &nsteerPath, MAX_STEER_POINTS);
			if (!nsteerPath)
				return false;

			if (outPoints && outPointCount)
			{
				*outPointCount = nsteerPath;
				for (int i = 0; i < nsteerPath; ++i)
					dtVcopy(&outPoints[i * 3], &steerPath[i * 3]);
			}


			// Find vertex far enough to steer to.
			int ns = 0;
			while (ns < nsteerPath)
			{
				// Stop at Off-Mesh link or when point is further than slop away.
				if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
					!InRange(&steerPath[ns * 3], startPos, minTargetDist, 1000.0f))
					break;
				ns++;
			}
			// Failed to find good point to steer to.
			if (ns >= nsteerPath)
				return false;

			dtVcopy(steerPos, &steerPath[ns * 3]);

			float h;
			if (dtStatusSucceed(navQuery->getPolyHeight(steerPosRef, steerPos, &h)))
				steerPos[1] = h;

			steerPosFlag = steerPathFlags[ns];
			steerPosRef = steerPathPolys[ns];

			return true;
		}
	}

	bool Map::FindPath(const Vector3& start, const Vector3& end, std::vector<Vector3>& output, bool allowPartial) const
	{
		constexpr float extents[] = { 5.f, 3.5f, 5.f };

		float recastStart[3] = { start.x, start.y, start.z };
		float recastEnd[3] = { end.x, end.y, end.z };

		dtPolyRef startPolyRef, endPolyRef;
		if (!dtStatusSucceed(m_navQuery.findNearestPoly(recastStart, extents, &m_queryFilter,
			&startPolyRef, nullptr)))
		{
			return false;
		}

		if (!startPolyRef)
		{
			return false;
		}

		if (!dtStatusSucceed(m_navQuery.findNearestPoly(recastEnd, extents, &m_queryFilter,
			&endPolyRef, nullptr)))
		{
			return false;
		}

		if (!endPolyRef)
		{
			return false;
		}

		// Just build a shortcut path?
		if (startPolyRef == endPolyRef)
		{
			output.push_back(start);
			output.push_back(end);
			return true;
		}

		dtPolyRef polys[MaxPathPolys];
		int m_npolys = 0;

		float smoothPath[MaxSmoothPathPoints * 3];

		if (!dtStatusSucceed(m_navQuery.findPath(startPolyRef, endPolyRef, recastStart, recastEnd, &m_queryFilter, polys, &m_npolys, MaxPathPolys)))
		{
			return false;
		}

		int smoothPathPoints = 0;
		if (m_npolys)
		{
			// Iterate over the path to find smooth path on the detail mesh surface.
			int npolys = m_npolys;

			float iterPos[3], targetPos[3];
			m_navQuery.closestPointOnPoly(startPolyRef, recastStart, iterPos, nullptr);
			m_navQuery.closestPointOnPoly(polys[npolys - 1], recastEnd, targetPos, nullptr);

			static constexpr float STEP_SIZE = 3.0f; // Smaller step in tight spaces
			static constexpr float SLOP = 0.5f;     // Relax the slop to avoid frequent redirects

			dtVcopy(&smoothPath[smoothPathPoints * 3], iterPos);
			smoothPathPoints++;

			// Move towards target a small advancement at a time until target reached or
			// when ran out of memory to store the path.
			while (npolys && smoothPathPoints < MaxSmoothPathPoints)
			{
				// Find location to steer towards.
				float steerPos[3];
				unsigned char steerPosFlag;
				dtPolyRef steerPosRef;

				if (!GetSteerTarget(&m_navQuery, iterPos, targetPos, SLOP,
					polys, npolys, steerPos, steerPosFlag, steerPosRef))
				{
					break;
				}

				bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) ? true : false;
				bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ? true : false;

				// Find movement delta.
				float delta[3], len;
				dtVsub(delta, steerPos, iterPos);
				len = dtMathSqrtf(dtVdot(delta, delta));
				// If the steer target is end of path or off-mesh link, do not move past the location.
				if ((endOfPath || offMeshConnection) && len < STEP_SIZE)
				{
					len = 1;
				}
				else
				{
					len = STEP_SIZE / len;
				}
				float moveTgt[3];
				dtVmad(moveTgt, iterPos, delta, len);

				// Move
				float result[3];
				dtPolyRef visited[16];
				int nvisited = 0;
				m_navQuery.moveAlongSurface(polys[0], iterPos, moveTgt, &m_queryFilter, result, visited, &nvisited, 16);

				npolys = dtMergeCorridorStartMoved(polys, npolys, MaxPathPolys, visited, nvisited);
				//npolys = FixupShortcuts(polys, npolys, &m_navQuery);

				float h = 0;
				m_navQuery.getPolyHeight(polys[0], result, &h);
				result[1] = h;
				dtVcopy(iterPos, result);

				// Handle end of path and off-mesh links when close enough.
				if (endOfPath && InRange(iterPos, steerPos, SLOP, 1.0f))
				{
					// Reached end of path.
					dtVcopy(iterPos, targetPos);
					if (smoothPathPoints < MaxSmoothPathPoints)
					{
						dtVcopy(&smoothPath[smoothPathPoints * 3], iterPos);
						smoothPathPoints++;
					}
					break;
				}

				if (offMeshConnection && InRange(iterPos, steerPos, SLOP, 1.0f))
				{
					// Reached off-mesh connection.
					float startPos[3], endPos[3];

					// Advance the path up to and over the off-mesh connection.
					dtPolyRef prevRef = 0, polyRef = polys[0];
					int npos = 0;
					while (npos < npolys && polyRef != steerPosRef)
					{
						prevRef = polyRef;
						polyRef = polys[npos];
						npos++;
					}
					for (int i = npos; i < npolys; ++i)
					{
						polys[i - npos] = polys[i];
					}
					npolys -= npos;

					// Handle the connection.
					dtStatus status = m_navMesh.getOffMeshConnectionPolyEndPoints(prevRef, polyRef, startPos, endPos);
					if (dtStatusSucceed(status))
					{
						if (smoothPathPoints < MaxSmoothPathPoints)
						{
							dtVcopy(&smoothPath[smoothPathPoints * 3], startPos);
							smoothPathPoints++;
							// Hack to make the dotted path not visible during off-mesh connection.
							if (smoothPathPoints & 1)
							{
								dtVcopy(&smoothPath[smoothPathPoints * 3], startPos);
								smoothPathPoints++;
							}
						}
						// Move position at the other side of the off-mesh link.
						dtVcopy(iterPos, endPos);
						float eh = 0.0f;
						m_navQuery.getPolyHeight(polys[0], iterPos, &eh);
						iterPos[1] = eh;
					}
				}

				// Store results.
				if (smoothPathPoints < MaxSmoothPathPoints)
				{
					dtVcopy(&smoothPath[smoothPathPoints * 3], iterPos);
					smoothPathPoints++;
				}
			}
		}
		else
		{
			return false;
		}

		output.resize(smoothPathPoints);
		for (auto i = 0; i < smoothPathPoints; ++i)
		{
			output[i] = Vector3(smoothPath[i * 3], smoothPath[i * 3 + 1], smoothPath[i * 3 + 2]);
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
		constexpr int maxAttempts = 10;

		// Prepare for Recast/Detour calls.
		const float recastCenter[3] = { centerPosition.x, centerPosition.y, centerPosition.z };
		constexpr float extents[] = { 1.f, 2.f, 1.f };

		// Find the nearest polygon to centerPosition.
		dtPolyRef startRef;
		if (!dtStatusSucceed(m_navQuery.findNearestPoly(recastCenter, extents, &m_queryFilter, &startRef, nullptr)))
			return false;

		// Try multiple times to get a point that is truly within 'radius'.
		for (int i = 0; i < maxAttempts; ++i)
		{
			float outputPoint[3];
			dtPolyRef randomRef;

			// Ask Recast for a random point around the circle.
			if (dtStatusSucceed(m_navQuery.findRandomPointAroundCircle(
				startRef,
				recastCenter,
				radius,
				&m_queryFilter,
				&random_between_0_and_1,  // your custom frand() or similar
				&randomRef,
				outputPoint)))
			{
				// Check if Detour's picked point is actually within the circle.
				const float dx = outputPoint[0] - centerPosition.x;
				const float dy = outputPoint[1] - centerPosition.y;
				const float dz = outputPoint[2] - centerPosition.z;

				if ((dx * dx + dy * dy + dz * dz) <= (radius * radius))
				{
					// Good point!
					randomPoint = Vector3(outputPoint[0], outputPoint[1], outputPoint[2]);
					return true;
				}
			}
		}

		// We could not find a valid point that satisfies the distance constraint.
		return false;
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
