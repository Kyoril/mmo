// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "page_io.h"
#include "page_chunks.h"

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/utilities.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace terrain_io
	{
		namespace
		{
			constexpr size_t OuterVertexCount = terrain::constants::OuterVerticesPerPageSide * terrain::constants::OuterVerticesPerPageSide;
			constexpr size_t InnerVertexCount = terrain::constants::InnerVerticesPerPageSide * terrain::constants::InnerVerticesPerPageSide;
			constexpr size_t TileCount = terrain::constants::TilesPerPage * terrain::constants::TilesPerPage;
			constexpr size_t LayerPixelCount = terrain::constants::PixelsPerPage * terrain::constants::PixelsPerPage;

			constexpr uint32 PageFileVersion = 0x02;

			/// @brief Chunk reader for v2 terrain page files writing into a PageData instance.
			class PageDataChunkReader final : public ChunkReader
			{
			public:
				explicit PageDataChunkReader(PageData &data)
					: m_data(data)
				{
					AddChunkHandler(*VersionChunk, true, *this, &PageDataChunkReader::ReadVersionChunk);
				}

			private:
				bool ReadVersionChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					uint32 version = 0;
					if (!(reader >> io::read<uint32>(version)))
					{
						ELOG("Failed to read terrain page format version!");
						return false;
					}

					if (version != PageFileVersion)
					{
						ELOG("Unsupported terrain page format version " << log_hex_digit(version) << " (only v2 pages are supported; open and re-save the world in the editor to convert legacy pages)");
						return false;
					}

					m_ignoreUnhandledChunks = true;

					AddChunkHandler(*MaterialChunk, false, *this, &PageDataChunkReader::ReadMaterialChunk);
					AddChunkHandler(*VertexChunk, true, *this, &PageDataChunkReader::ReadVertexChunk);
					AddChunkHandler(*NormalChunk, true, *this, &PageDataChunkReader::ReadNormalChunk);
					AddChunkHandler(*InnerVertexChunk, false, *this, &PageDataChunkReader::ReadInnerVertexChunk);
					AddChunkHandler(*InnerNormalChunk, false, *this, &PageDataChunkReader::ReadInnerNormalChunk);
					AddChunkHandler(*InnerVertexShadingChunk, false, *this, &PageDataChunkReader::ReadInnerShadingChunk);
					AddChunkHandler(*HoleChunk, false, *this, &PageDataChunkReader::ReadHoleChunk);
					AddChunkHandler(*LayerChunk, true, *this, &PageDataChunkReader::ReadLayerChunk);
					AddChunkHandler(*AreaChunk, false, *this, &PageDataChunkReader::ReadAreaChunk);
					AddChunkHandler(*VertexShadingChunk, false, *this, &PageDataChunkReader::ReadShadingChunk);
					AddChunkHandler(*WaterQuadChunk, false, *this, &PageDataChunkReader::ReadWaterQuadChunk);

					return reader;
				}

				bool ReadMaterialChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					uint16 numMaterials = 0;
					if (!(reader >> io::read<uint16>(numMaterials)))
					{
						ELOG("Failed to read terrain page material count!");
						return false;
					}

					m_data.materialNames.clear();
					m_data.materialNames.reserve(numMaterials);

					for (uint16 i = 0; i < numMaterials; ++i)
					{
						String materialName;
						if (!(reader >> io::read_container<uint16>(materialName)))
						{
							ELOG("Failed to read terrain page material name!");
							return false;
						}

						m_data.materialNames.push_back(std::move(materialName));
					}

					return reader;
				}

				bool ReadVertexChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					return reader >> io::read_range(m_data.heightmap);
				}

				bool ReadNormalChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					for (auto &normal : m_data.normals)
					{
						reader.readPOD(normal);
					}

					return reader;
				}

				bool ReadInnerVertexChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					return reader >> io::read_range(m_data.innerHeightmap);
				}

				bool ReadInnerNormalChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					for (auto &normal : m_data.innerNormals)
					{
						reader.readPOD(normal);
					}

					return reader;
				}

				bool ReadInnerShadingChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					return reader >> io::read_range(m_data.innerColors);
				}

				bool ReadHoleChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					uint16 holeCount = 0;
					if (!(reader >> io::read<uint16>(holeCount)))
					{
						ELOG("Failed to read terrain page hole count!");
						return false;
					}

					for (uint16 i = 0; i < holeCount; ++i)
					{
						uint16 tileIndex = 0;
						uint64 holeMask = 0;
						if (!(reader >> io::read<uint16>(tileIndex) >> io::read<uint64>(holeMask)))
						{
							ELOG("Failed to read terrain page hole entry!");
							return false;
						}

						if (tileIndex < m_data.holes.size())
						{
							m_data.holes[tileIndex] = holeMask;
						}
					}

					return reader;
				}

				bool ReadLayerChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					return reader >> io::read_range(m_data.layers);
				}

				bool ReadAreaChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					for (auto &zone : m_data.zones)
					{
						reader >> io::read<uint32>(zone);
					}

					return reader;
				}

				bool ReadShadingChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					return reader >> io::read_range(m_data.colors);
				}

				bool ReadWaterQuadChunk(io::Reader &reader, uint32 header, uint32 size)
				{
					uint16 count = 0;
					if (!(reader >> io::read<uint16>(count)))
					{
						ELOG("Failed to read terrain page water quad count!");
						return false;
					}

					for (uint16 i = 0; i < count; ++i)
					{
						uint16 tileIndex = 0;
						uint8 type = 0;
						uint64 mask = 0;
						if (!(reader >> io::read<uint16>(tileIndex) >> io::read<uint8>(type) >> io::read<uint64>(mask)))
						{
							ELOG("Failed to read terrain page water quad entry!");
							return false;
						}

						if (tileIndex < m_data.waterQuadMasks.size())
						{
							m_data.waterQuadMasks[tileIndex] = mask;
							m_data.waterTypes[tileIndex] = type;
						}
					}

					for (float &height : m_data.waterVertexHeights)
					{
						if (!(reader >> io::read<float>(height)))
						{
							break;
						}
					}

					if (reader)
					{
						reader >> io::read_container<uint16>(m_data.waterMaterialName);
					}

					return reader;
				}

			private:
				PageData &m_data;
			};
		}

		void PageData::Reset()
		{
			heightmap.assign(OuterVertexCount, 0.0f);
			innerHeightmap.assign(InnerVertexCount, 0.0f);
			normals.assign(OuterVertexCount, EncodedNormal8{ 0, 127, 0 });
			innerNormals.assign(InnerVertexCount, EncodedNormal8{ 0, 127, 0 });
			materialNames.assign(TileCount, String());
			layers.assign(LayerPixelCount, 0x000000FF);
			colors.assign(OuterVertexCount, 0xffffffff);
			innerColors.assign(InnerVertexCount, 0xffffffff);
			holes.assign(TileCount, 0);
			zones.assign(TileCount, 0);
			waterQuadMasks.assign(TileCount, 0);
			waterTypes.assign(TileCount, 0);
			waterVertexHeights.assign(OuterVertexCount, 0.0f);
			waterMaterialName.clear();
		}

		PageDataView MakeView(const PageData &data)
		{
			PageDataView view;
			view.heightmap = data.heightmap.data();
			view.innerHeightmap = data.innerHeightmap.data();
			view.normals = data.normals.data();
			view.innerNormals = data.innerNormals.data();
			view.materialNames = data.materialNames.data();
			view.materialCount = data.materialNames.size();
			view.layers = data.layers.data();
			view.colors = data.colors.data();
			view.innerColors = data.innerColors.data();
			view.holes = data.holes.data();
			view.zones = data.zones.data();
			view.waterQuadMasks = data.waterQuadMasks.data();
			view.waterTypes = data.waterTypes.data();
			view.waterVertexHeights = data.waterVertexHeights.data();
			view.waterMaterialName = &data.waterMaterialName;
			return view;
		}

		bool SavePage(io::Writer &writer, const PageDataView &page)
		{
			if (!page.heightmap || !page.innerHeightmap || !page.normals || !page.innerNormals ||
				!page.layers || !page.colors || !page.innerColors || !page.zones)
			{
				ELOG("Unable to save terrain page: incomplete page data view!");
				return false;
			}

			// File version chunk
			{
				ChunkWriter versionChunkWriter{ VersionChunk, writer };
				writer << io::write<uint32>(PageFileVersion);
				versionChunkWriter.Finish();
			}

			// Materials
			{
				ChunkWriter materialChunkWriter{ MaterialChunk, writer };
				writer << io::write<uint16>(static_cast<uint16>(page.materialCount));
				for (size_t i = 0; i < page.materialCount; ++i)
				{
					writer << io::write_dynamic_range<uint16>(page.materialNames[i]);
				}
				materialChunkWriter.Finish();
			}

			// Heightmap (outer grid)
			{
				ChunkWriter heightmapChunk{ VertexChunk, writer };
				writer << io::write_range(page.heightmap, page.heightmap + OuterVertexCount);
				heightmapChunk.Finish();
			}

			// Inner heightmap (v2)
			{
				ChunkWriter innerHeightChunk{ InnerVertexChunk, writer };
				writer << io::write_range(page.innerHeightmap, page.innerHeightmap + InnerVertexCount);
				innerHeightChunk.Finish();
			}

			// (Encoded) Normals (outer grid)
			{
				ChunkWriter normalChunk{ NormalChunk, writer };
				for (size_t i = 0; i < OuterVertexCount; ++i)
				{
					writer.WritePOD(page.normals[i]);
				}
				normalChunk.Finish();
			}

			// Inner (encoded) normals (v2)
			{
				ChunkWriter innerNormalChunk{ InnerNormalChunk, writer };
				for (size_t i = 0; i < InnerVertexCount; ++i)
				{
					writer.WritePOD(page.innerNormals[i]);
				}
				innerNormalChunk.Finish();
			}

			// Layers
			{
				ChunkWriter layerChunk{ LayerChunk, writer };
				writer << io::write_range(page.layers, page.layers + LayerPixelCount);
				layerChunk.Finish();
			}

			// Vertex shading (outer grid)
			{
				ChunkWriter colorsChunk{ VertexShadingChunk, writer };
				writer << io::write_range(page.colors, page.colors + OuterVertexCount);
				colorsChunk.Finish();
			}

			// Inner vertex shading (v2)
			{
				ChunkWriter innerColorsChunk{ InnerVertexShadingChunk, writer };
				writer << io::write_range(page.innerColors, page.innerColors + InnerVertexCount);
				innerColorsChunk.Finish();
			}

			// Hole data (only save tiles with holes)
			if (page.holes)
			{
				std::vector<std::pair<uint16, uint64>> tilesWithHoles;
				for (uint16 i = 0; i < TileCount; ++i)
				{
					if (page.holes[i] != 0)
					{
						tilesWithHoles.emplace_back(i, page.holes[i]);
					}
				}

				if (!tilesWithHoles.empty())
				{
					ChunkWriter holeChunk{ HoleChunk, writer };
					writer << io::write<uint16>(static_cast<uint16>(tilesWithHoles.size()));

					for (const auto &tilePair : tilesWithHoles)
					{
						writer << io::write<uint16>(tilePair.first);
						writer << io::write<uint64>(tilePair.second);
					}

					holeChunk.Finish();
				}
			}

			// Water quad data: sparse quad masks + shared vertex heights
			if (page.waterQuadMasks)
			{
				std::vector<uint16> waterTileIndices;
				for (uint16 i = 0; i < TileCount; ++i)
				{
					if (page.waterQuadMasks[i] != 0)
					{
						waterTileIndices.push_back(i);
					}
				}

				const bool hasWaterMaterial = page.waterMaterialName && !page.waterMaterialName->empty();
				if (!waterTileIndices.empty() || hasWaterMaterial)
				{
					if (!page.waterTypes || !page.waterVertexHeights)
					{
						ELOG("Unable to save terrain page: water quad masks set but water types or vertex heights missing!");
						return false;
					}

					ChunkWriter waterChunkWriter{ WaterQuadChunk, writer };
					writer << io::write<uint16>(static_cast<uint16>(waterTileIndices.size()));
					for (const uint16 idx : waterTileIndices)
					{
						writer << io::write<uint16>(idx)
						       << io::write<uint8>(page.waterTypes[idx])
						       << io::write<uint64>(page.waterQuadMasks[idx]);
					}
					for (size_t i = 0; i < OuterVertexCount; ++i)
					{
						writer << io::write<float>(page.waterVertexHeights[i]);
					}
					writer << io::write_dynamic_range<uint16>(page.waterMaterialName ? *page.waterMaterialName : String());
					waterChunkWriter.Finish();
				}
			}

			// Zones
			{
				ChunkWriter areaChunk{ AreaChunk, writer };
				for (size_t i = 0; i < TileCount; ++i)
				{
					writer << io::write<uint32>(page.zones[i]);
				}
				areaChunk.Finish();
			}

			return true;
		}

		bool LoadPage(io::Reader &reader, PageData &out)
		{
			out.Reset();

			PageDataChunkReader chunkReader{ out };
			return chunkReader.Read(reader);
		}
	}
}
