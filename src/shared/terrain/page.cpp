
#include "page.h"

#include "terrain.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include <sstream>

#include "tile.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "base/utilities.h"
#include "binary_io/stream_sink.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"

namespace mmo
{
	namespace terrain
	{
		namespace constants
		{
			static const ChunkMagic VersionChunk = MakeChunkMagic('REVM');
			static const ChunkMagic MaterialChunk = MakeChunkMagic('TMCM');
			static const ChunkMagic VertexChunk = MakeChunkMagic('TVCM');
			static const ChunkMagic NormalChunk = MakeChunkMagic('MNCM');
			static const ChunkMagic LayerChunk = MakeChunkMagic('YLCM');
			static const ChunkMagic AreaChunk = MakeChunkMagic('RACM');
			static const ChunkMagic VertexShadingChunk = MakeChunkMagic('SVCM');
			// New v2 chunks to persist inner-grid data for editor precision
			static const ChunkMagic InnerVertexChunk = MakeChunkMagic('IVCM');
			static const ChunkMagic InnerNormalChunk = MakeChunkMagic('INCM');
			static const ChunkMagic InnerVertexShadingChunk = MakeChunkMagic('ISCM');
		}

		namespace
		{
			inline uint32 CalculateARGB(const Vector4& val)
			{
				return (
					static_cast<uint32>(val.w * 255) << 24 |
					static_cast<uint32>(val.z * 255) << 16 |
					static_cast<uint32>(val.y * 255) << 8 |
					static_cast<uint32>(val.x * 255)
					);
			}
		}
		Page::Page(Terrain& terrain, const int32 x, const int32 z)
			: m_terrain(terrain)
			, m_x(x)
			, m_z(z)
			, m_preparing(false)
			, m_prepared(false)
			, m_loaded(false)
		{
			const auto offset = Vector3(
				static_cast<float>(static_cast<double>(m_x - 32) * constants::PageSize),
				0.0f,
				static_cast<float>(static_cast<double>(m_z - 32) * constants::PageSize)
			);

			// Bounding box
			m_boundingBox = AABB(
				offset,
				offset + Vector3(constants::PageSize, 0.0f, constants::PageSize)
			);

			m_pageNode = m_terrain.GetNode()->CreateChildSceneNode();
			m_pageNode->SetPosition(offset);
		}

		Page::~Page()
		{
			Unload();

			if (m_pageNode != nullptr)
			{
				m_terrain.GetScene().DestroySceneNode(*m_pageNode);
				m_pageNode = nullptr;
			}
		}

		bool Page::Prepare()
		{
			if (IsPrepared() || IsPreparing())
			{
				return true;
			}

			m_preparing = true;

			// Page stores only outer vertices (shared between tiles)
			// Inner vertices are derived by tiles during rendering
			const size_t outerVertexCount = constants::OuterVerticesPerPageSide * constants::OuterVerticesPerPageSide;
			m_heightmap.resize(outerVertexCount);
			m_normals.resize(outerVertexCount);
			m_materials.resize(constants::TilesPerPage * constants::TilesPerPage, nullptr);
			m_layers.resize(constants::PixelsPerPage * constants::PixelsPerPage, 0x000000FF);
			m_tileZones.resize(constants::TilesPerPage * constants::TilesPerPage, 0);
			m_colors.resize(outerVertexCount, 0xffffffff);

			// Allocate inner arrays for editor precision (8*TilesPerPage per side)
			const size_t innerVertexCount = constants::InnerVerticesPerPageSide * constants::InnerVerticesPerPageSide;
			m_innerHeightmap.resize(innerVertexCount);
			m_innerNormals.resize(innerVertexCount);
			m_innerColors.resize(innerVertexCount, 0xffffffff);

			const String pageFileName = m_terrain.GetBaseFileName() + "/" + std::to_string(m_x) + "_" + std::to_string(m_z) + ".tile";
			if (AssetRegistry::HasFile(pageFileName))
			{
				std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(pageFileName);
				ASSERT(file);

				// Lets start fresh
				RemoveAllChunkHandlers();

				// Register chunk handler
				AddChunkHandler(*constants::VersionChunk, true, *this, &Page::ReadMCVRChunk);

				io::StreamSource source{ *file };
				io::Reader reader{source};
				if (!Read(reader))
				{
					ELOG("Failed to read page file '" << pageFileName << "'!");
					m_preparing = false;
					return false;
				}

				// If inner arrays were not provided (legacy v1), derive from outer grid
				if (m_innerHeightmap.empty())
				{
					const uint32 newSide = constants::InnerVerticesPerPageSide;
					for (uint32 j = 0; j < newSide; ++j)
					{
						for (uint32 i = 0; i < newSide; ++i)
						{
							const uint32 ox = i;
							const uint32 oz = j;
							const float h00 = GetHeightAt(ox, oz);
							const float h10 = GetHeightAt(ox + 1, oz);
							const float h01 = GetHeightAt(ox, oz + 1);
							const float h11 = GetHeightAt(ox + 1, oz + 1);
							m_innerHeightmap[i + j * newSide] = (h00 + h10 + h01 + h11) * 0.25f;

							// Approximate inner normals from outer normals
							Vector3 n00 = CalculateNormalAt(ox, oz);
							Vector3 n10 = CalculateNormalAt(ox + 1, oz);
							Vector3 n01 = CalculateNormalAt(ox, oz + 1);
							Vector3 n11 = CalculateNormalAt(ox + 1, oz + 1);
							Vector3 avg = ((n00 + n10 + n01 + n11) * 0.25f).NormalizedCopy();
							m_innerNormals[i + j * newSide] = EncodeNormalSNorm8(avg.x, avg.y, avg.z);

							// Average vertex colors
							const Color c00(GetColorAt(ox, oz));
							const Color c10(GetColorAt(ox + 1, oz));
							const Color c01(GetColorAt(ox, oz + 1));
							const Color c11(GetColorAt(ox + 1, oz + 1));
							const Color avgColor = (c00 + c10 + c01 + c11) * 0.25f;
							m_innerColors[i + j * newSide] = avgColor.GetARGB();
						}
					}
				}

				m_changed = true;
			}
			else
			{
				EncodedNormal8 encoded = EncodeNormalSNorm8(0.0f, 1.0f, 0.0f);
				for (size_t i = 0; i < m_normals.size(); ++i)
				{
					m_normals[i] = encoded;
				}

				WLOG("Terrain page file '" << pageFileName << "' is missing, page will be initialized as blank tile");
				m_changed = true;
			}

			m_prepared = true;
			m_preparing = false;

			return true;
		}

