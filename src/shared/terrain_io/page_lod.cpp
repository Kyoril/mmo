// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "page_lod.h"
#include "page_data.h"

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/macros.h"
#include "base/utilities.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace terrain_io
	{
		namespace
		{
			constexpr uint32 OuterSide = terrain::constants::OuterVerticesPerPageSide;
			constexpr uint32 QuadsPerTileSide = terrain::constants::OuterVerticesPerTileSide - 1;
			constexpr uint32 QuadsPerPageSide = OuterSide - 1;

			constexpr uint32 LodFileVersion = 0x01;

			inline const ChunkMagic LodVersionChunk = MakeChunkMagic('REVM');
			inline const ChunkMagic LodHeightChunk = MakeChunkMagic('TGHL');
			inline const ChunkMagic LodNormalChunk = MakeChunkMagic('MRNL');

			/// Whether the page sub-quad (qx, qz), in page quad coordinates, carries water.
			bool HasWaterQuad(const PageLodSource &source, const uint32 qx, const uint32 qz)
			{
				const uint32 tileIndex = (qx / QuadsPerTileSide) + (qz / QuadsPerTileSide) * terrain::constants::TilesPerPage;
				const uint32 bit = (qx % QuadsPerTileSide) + (qz % QuadsPerTileSide) * QuadsPerTileSide;
				return (source.waterQuadMasks[tileIndex] & (1ull << bit)) != 0ull;
			}

			/// Whether any of the up to four sub-quads sharing outer vertex (x, z) carries water.
			bool TouchesWater(const PageLodSource &source, const uint32 x, const uint32 z)
			{
				if (source.waterQuadMasks == nullptr || source.waterVertexHeights == nullptr)
				{
					return false;
				}

				for (int32 dz = -1; dz <= 0; ++dz)
				{
					for (int32 dx = -1; dx <= 0; ++dx)
					{
						const int32 qx = static_cast<int32>(x) + dx;
						const int32 qz = static_cast<int32>(z) + dz;
						if (qx < 0 || qz < 0 || qx >= static_cast<int32>(QuadsPerPageSide) || qz >= static_cast<int32>(QuadsPerPageSide))
						{
							continue;
						}

						if (HasWaterQuad(source, static_cast<uint32>(qx), static_cast<uint32>(qz)))
						{
							return true;
						}
					}
				}

				return false;
			}

			/// Normalised mean of the full-resolution normals within half a LOD cell of outer vertex (x, z).
			EncodedNormal8 FilteredNormal(const PageLodSource &source, const uint32 x, const uint32 z)
			{
				constexpr int32 radius = static_cast<int32>(LodVertexStride / 2);

				float sx = 0.0f, sy = 0.0f, sz = 0.0f;
				for (int32 dz = -radius; dz <= radius; ++dz)
				{
					for (int32 dx = -radius; dx <= radius; ++dx)
					{
						const int32 px = static_cast<int32>(x) + dx;
						const int32 pz = static_cast<int32>(z) + dz;
						if (px < 0 || pz < 0 || px >= static_cast<int32>(OuterSide) || pz >= static_cast<int32>(OuterSide))
						{
							continue;
						}

						float nx, ny, nz;
						DecodeNormalSNorm8(source.normals[px + pz * OuterSide], nx, ny, nz);
						sx += nx;
						sy += ny;
						sz += nz;
					}
				}

				const float length = std::sqrt(sx * sx + sy * sy + sz * sz);
				if (length < 1.0e-6f)
				{
					return EncodeNormalSNorm8(0.0f, 1.0f, 0.0f);
				}

				return EncodeNormalSNorm8(sx / length, sy / length, sz / length);
			}

			class PageLodChunkReader final : public ChunkReader
			{
			public:
				explicit PageLodChunkReader(PageLodData &data)
					: m_data(data)
				{
					AddChunkHandler(*LodVersionChunk, true, *this, &PageLodChunkReader::ReadVersionChunk);
				}

			private:
				bool ReadVersionChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					uint32 version = 0;
					if (!(reader >> io::read<uint32>(version)))
					{
						ELOG("Failed to read terrain page LOD format version!");
						return false;
					}

					if (version != LodFileVersion)
					{
						ELOG("Unsupported terrain page LOD format version " << log_hex_digit(version) << " (re-bake the terrain LOD in the editor)");
						return false;
					}

					m_ignoreUnhandledChunks = true;
					AddChunkHandler(*LodHeightChunk, true, *this, &PageLodChunkReader::ReadHeightChunk);
					AddChunkHandler(*LodNormalChunk, true, *this, &PageLodChunkReader::ReadNormalChunk);
					return reader;
				}

				bool ReadHeightChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					if (size != LodVertexCount * sizeof(float))
					{
						ELOG("Terrain page LOD height chunk has an unexpected size of " << size << " bytes");
						return false;
					}

					return reader >> io::read_range(m_data.heights);
				}

				bool ReadNormalChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					if (size != LodVertexCount * sizeof(EncodedNormal8))
					{
						ELOG("Terrain page LOD normal chunk has an unexpected size of " << size << " bytes");
						return false;
					}

					for (auto &normal : m_data.normals)
					{
						reader.readPOD(normal);
					}

					return reader;
				}

			private:
				PageLodData &m_data;
			};
		}

		void PageLodData::Reset()
		{
			heights.assign(LodVertexCount, 0.0f);
			normals.assign(LodVertexCount, EncodeNormalSNorm8(0.0f, 1.0f, 0.0f));
		}

		PageLodSource MakeLodSource(const PageData &data)
		{
			PageLodSource source;
			source.heightmap = data.heightmap.data();
			source.normals = data.normals.data();
			source.waterQuadMasks = data.waterQuadMasks.empty() ? nullptr : data.waterQuadMasks.data();
			source.waterVertexHeights = data.waterVertexHeights.empty() ? nullptr : data.waterVertexHeights.data();
			return source;
		}

		void BuildPageLod(const PageLodSource &source, PageLodData &out)
		{
			ASSERT(source.heightmap && source.normals);

			out.Reset();

			for (uint32 j = 0; j < LodVerticesPerPageSide; ++j)
			{
				for (uint32 i = 0; i < LodVerticesPerPageSide; ++i)
				{
					const uint32 x = i * LodVertexStride;
					const uint32 z = j * LodVertexStride;
					const uint32 outerIndex = x + z * OuterSide;
					const uint32 lodIndex = i + j * LodVerticesPerPageSide;

					float height = source.heightmap[outerIndex];
					EncodedNormal8 normal = FilteredNormal(source, x, z);

					if (TouchesWater(source, x, z))
					{
						const float surface = source.waterVertexHeights[outerIndex];
						if (surface > height)
						{
							height = surface;
							normal = EncodeNormalSNorm8(0.0f, 1.0f, 0.0f);
						}
					}

					out.heights[lodIndex] = height;
					out.normals[lodIndex] = normal;
				}
			}
		}

		bool SavePageLod(io::Writer &writer, const PageLodData &lod)
		{
			if (lod.heights.size() != LodVertexCount || lod.normals.size() != LodVertexCount)
			{
				ELOG("Unable to save terrain page LOD: unexpected array sizes");
				return false;
			}

			{
				ChunkWriter versionChunk{ LodVersionChunk, writer };
				writer << io::write<uint32>(LodFileVersion);
				versionChunk.Finish();
			}

			{
				ChunkWriter heightChunk{ LodHeightChunk, writer };
				writer << io::write_range(lod.heights.data(), lod.heights.data() + lod.heights.size());
				heightChunk.Finish();
			}

			{
				ChunkWriter normalChunk{ LodNormalChunk, writer };
				for (const auto &normal : lod.normals)
				{
					writer.WritePOD(normal);
				}
				normalChunk.Finish();
			}

			return true;
		}

		bool LoadPageLod(io::Reader &reader, PageLodData &out)
		{
			out.Reset();

			PageLodChunkReader chunkReader{ out };
			return chunkReader.Read(reader);
		}
	}
}
