// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "nav_mesh/map.h"
#include "terrain/constants.h"

#include "DetourAlloc.h"
#include "DetourNavMeshBuilder.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace test_nav
{
	using namespace mmo;

	// Mirror the constants of nav_mesh/map.cpp, which writes and checks the same file layout.
	constexpr uint32 FileSignature = 'NAVM';
	constexpr uint32 FileVersion = '0001';
	constexpr uint32 FilePage = 'PAGE';
	constexpr uint32 FileMap = 'MAP1';

	constexpr int32 TilesPerPage = static_cast<int32>(terrain::constants::TilesPerPage);
	constexpr float TileSize = static_cast<float>(terrain::constants::TileSize);
	constexpr float PageSize = static_cast<float>(terrain::constants::PageSize);
	constexpr float MapOrigin = -32.f * PageSize;

	/// World-space XZ origin (min corner) of a global tile.
	inline Vector3 TileOrigin(const int32 tileX, const int32 tileY)
	{
		return { MapOrigin + static_cast<float>(tileX) * TileSize, 0.f, MapOrigin + static_cast<float>(tileY) * TileSize };
	}

	/// Detour data for one tile: a single flat quad at y = 0 covering the whole tile, with all
	/// four edges marked as portals so neighbouring tiles link up.
	inline std::vector<uint8> BuildFlatTile(const int32 tileX, const int32 tileY)
	{
		constexpr unsigned short cells = 10;
		constexpr int nvp = 6;
		constexpr unsigned short nullIndex = 0xffff;

		const unsigned short verts[] = {
			0, 0, 0,
			0, 0, cells,
			cells, 0, cells,
			cells, 0, 0,
		};

		// Vertex indices, then per-edge neighbour info: 0x8000 | dir marks a portal edge
		// (0 = x-, 1 = z+, 2 = x+, 3 = z-), matching what Recast emits at tile borders.
		const unsigned short polys[nvp * 2] = {
			0, 1, 2, 3, nullIndex, nullIndex,
			0x8000 | 0, 0x8000 | 1, 0x8000 | 2, 0x8000 | 3, 0, 0,
		};
		const unsigned short polyFlags[] = { 1 };
		const unsigned char polyAreas[] = { 0 };

		dtNavMeshCreateParams params;
		std::memset(&params, 0, sizeof(params));
		params.verts = verts;
		params.vertCount = 4;
		params.polys = polys;
		params.polyFlags = polyFlags;
		params.polyAreas = polyAreas;
		params.polyCount = 1;
		params.nvp = nvp;
		params.tileX = tileX;
		params.tileY = tileY;
		params.bmin[0] = MapOrigin + static_cast<float>(tileX) * TileSize;
		params.bmin[1] = 0.f;
		params.bmin[2] = MapOrigin + static_cast<float>(tileY) * TileSize;
		params.bmax[0] = params.bmin[0] + TileSize;
		params.bmax[1] = 1.f;
		params.bmax[2] = params.bmin[2] + TileSize;
		params.cs = TileSize / cells;
		params.ch = 0.1f;
		params.walkableHeight = 2.f;
		params.walkableRadius = 0.5f;
		params.walkableClimb = 1.f;
		params.buildBvTree = true;

		unsigned char* data = nullptr;
		int dataSize = 0;
		REQUIRE(dtCreateNavMeshData(&params, &data, &dataSize));

		std::vector<uint8> result(data, data + dataSize);
		dtFree(data);
		return result;
	}

	/// Detour data for one tile split into cols x rows flat quads at y = 0. Quad (col, row) spans
	/// x in [col, col + 1) * TileSize / cols and z in [row, row + 1) * TileSize / rows, relative to
	/// the tile origin. Poly index is row * cols + col. Inner edges link neighbouring quads. Border
	/// edges are portals when portalBorders is set, otherwise walls.
	inline std::vector<uint8> BuildGridTile(const int32 tileX, const int32 tileY, const int32 cols, const int32 rows,
		const std::function<uint16(int32, int32)>& flags, const bool portalBorders)
	{
		constexpr int nvp = 6;
		constexpr unsigned short nullIndex = 0xffff;
		constexpr unsigned short voxels = 60;
		const unsigned short stepX = static_cast<unsigned short>(voxels / cols);
		const unsigned short stepZ = static_cast<unsigned short>(voxels / rows);

		std::vector<unsigned short> verts;
		for (int32 j = 0; j <= rows; ++j)
		{
			for (int32 i = 0; i <= cols; ++i)
			{
				verts.push_back(static_cast<unsigned short>(i * stepX));
				verts.push_back(0);
				verts.push_back(static_cast<unsigned short>(j * stepZ));
			}
		}

		const auto vertexIndex = [cols](const int32 i, const int32 j)
		{
			return static_cast<unsigned short>(j * (cols + 1) + i);
		};

		const auto border = [portalBorders](const unsigned short dir)
		{
			return portalBorders ? static_cast<unsigned short>(0x8000 | dir) : nullIndex;
		};

		std::vector<unsigned short> polys;
		std::vector<unsigned short> polyFlags;
		std::vector<unsigned char> polyAreas;
		for (int32 r = 0; r < rows; ++r)
		{
			for (int32 c = 0; c < cols; ++c)
			{
				// Same winding as BuildFlatTile: (x0,z0) (x0,z1) (x1,z1) (x1,z0).
				// Edge 0 = x-, 1 = z+, 2 = x+, 3 = z-.
				polys.push_back(vertexIndex(c, r));
				polys.push_back(vertexIndex(c, r + 1));
				polys.push_back(vertexIndex(c + 1, r + 1));
				polys.push_back(vertexIndex(c + 1, r));
				polys.push_back(nullIndex);
				polys.push_back(nullIndex);

				polys.push_back(c > 0 ? static_cast<unsigned short>(r * cols + c - 1) : border(0));
				polys.push_back(r < rows - 1 ? static_cast<unsigned short>((r + 1) * cols + c) : border(1));
				polys.push_back(c < cols - 1 ? static_cast<unsigned short>(r * cols + c + 1) : border(2));
				polys.push_back(r > 0 ? static_cast<unsigned short>((r - 1) * cols + c) : border(3));
				polys.push_back(0);
				polys.push_back(0);

				polyFlags.push_back(flags(c, r));
				polyAreas.push_back(0);
			}
		}

		dtNavMeshCreateParams params;
		std::memset(&params, 0, sizeof(params));
		params.verts = verts.data();
		params.vertCount = static_cast<int>(verts.size() / 3);
		params.polys = polys.data();
		params.polyFlags = polyFlags.data();
		params.polyAreas = polyAreas.data();
		params.polyCount = cols * rows;
		params.nvp = nvp;
		params.tileX = tileX;
		params.tileY = tileY;
		const Vector3 origin = TileOrigin(tileX, tileY);
		params.bmin[0] = origin.x;
		params.bmin[1] = 0.f;
		params.bmin[2] = origin.z;
		params.bmax[0] = origin.x + TileSize;
		params.bmax[1] = 1.f;
		params.bmax[2] = origin.z + TileSize;
		params.cs = TileSize / voxels;
		params.ch = 0.1f;
		params.walkableHeight = 2.f;
		params.walkableRadius = 0.5f;
		params.walkableClimb = 1.f;
		params.buildBvTree = true;

		unsigned char* data = nullptr;
		int dataSize = 0;
		REQUIRE(dtCreateNavMeshData(&params, &data, &dataSize));

		std::vector<uint8> result(data, data + dataSize);
		dtFree(data);
		return result;
	}

	/// Builds the Detour data of one global tile, or nothing to leave the tile out of its page.
	using TileBuilder = std::function<std::optional<std::vector<uint8>>(int32 tileX, int32 tileY)>;

	template <typename T>
	void WritePod(std::ofstream& out, const T& value)
	{
		out.write(reinterpret_cast<const char*>(&value), sizeof(value));
	}

	/// A nav map on disk, registered as the only asset folder for the lifetime of the object.
	class SyntheticNavMap
	{
	public:
		/// @param pages Pages the map's bitmap lists.
		/// @param missingFiles Listed pages whose .nav file is not written.
		/// @param tileBuilder Builds each tile of a page; defaults to a flat quad per tile.
		explicit SyntheticNavMap(const std::vector<std::pair<int32, int32>>& pages, const std::set<std::pair<int32, int32>>& missingFiles = {}, TileBuilder tileBuilder = {})
			: m_root(std::filesystem::temp_directory_path() / ("mmo_nav_streamer_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
			, m_tileBuilder(std::move(tileBuilder))
		{
			std::filesystem::create_directories(m_root / Name);

			uint8 bitmap[terrain::constants::MaxPagesSquared / 8] = {};
			for (const auto& [x, y] : pages)
			{
				const uint32 offset = static_cast<uint32>(y) * terrain::constants::MaxPages + static_cast<uint32>(x);
				bitmap[offset / 8] |= static_cast<uint8>(1 << (offset % 8));

				if (missingFiles.count({ x, y }) == 0)
				{
					WritePage(x, y);
				}
			}

			std::ofstream mapFile(m_root / (std::string(Name) + ".map"), std::ios::binary);
			WritePod(mapFile, FileMap);
			WritePod(mapFile, static_cast<uint8>(1));
			mapFile.write(reinterpret_cast<const char*>(bitmap), sizeof(bitmap));
			mapFile.close();

			AssetRegistry::Initialize(m_root, {});
		}

		~SyntheticNavMap()
		{
			AssetRegistry::Destroy();

			std::error_code error;
			std::filesystem::remove_all(m_root, error);
		}

		static constexpr const char* Name = "StreamTest";

	private:
		void WritePage(const int32 pageX, const int32 pageY) const
		{
			struct CollectedTile
			{
				int32 x;
				int32 y;
				std::vector<uint8> data;
			};

			std::vector<CollectedTile> tiles;
			for (int32 y = pageY * TilesPerPage; y < (pageY + 1) * TilesPerPage; ++y)
			{
				for (int32 x = pageX * TilesPerPage; x < (pageX + 1) * TilesPerPage; ++x)
				{
					if (m_tileBuilder)
					{
						if (std::optional<std::vector<uint8>> built = m_tileBuilder(x, y))
						{
							tiles.push_back({ x, y, std::move(*built) });
						}
					}
					else
					{
						tiles.push_back({ x, y, BuildFlatTile(x, y) });
					}
				}
			}

			char fileName[16];
			std::snprintf(fileName, sizeof(fileName), "%02d_%02d.nav", pageX, pageY);

			std::ofstream out(m_root / Name / fileName, std::ios::binary);

			nav::MapHeader header{};
			header.sig = FileSignature;
			header.ver = FileVersion;
			header.kind = FilePage;
			header.x = static_cast<uint32>(pageX);
			header.y = static_cast<uint32>(pageY);
			header.tileCount = static_cast<uint32>(tiles.size());
			WritePod(out, header);

			for (const CollectedTile& tile : tiles)
			{
				WritePod(out, tile.x);
				WritePod(out, tile.y);
				WritePod(out, static_cast<uint32>(tile.data.size()));
				out.write(reinterpret_cast<const char*>(tile.data.data()), static_cast<std::streamsize>(tile.data.size()));
			}
		}

		std::filesystem::path m_root;
		TileBuilder m_tileBuilder;
	};

	/// World position in the middle of a page, on the flat test mesh.
	inline Vector3 PageCenter(const int32 pageX, const int32 pageY)
	{
		return {
			MapOrigin + (static_cast<float>(pageX) + 0.5f) * PageSize,
			0.f,
			MapOrigin + (static_cast<float>(pageY) + 0.5f) * PageSize
		};
	}
}