		bool Page::IsValid() const
		{
			return ChunkReader::IsValid();
		}

		bool Page::OnReadFinished()
		{
			return ChunkReader::OnReadFinished();
		}

		bool Page::Load()
		{
			if (m_unloadRequested)
			{
				m_unloadRequested = false;
				return true;
			}

			if (!IsLoadable())
			{
				return m_loaded;
			}

			// Build file name
			std::stringstream stream;
			stream << m_terrain.GetBaseFileName() << "_" << std::setfill('0') << std::setw(2) << m_x << "_" << std::setfill('0') << std::setw(2) << m_z;

			// Get filename
			const String filename = stream.str();

			String pageBaseName = "Page_" + std::to_string(m_x) + "_" + std::to_string(m_z);
			if (m_Tiles.empty())
			{
				m_Tiles = TileGrid(constants::TilesPerPage, constants::TilesPerPage);
			}

			bool allTilesLoaded = true;
			bool loadedNewTile = false;

			// Ensure we call load as many times as needed
			for (unsigned int i = 0; i < constants::TilesPerPage; i++)
			{
				for (unsigned int j = 0; j < constants::TilesPerPage; j++)
				{
					String tileName = pageBaseName + "_Tile_" + std::to_string(i) + "_" + std::to_string(j);
					auto& tile = m_Tiles(i, j);

					// Tile already loaded?
					if (tile)
					{
						continue;
					}

					tile = std::make_unique<Tile>(tileName, *this, i * (constants::OuterVerticesPerTileSide - 1), j * (constants::OuterVerticesPerTileSide - 1));

					// Setup tile material assignment
					if (const int tileIndex = i + j * constants::TilesPerPage; tileIndex < m_materials.size())
					{
						tile->SetMaterial(m_materials[tileIndex]);
					}

					// Ensure tile respects selection query
					tile->SetQueryFlags(m_terrain.GetTileSceneQueryFlags());
					m_pageNode->AttachObject(*tile);

					allTilesLoaded = false;
					loadedNewTile = true;
					break;
				}

				if (loadedNewTile)
				{
					break;
				}
			}

			if (allTilesLoaded)
			{
				m_loaded = true;
			}
			
			UpdateBoundingBox();
			return m_loaded;
		}

		void Page::Unload()
		{
			if (!m_loaded)
			{
				m_unloadRequested = true;
			}

			for (const auto& tile : m_Tiles)
			{
				if (!tile)
				{
					continue;
				}

				tile->DetachFromParent();
			}

			m_loaded = false;
			m_Tiles.clear();
		}

		void Page::Destroy()
		{
			if (IsPreparing())
			{
				return;
			}

			if (IsLoaded())
			{
				Unload();
			}

			// Unload all loaded data so we will have to reload it again later
			m_heightmap.clear();
			m_normals.clear();
			m_materials.clear();
			m_layers.clear();

			m_prepared = false;
			m_preparing = false;
		}

		Tile* Page::GetTile(const uint32 x, const uint32 y)
		{
			if (x >= m_Tiles.width() ||
				y >= m_Tiles.height()) 
			{
				return nullptr;
			}

			return m_Tiles(x, y).get();
		}

		Tile* Page::GetTileAt(float x, float z)
		{
			// Is the tile inside our bounds?
			if (x <= m_boundingBox.min.x || x >= m_boundingBox.max.x ||
				z <= m_boundingBox.min.z || z >= m_boundingBox.max.z)
			{
				return nullptr;
			}

			x -= m_boundingBox.min.x;
			z -= m_boundingBox.min.z;

			return GetTile(static_cast<uint32>((x / constants::PageSize) * constants::TilesPerPage), static_cast<uint32>((z / constants::PageSize) * constants::TilesPerPage));
		}

		Terrain& Page::GetTerrain()
		{
			return m_terrain;
		}

		uint32 Page::GetX() const
		{
			return m_x;
		}

		uint32 Page::GetY() const
		{
			return m_z;
		}

		uint32 Page::GetID() const
		{
			return m_x + m_z * m_terrain.GetWidth();
		}

		float Page::GetHeightAt(const size_t x, const size_t y) const
		{
			if (!IsPrepared())
			{
				return 0.0f;
			}

			if (x >= constants::OuterVerticesPerPageSide ||
				y >= constants::OuterVerticesPerPageSide)
			{
				return 0.0f;
			}

			return m_heightmap[x + y * constants::OuterVerticesPerPageSide];
		}

		uint32 Page::GetColorAt(size_t x, size_t y) const
		{
			if (!IsPrepared())
			{
				return 0xffffffff;
			}

			if (x >= constants::OuterVerticesPerPageSide ||
				y >= constants::OuterVerticesPerPageSide)
			{
				return 0xffffffff;
			}

			return m_colors[x + y * constants::OuterVerticesPerPageSide];
		}

		uint32 Page::GetLayersAt(const size_t x, const size_t y) const
		{
			if (!IsPrepared())
			{
				return 0;
			}

			if (x >= constants::PixelsPerPage ||
				y >= constants::PixelsPerPage)
			{
				return 0;
			}

			const size_t index = x + y * constants::PixelsPerPage;
			ASSERT(index < m_layers.size());
			return m_layers[index];
		}

		float Page::GetSmoothHeightAt(float x, float y) const
		{
			// scale down
			const float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::OuterVerticesPerPageSide - 1));
			x /= scale;
			y /= scale;

			// retrieve height from heightmap via bilinear interpolation
			size_t xi = (size_t)x, zi = (size_t)y;
			float xpct = x - xi, zpct = y - zi;
			if (xi == constants::OuterVerticesPerPageSide - 1)
			{
				// one too far
				--xi;
				xpct = 1.0f;
			}
			if (zi == constants::OuterVerticesPerPageSide - 1)
			{
				--zi;
				zpct = 1.0f;
			}

			// retrieve heights
			float heights[4];
			heights[0] = GetHeightAt(xi, zi);
			heights[1] = GetHeightAt(xi, zi + 1);
			heights[2] = GetHeightAt(xi + 1, zi);
			heights[3] = GetHeightAt(xi + 1, zi + 1);

			// interpolate
			float w[4];
			w[0] = (1.0f - xpct) * (1.0f - zpct);
			w[1] = (1.0f - xpct) * zpct;
			w[2] = xpct * (1.0f - zpct);
			w[3] = xpct * zpct;
			float ipHeight = w[0] * heights[0] + w[1] * heights[1] + w[2] * heights[2] + w[3] * heights[3];

			return ipHeight;
		}

		Vector3 Page::GetSmoothNormalAt(float x, float y) const
		{
			// scale down
			const float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::OuterVerticesPerPageSide - 1));
			x /= scale;
			y /= scale;

			// retrieve height from heightmap via bilinear interpolation
			size_t xi = (size_t)x, zi = (size_t)y;
			float xpct = x - xi, zpct = y - zi;
			if (xi == constants::OuterVerticesPerPageSide - 1)
			{
				// one too far
				--xi;
				xpct = 1.0f;
			}
			if (zi == constants::OuterVerticesPerPageSide - 1)
			{
				--zi;
				zpct = 1.0f;
			}

			// retrieve normals
			const Vector3 n00 = GetNormalAt(xi, zi);
			const Vector3 n01 = GetNormalAt(xi, zi + 1);
			const Vector3 n10 = GetNormalAt(xi + 1, zi);
			const Vector3 n11 = GetNormalAt(xi + 1, zi + 1);

			// Bilinear interpolation
			const Vector3 n0 = n00 * (1.0f - xpct) + n10 * xpct;
			const Vector3 n1 = n01 * (1.0f - xpct) + n11 * xpct;

			// Then interpolate the result along the z-axis.
			return (n0 * (1.0f - zpct) + n1 * zpct).NormalizedCopy();
		}

		void Page::UpdateTiles(const int fromX, const int fromZ, const int toX, const int toZ, const bool normalsOnly)
		{
			if (!m_loaded)
			{
				return;
			}

			unsigned int fromTileX = fromX / (constants::OuterVerticesPerTileSide - 1);
			unsigned int fromTileZ = fromZ / (constants::OuterVerticesPerTileSide - 1);
			unsigned int toTileX = toX / (constants::OuterVerticesPerTileSide - 1);
			unsigned int toTileZ = toZ / (constants::OuterVerticesPerTileSide - 1);

			if (fromTileX >= constants::TilesPerPage || fromTileZ >= constants::TilesPerPage)
			{
				return;
			}

			if (toTileX >= constants::TilesPerPage)
			{
				toTileX = constants::TilesPerPage - 1;
			}
			if (toTileZ >= constants::TilesPerPage)
			{
				toTileZ = constants::TilesPerPage - 1;
			}

			for (unsigned int x = fromTileX; x <= toTileX; x++)
			{
				for (unsigned int z = fromTileZ; z <= toTileZ; z++)
				{
					Tile* pTile = GetTile(x, z);
					if (pTile != nullptr)
					{
						if (normalsOnly)
						{
							//pTile->UpdateVertexNormals();
						}
						else
						{
							pTile->UpdateTerrain(fromX, fromZ, toX, toZ);
						}
					}
				}
			}

			if (!normalsOnly)
			{
				UpdateBoundingBox();
			}
		}

		void Page::UpdateTileCoverage(const int fromX, const int fromZ, const int toX, const int toZ)
		{
			if (!m_loaded)
			{
				return;
			}

			unsigned int fromTileX = fromX / (constants::PixelsPerTile - 1);
			unsigned int fromTileZ = fromZ / (constants::PixelsPerTile - 1);
			unsigned int toTileX = toX / (constants::PixelsPerTile - 1);
			unsigned int toTileZ = toZ / (constants::PixelsPerTile - 1);

			if (fromTileX >= constants::TilesPerPage || fromTileZ >= constants::TilesPerPage)
			{
				return;
			}

			if (toTileX >= constants::TilesPerPage) toTileX = constants::TilesPerPage - 1;
			if (toTileZ >= constants::TilesPerPage) toTileZ = constants::TilesPerPage - 1;

			for (unsigned int x = fromTileX; x <= toTileX; x++)
			{
				for (unsigned int z = fromTileZ; z <= toTileZ; z++)
				{
					Tile* pTile = GetTile(x, z);
					if (pTile != nullptr)
					{
						pTile->UpdateCoverageMap();
					}
				}
			}
		}

		Vector3 Page::GetNormalAt(const uint32 x, const uint32 z) const
		{
			ASSERT(x < constants::OuterVerticesPerPageSide && z < constants::OuterVerticesPerPageSide);

			Vector3 normal;
			DecodeNormalSNorm8(m_normals[x + z * constants::OuterVerticesPerPageSide], normal.x, normal.y, normal.z);
			return normal;
		}

		float Page::GetInnerHeightAt(size_t x, size_t y) const
		{
			ASSERT(x < constants::InnerVerticesPerPageSide && y < constants::InnerVerticesPerPageSide);
			return m_innerHeightmap[x + y * constants::InnerVerticesPerPageSide];
		}

		void Page::SetInnerHeightAt(size_t x, size_t y, float value)
		{
			ASSERT(x < constants::InnerVerticesPerPageSide && y < constants::InnerVerticesPerPageSide);
			m_innerHeightmap[x + y * constants::InnerVerticesPerPageSide] = value;
			m_changed = true;
		}

		uint32 Page::GetInnerColorAt(size_t x, size_t y) const
		{
			ASSERT(x < constants::InnerVerticesPerPageSide && y < constants::InnerVerticesPerPageSide);
			return m_innerColors[x + y * constants::InnerVerticesPerPageSide];
		}

		void Page::SetInnerColorAt(size_t x, size_t y, uint32 color)
		{
			ASSERT(x < constants::InnerVerticesPerPageSide && y < constants::InnerVerticesPerPageSide);
			m_innerColors[x + y * constants::InnerVerticesPerPageSide] = color;
			m_changed = true;
		}

		Vector3 Page::GetInnerNormalAt(size_t x, size_t y) const
		{
			ASSERT(x < constants::InnerVerticesPerPageSide && y < constants::InnerVerticesPerPageSide);
			Vector3 normal;
			DecodeNormalSNorm8(m_innerNormals[x + y * constants::InnerVerticesPerPageSide], normal.x, normal.y, normal.z);
			return normal;
		}

		void Page::SetInnerNormalAt(size_t x, size_t y, const Vector3& normal)
		{
			ASSERT(x < constants::InnerVerticesPerPageSide && y < constants::InnerVerticesPerPageSide);
			m_innerNormals[x + y * constants::InnerVerticesPerPageSide] = EncodeNormalSNorm8(normal.x, normal.y, normal.z);
			m_changed = true;
		}

		Vector3 Page::CalculateNormalAt(const uint32 x, const uint32 z)
		{
			const float scaling = static_cast<float>(constants::PageSize / static_cast<double>(constants::OuterVerticesPerPageSide));

		size_t offsX = m_x * (constants::OuterVerticesPerPageSide - 1);
		size_t offsY = m_z * (constants::OuterVerticesPerPageSide - 1);

		const size_t maxX = m_terrain.GetWidth() * (constants::OuterVerticesPerPageSide - 1);
		const size_t maxZ = m_terrain.GetHeight() * (constants::OuterVerticesPerPageSide - 1);

		// Get heights at current position and neighbors
		const float heightCenter = m_terrain.GetAt(offsX + x, offsY + z);
		
		// Sample all 8 neighbors for smooth normal calculation
		// Handle boundary conditions by clamping to valid range
		const float heightLeft = (x > 0) ? m_terrain.GetAt(offsX + x - 1, offsY + z) : heightCenter;
		const float heightRight = (x < maxX) ? m_terrain.GetAt(offsX + x + 1, offsY + z) : heightCenter;
		const float heightUp = (z > 0) ? m_terrain.GetAt(offsX + x, offsY + z - 1) : heightCenter;
		const float heightDown = (z < maxZ) ? m_terrain.GetAt(offsX + x, offsY + z + 1) : heightCenter;
		
		// Calculate normal using central differences (smoother than single triangle)
		// This averages the gradients from both sides
		const float dx = (heightRight - heightLeft) / (2.0f * scaling);
		const float dz = (heightDown - heightUp) / (2.0f * scaling);
		
		// Normal is perpendicular to the tangent plane
		// Cross product of tangent vectors gives the normal
		Vector3 norm(-dx, 1.0f, -dz);

			return norm;
		}

		Vector3 Page::GetTangentAt(const uint32 x, const uint32 z)
		{
			return CalculateTangentAt(x, z);
		}

		Vector3 Page::CalculateTangentAt(const uint32 x, const uint32 z)
		{
			size_t offsX = m_x * (constants::OuterVerticesPerPageSide - 1);
			size_t offsY = m_z * (constants::OuterVerticesPerPageSide - 1);

			int flip = 1;
			Vector3 here = m_terrain.GetVectorAt(x + offsX, z + offsY);
			Vector3 left = m_terrain.GetVectorAt(x + offsX - 1, z + offsY);
			if (left.x < 0.0f)
			{
				flip *= -1;
				left = m_terrain.GetVectorAt(x + offsX + 1, z + offsY);
			}

			left -= here;

			return (left * flip).NormalizedCopy();
		}

		bool Page::IsPrepared() const
		{
			return m_prepared;
		}

		bool Page::IsPreparing() const
		{
			return m_preparing;
		}

		bool Page::IsLoaded() const
		{
			return IsPrepared() && m_loaded;
		}

		bool Page::IsLoadable() const
		{
			return IsPrepared() && !IsLoaded();
		}

		const AABB& Page::GetBoundingBox() const
		{
			return m_boundingBox;
		}

		void Page::UpdateTileSelectionQuery()
		{
			for (const auto& tile : m_Tiles)
			{
				tile->SetQueryFlags(m_terrain.GetTileSceneQueryFlags());
			}
		}

		bool Page::Save()
		{
			auto file = AssetRegistry::CreateNewFile(GetPageFilename());
			if (!file)
			{
				ELOG("Failed to save changed tile " << m_x << "x" << m_z << ": Failed to create file!");
				return false;
			}

			io::StreamSink sink(*file);
			io::Writer writer(sink);

			uint32 version = 0x02;

			// File version chunk
			{
				ChunkWriter versionChunkWriter{ constants::VersionChunk, writer };
				writer << io::write<uint32>(version);
				versionChunkWriter.Finish();
			}

			// Materials
			{
				ChunkWriter materialChunkWriter{ constants::MaterialChunk, writer };
				writer << io::write<uint16>(m_materials.size());
				for (auto& material : m_materials)
				{
					if (material)
					{
						writer << io::write_dynamic_range<uint16>(material->GetName());
					}
					else
					{
						writer << io::write<uint16>(0);
					}
				}
				materialChunkWriter.Finish();
			}

			// Heightmap (outer grid)
			{
				ChunkWriter heightmapChunk{ constants::VertexChunk, writer };
				writer << io::write_range(m_heightmap);
				heightmapChunk.Finish();
			}

			// Inner heightmap (v2)
			{
				ChunkWriter innerHeightChunk{ constants::InnerVertexChunk, writer };
				writer << io::write_range(m_innerHeightmap);
				innerHeightChunk.Finish();
			}

			// (Encoded) Normals (outer grid)
			{
				ChunkWriter normalChunk{ constants::NormalChunk, writer };
				for (const auto& normal : m_normals)
				{
					writer.WritePOD(normal);
				}
				normalChunk.Finish();
			}

			// Inner (encoded) normals (v2)
			{
				ChunkWriter innerNormalChunk{ constants::InnerNormalChunk, writer };
				for (const auto& normal : m_innerNormals)
				{
					writer.WritePOD(normal);
				}
				innerNormalChunk.Finish();
			}

			// Layers
			{
				ChunkWriter layerChunk{ constants::LayerChunk, writer };
				writer << io::write_range(m_layers);
				layerChunk.Finish();
			}

			// Vertex shading (outer grid)
			{
				ChunkWriter colorsChunk{ constants::VertexShadingChunk, writer };
				writer << io::write_range(m_colors);
				colorsChunk.Finish();
			}

			// Inner vertex shading (v2)
			{
				ChunkWriter innerColorsChunk{ constants::InnerVertexShadingChunk, writer };
				writer << io::write_range(m_innerColors);
				innerColorsChunk.Finish();
			}

			// Zones
			{
				ChunkWriter areaChunk{ constants::AreaChunk, writer };
				for (const auto& zone : m_tileZones)
				{
					writer << io::write<uint32>(zone);
				}
				areaChunk.Finish();
			}

			sink.Flush();
			file.reset();

			m_changed = false;
			return false;
		}

		bool Page::ReadMCMTChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Read number of materials used
			uint16 numMaterials;
			if (!(reader >> io::read<uint16>(numMaterials)))
			{
				ELOG("Failed to read number of materials used in tile!");
				return false;
			}

			m_materials.clear();
			m_materials.reserve(numMaterials);

			for (uint16 i = 0; i < numMaterials; ++i)
			{
				String materialName;
				if (!(reader >> io::read_container<uint16>(materialName)))
				{
					ELOG("Failed to read material name from tile " << m_x << "x" << m_z << "!");
					return false;
				}

				if (materialName.empty())
				{
					m_materials.push_back(nullptr);
				}
				else
				{
					// Don't worry: MaterialManager has a cache system, so loading the same material multiple times is not a problem!
					m_materials.push_back(MaterialManager::Get().Load(materialName));
				}
			}

			return reader;
		}

		bool Page::ReadMCVTChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Read heightmap data
			if (!(reader >> io::read_range(m_heightmap)))
			{
				ELOG("Failed to read heightmap from tile " << m_x << "x" << m_z << "!");
				return false;
			}

			return reader;
		}

		bool Page::ReadMCIVChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			return reader >> io::read_range(m_innerHeightmap);
		}

		bool Page::ReadMCINChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			for (auto& normal : m_innerNormals)
			{
				reader.readPOD(normal);
				if (!reader)
				{
					ELOG("Failed to read inner normal from tile " << m_x << "x" << m_z << "!");
					return false;
				}
			}
			return true;
		}

		bool Page::ReadMCISChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			return reader >> io::read_range(m_innerColors);
		}

		// Legacy v1 chunk readers and resampling helpers
		namespace
		{
			constexpr uint32 kOldVerticesPerTileSide = 18;
			constexpr uint32 kOldVerticesPerPageSide = (kOldVerticesPerTileSide - 1) * terrain::constants::TilesPerPage + 1; // 273

			inline uint32 MapLegacyIndex(uint32 newIdx, uint32 newMax, uint32 oldMax)
			{
				if (newMax == 0)
				{
					return 0;
				}
				const double t = static_cast<double>(newIdx) / static_cast<double>(newMax);
				const double mapped = t * static_cast<double>(oldMax) + 0.5;
				const uint32 idx = static_cast<uint32>(mapped);
				return idx > oldMax ? oldMax : idx;
			}
		}

		bool Page::ReadMCVTChunkV1(io::Reader& reader, uint32 header, uint32 size)
		{
			std::vector<float> legacy;
			legacy.resize(kOldVerticesPerPageSide * kOldVerticesPerPageSide);
			if (!(reader >> io::read_range(legacy)))
			{
				ELOG("Failed to read legacy heightmap from tile " << m_x << "x" << m_z << "!");
				return false;
			}

			const uint32 newSide = constants::OuterVerticesPerPageSide;
			for (uint32 y = 0; y < newSide; ++y)
			{
				const uint32 oy = MapLegacyIndex(y, newSide - 1, kOldVerticesPerPageSide - 1);
				for (uint32 x = 0; x < newSide; ++x)
				{
					const uint32 ox = MapLegacyIndex(x, newSide - 1, kOldVerticesPerPageSide - 1);
					m_heightmap[x + y * newSide] = legacy[ox + oy * kOldVerticesPerPageSide];
				}
			}
			return true;
		}

		bool Page::ReadMCNMChunkV1(io::Reader& reader, uint32 header, uint32 size)
		{
			std::vector<EncodedNormal8> legacy;
			legacy.resize(kOldVerticesPerPageSide * kOldVerticesPerPageSide);
			for (auto& n : legacy)
			{
				reader.readPOD(n);
				if (!reader)
				{
					ELOG("Failed to read legacy normal from tile " << m_x << "x" << m_z << "!");
					return false;
				}
			}

			const uint32 newSide = constants::OuterVerticesPerPageSide;
			for (uint32 y = 0; y < newSide; ++y)
			{
				const uint32 oy = MapLegacyIndex(y, newSide - 1, kOldVerticesPerPageSide - 1);
				for (uint32 x = 0; x < newSide; ++x)
				{
					const uint32 ox = MapLegacyIndex(x, newSide - 1, kOldVerticesPerPageSide - 1);
					m_normals[x + y * newSide] = legacy[ox + oy * kOldVerticesPerPageSide];
				}
			}
			return true;
		}

		bool Page::ReadMCVSChunkV1(io::Reader& reader, uint32 header, uint32 size)
		{
			std::vector<uint32> legacy;
			legacy.resize(kOldVerticesPerPageSide * kOldVerticesPerPageSide);
			if (!(reader >> io::read_range(legacy)))
			{
				return false;
			}

			const uint32 newSide = constants::OuterVerticesPerPageSide;
			for (uint32 y = 0; y < newSide; ++y)
			{
				const uint32 oy = MapLegacyIndex(y, newSide - 1, kOldVerticesPerPageSide - 1);
				for (uint32 x = 0; x < newSide; ++x)
				{
					const uint32 ox = MapLegacyIndex(x, newSide - 1, kOldVerticesPerPageSide - 1);
					m_colors[x + y * newSide] = legacy[ox + oy * kOldVerticesPerPageSide];
				}
			}
			return true;
		}

		bool Page::ReadMCNMChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Read heightmap data
			for (auto& normal : m_normals)
			{
				reader.readPOD(normal);

				if (!reader)
				{
					ELOG("Failed to read normal from tile " << m_x << "x" << m_z << "!");
					return false;
				}
			}

			return reader;
		}

		bool Page::ReadMCLYChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Read heightmap data
			reader >> io::read_range(m_layers);
			return reader;
		}

		String Page::GetPageFilename() const
		{
			return m_terrain.GetBaseFileName() + "/" + std::to_string(m_x) + "_" + std::to_string(m_z) + ".tile";
		}

		void Page::NotifyTileMaterialChanged(const uint32 x, const uint32 y)
		{
			if (x >= constants::TilesPerPage || y >= constants::TilesPerPage)
			{
				return;
			}

			if (!m_loaded)
			{
				return;
			}

			const uint32 tileIndex = x + y * constants::TilesPerPage;
			ASSERT(tileIndex < m_materials.size());

			MaterialPtr material = m_Tiles(x, y)->GetBaseMaterial();
			if (material == m_terrain.GetDefaultMaterial())
			{
				// Default terrain material does not need to be set explicitly!
				m_materials[tileIndex] = nullptr;
			}
			else
			{
				m_materials[tileIndex] = material;
			}

			m_changed = true;
		}

		void Page::SetHeightAt(const unsigned int x, const unsigned int z, const float value)
		{
			if (x >= constants::OuterVerticesPerPageSide || z >= constants::OuterVerticesPerPageSide)
			{
				return;
			}

			m_heightmap[x + z * constants::OuterVerticesPerPageSide] = value;
			m_changed = true;
		}

		void Page::SetColorAt(size_t x, size_t y, uint32 color)
		{
			if (x >= constants::OuterVerticesPerPageSide || y >= constants::OuterVerticesPerPageSide)
			{
				return;
			}

			m_colors[x + y * constants::OuterVerticesPerPageSide] = color;
			m_changed = true;
		}

		void Page::SetLayerAt(const unsigned int x, const unsigned int z, const uint8 layer, const float value)
		{
			if (x >= constants::PixelsPerPage || z >= constants::PixelsPerPage)
			{
				return;
			}

			uint32& layers = m_layers[x + z * constants::PixelsPerPage];

			Vector4 v;
			v.x = ((layers >> 0) & 0xFF) / 255.0f; 
			v.y = ((layers >> 8) & 0xFF) / 255.0f;
			v.z = ((layers >> 16) & 0xFF) / 255.0f;
			v.w = ((layers >> 24) & 0xFF) / 255.0f;

			// Record the old sum (for informational purposes, or checks).
			float oldSum = v.x + v.y + v.z + v.w;

			// Set the chosen channel to newValue
			switch (layer)
			{
			case 0: v.x = value; break;
			case 1: v.y = value; break;
			case 2: v.z = value; break;
			case 3: v.w = value; break;
			default:
				// Handle error, unknown channel
				return;
			}

			// Compute the sum of all four *after* setting the chosen channel
			const float sumAfter = v.x + v.y + v.z + v.w;

			// If the sum is 0 (or extremely close to 0), we can�t scale. 
			// Handle that case gracefully.
			if (std::fabs(sumAfter) < 1e-6f)
			{
				// e.g. just set everything else to 0, or choose a fallback strategy.
				// We'll do a simple fallback here:
				v.x = (layer == 0) ? value : 0.f;
				v.y = (layer == 1) ? value : 0.f;
				v.z = (layer == 2) ? value : 0.f;
				v.w = (layer == 3) ? value : 0.f;

				layers = CalculateARGB(v);
				return;
			}

			// We want the total to be exactly 1. So we figure out the scale factor:
			const float scale = 1.0f / sumAfter;

			// Multiply the entire vector by this scale to force sum == 1
			v.x *= scale;
			v.y *= scale;
			v.z *= scale;
			v.w *= scale;
			layers = CalculateARGB(v);

			m_changed = true;
		}

		uint32 Page::GetArea(uint32 localTileX, uint32 localTileY) const
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return 0;
			}

			return m_tileZones[localTileX + localTileY * constants::TilesPerPage];
		}

		void Page::SetArea(uint32 localTileX, uint32 localTileY, uint32 area)
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return;
			}

			if (GetArea(localTileX, localTileY) == area)
			{
				return;
			}

			m_tileZones[localTileX + localTileY * constants::TilesPerPage] = area;
			m_changed = true;
		}

		bool Page::ReadMCVRChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			uint32 version;
			if (!(reader >> io::read<uint32>(version)))
			{
				ELOG("Failed to read tile format version!");
				return false;
			}

			const uint32 versionV1 = 0x01;
			const uint32 versionV2 = 0x02;
			if (version != versionV1 && version != versionV2)
			{
				ELOG("Unsupported file format version detected (detected " << log_hex_digit(version) << ", supported " << log_hex_digit(versionV1) << ", " << log_hex_digit(versionV2) << ")");
				return false;
			}

			m_ignoreUnhandledChunks = true;

			// Register chunk readers based on version
			AddChunkHandler(*constants::MaterialChunk, false, *this, &Page::ReadMCMTChunk);
			if (version == versionV1)
			{
				AddChunkHandler(*constants::VertexChunk, true, *this, &Page::ReadMCVTChunkV1);
				AddChunkHandler(*constants::NormalChunk, true, *this, &Page::ReadMCNMChunkV1);
				AddChunkHandler(*constants::LayerChunk, true, *this, &Page::ReadMCLYChunk);
				AddChunkHandler(*constants::AreaChunk, false, *this, &Page::ReadMCARChunk);
				AddChunkHandler(*constants::VertexShadingChunk, false, *this, &Page::ReadMCVSChunkV1);
			}
			else
			{
				AddChunkHandler(*constants::VertexChunk, true, *this, &Page::ReadMCVTChunk);
				AddChunkHandler(*constants::NormalChunk, true, *this, &Page::ReadMCNMChunk);
				// Inner v2 chunks
				AddChunkHandler(*constants::InnerVertexChunk, true, *this, &Page::ReadMCIVChunk);
				AddChunkHandler(*constants::InnerNormalChunk, true, *this, &Page::ReadMCINChunk);
				AddChunkHandler(*constants::InnerVertexShadingChunk, true, *this, &Page::ReadMCISChunk);
				AddChunkHandler(*constants::LayerChunk, true, *this, &Page::ReadMCLYChunk);
				AddChunkHandler(*constants::AreaChunk, false, *this, &Page::ReadMCARChunk);
				AddChunkHandler(*constants::VertexShadingChunk, false, *this, &Page::ReadMCVSChunk);
			}

			return reader;
		}

		bool Page::ReadMCARChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Read tile zones
			for (auto& zone : m_tileZones)
			{
				reader >> io::read<uint32>(zone);
				if (!reader)
				{
					ELOG("Failed to read tile zones from tile " << m_x << "x" << m_z << "!");
					return false;
				}
			}

			return reader;
		}

		bool Page::ReadMCVSChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			reader >> io::read_range(m_colors);
			return reader;
		}

		void Page::UpdateBoundingBox()
		{
			const Vector3 offset = Vector3(
				static_cast<float>(static_cast<double>(m_x - 32) * constants::PageSize),
				0.0f,
				static_cast<float>(static_cast<double>(m_z - 32) * constants::PageSize)
			);

			// Bounding box
			m_boundingBox = AABB(
				offset,
				offset + Vector3(constants::PageSize, 0.0f, constants::PageSize)
			);

			for (auto& tile : m_Tiles)
			{
				if (!tile)
				{
					continue;
				}

				const auto tileBounds = tile->GetWorldBoundingBox(true);
				m_boundingBox.Combine(tileBounds);
			}
		}

		void Page::UpdateMaterial()
		{

		}
	}
}
