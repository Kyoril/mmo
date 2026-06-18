
#include "page.h"

#include "terrain.h"
#include "scene_graph/scene.h"
#include "scene_graph/manual_render_object.h"
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
			static const ChunkMagic HoleChunk = MakeChunkMagic('LOHM');
			static const ChunkMagic WaterChunk = MakeChunkMagic('WCLM');     // legacy v1
			static const ChunkMagic WaterQuadChunk = MakeChunkMagic('QWCM'); // current v2
		}

		namespace
		{
			inline uint32 CalculateARGB(const Vector4 &val)
			{
				return (
					static_cast<uint32>(val.w * 255) << 24 |
					static_cast<uint32>(val.z * 255) << 16 |
					static_cast<uint32>(val.y * 255) << 8 |
					static_cast<uint32>(val.x * 255));
			}
		}
		Page::Page(Terrain &terrain, const int32 x, const int32 z)
			: m_terrain(terrain), m_x(x), m_z(z), m_preparing(false), m_prepared(false), m_loaded(false)
		{
			const auto offset = Vector3(
				static_cast<float>(static_cast<double>(m_x - 32) * constants::PageSize),
				0.0f,
				static_cast<float>(static_cast<double>(m_z - 32) * constants::PageSize));

			// Bounding box
			m_boundingBox = AABB(
				offset,
				offset + Vector3(constants::PageSize, 0.0f, constants::PageSize));

			m_pageNode = m_terrain.GetNode()->CreateChildSceneNode();
			m_pageNode->SetPosition(offset);
		}

		Page::~Page()
		{
			Unload();

			// Destroy the water render object before the scene node it is attached to
			if (m_waterRenderObject)
			{
				m_terrain.GetScene().DestroyManualRenderObject(*m_waterRenderObject);
				m_waterRenderObject = nullptr;
			}

			if (m_pageNode != nullptr)
			{
				m_terrain.GetScene().DestroySceneNode(*m_pageNode);
				m_pageNode = nullptr;
			}
		}

		bool Page::Prepare()
		{
			// A fresh load has been requested for this page (Prepare is always called from the
			// "page became available" path before the EnsurePageIsLoaded streaming loop starts).
			// Clear any leftover unload cancellation from a previous streaming cycle that was
			// aborted mid-load. Without this, the very first Load() of the new cycle would consume
			// the stale m_unloadRequested flag, return true early and stop the loop, leaving the
			// page prepared but empty (no tiles). The player would then fall through the ground and
			// the page would never finish loading until the next visibility toggle.
			m_unloadRequested = false;

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
			// Initialize hole map (one 64-bit value per tile, all zeros = no holes)
			m_holeMap.resize(constants::TilesPerPage * constants::TilesPerPage, 0);

			// Initialize water data (one entry per tile, defaults = no water)
			const size_t tileCount = constants::TilesPerPage * constants::TilesPerPage;
			m_waterQuadMasks.assign(tileCount, 0ULL);
			m_waterVertexHeights.assign(constants::OuterVerticesPerPageSide * constants::OuterVerticesPerPageSide, 0.0f);
			m_waterTypes.assign(tileCount, static_cast<uint8>(WaterType::None));

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

				io::StreamSource source{*file};
				io::Reader reader{source};
				if (!Read(reader))
				{
					ELOG("Failed to read page file '" << pageFileName << "'!");
					m_preparing = false;
					return false;
				}

					// If inner heights were not loaded from file (legacy page without IVCM chunk),
				// derive them from the outer vertex grid so the terrain renders correctly.
				if (!m_innerHeightmapFromFile)
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

					// Mark as changed so the derived inner data is persisted on the next save
					m_changed = true;
				}
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
					auto &tile = m_Tiles(i, j);

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

				// Create or re-attach the water render object (must be on main thread)
				if (!m_waterRenderObject)
				{
					const String waterName = "Water_" + std::to_string(m_x) + "_" + std::to_string(m_z);
					m_waterRenderObject = m_terrain.GetScene().CreateManualRenderObject(waterName);
					m_waterRenderObject->SetQueryFlags(0);
					m_waterRenderObject->SetCastShadows(false);
				}

				if (m_waterRenderObject && !m_waterRenderObject->GetParentSceneNode())
				{
					m_pageNode->AttachObject(*m_waterRenderObject);
				}

				// Honour the terrain-wide water visibility setting for freshly streamed pages.
				if (m_waterRenderObject)
				{
					m_waterRenderObject->SetVisible(m_terrain.IsWaterVisible());
				}

				RebuildWaterMesh();
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

			for (const auto &tile : m_Tiles)
			{
				if (!tile)
				{
					continue;
				}

				tile->DetachFromParent();
			}

			// Detach water render object so it is invisible while the page is unloaded
			if (m_waterRenderObject)
			{
				m_waterRenderObject->DetachFromParent();
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

			// Destroy the water render object (GPU resource) along with the page data
			if (m_waterRenderObject)
			{
				m_terrain.GetScene().DestroyManualRenderObject(*m_waterRenderObject);
				m_waterRenderObject = nullptr;
			}

			// Unload all loaded data so we will have to reload it again later
			m_heightmap.clear();
			m_normals.clear();
			m_materials.clear();
			m_layers.clear();
			m_waterQuadMasks.clear();
			m_waterVertexHeights.clear();
			m_waterTypes.clear();
			m_waterMaterialName.clear();

			m_prepared = false;
			m_preparing = false;
		}

		Tile *Page::GetTile(const uint32 x, const uint32 y)
		{
			if (x >= m_Tiles.width() ||
				y >= m_Tiles.height())
			{
				return nullptr;
			}

			return m_Tiles(x, y).get();
		}

		Tile *Page::GetTileAt(float x, float z)
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

		Terrain &Page::GetTerrain()
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
					Tile *pTile = GetTile(x, z);
					if (pTile != nullptr)
					{
						if (normalsOnly)
						{
							// pTile->UpdateVertexNormals();
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

			if (toTileX >= constants::TilesPerPage)
				toTileX = constants::TilesPerPage - 1;
			if (toTileZ >= constants::TilesPerPage)
				toTileZ = constants::TilesPerPage - 1;

			for (unsigned int x = fromTileX; x <= toTileX; x++)
			{
				for (unsigned int z = fromTileZ; z <= toTileZ; z++)
				{
					Tile *pTile = GetTile(x, z);
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

			// Recalculate inner normal by averaging surrounding outer normals
			// Inner vertex (x,y) corresponds to the center of outer quad (x,y) to (x+1,y+1)
			const Vector3 n00 = GetNormalAt(x, y);
			const Vector3 n10 = GetNormalAt(x + 1, y);
			const Vector3 n01 = GetNormalAt(x, y + 1);
			const Vector3 n11 = GetNormalAt(x + 1, y + 1);
			const Vector3 avgNormal = ((n00 + n10 + n01 + n11) * 0.25f).NormalizedCopy();
			SetInnerNormalAt(x, y, avgNormal);

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

		void Page::SetInnerNormalAt(size_t x, size_t y, const Vector3 &normal)
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
			norm.Normalize();

			m_normals[x + z * constants::OuterVerticesPerPageSide] = EncodeNormalSNorm8(norm.x, norm.y, norm.z);
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

		const AABB &Page::GetBoundingBox() const
		{
			return m_boundingBox;
		}

		void Page::UpdateTileSelectionQuery()
		{
			for (const auto &tile : m_Tiles)
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
				ChunkWriter versionChunkWriter{constants::VersionChunk, writer};
				writer << io::write<uint32>(version);
				versionChunkWriter.Finish();
			}

			// Materials
			{
				ChunkWriter materialChunkWriter{constants::MaterialChunk, writer};
				writer << io::write<uint16>(m_materials.size());
				for (auto &material : m_materials)
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
				ChunkWriter heightmapChunk{constants::VertexChunk, writer};
				writer << io::write_range(m_heightmap);
				heightmapChunk.Finish();
			}

			// Inner heightmap (v2)
			{
				ChunkWriter innerHeightChunk{constants::InnerVertexChunk, writer};
				writer << io::write_range(m_innerHeightmap);
				innerHeightChunk.Finish();
			}

			// (Encoded) Normals (outer grid)
			{
				ChunkWriter normalChunk{constants::NormalChunk, writer};
				for (const auto &normal : m_normals)
				{
					writer.WritePOD(normal);
				}
				normalChunk.Finish();
			}

			// Inner (encoded) normals (v2)
			{
				ChunkWriter innerNormalChunk{constants::InnerNormalChunk, writer};
				for (const auto &normal : m_innerNormals)
				{
					writer.WritePOD(normal);
				}
				innerNormalChunk.Finish();
			}

			// Layers
			{
				ChunkWriter layerChunk{constants::LayerChunk, writer};
				writer << io::write_range(m_layers);
				layerChunk.Finish();
			}

			// Vertex shading (outer grid)
			{
				ChunkWriter colorsChunk{constants::VertexShadingChunk, writer};
				writer << io::write_range(m_colors);
				colorsChunk.Finish();
			}

			// Inner vertex shading (v2)
			{
				ChunkWriter innerColorsChunk{constants::InnerVertexShadingChunk, writer};
				writer << io::write_range(m_innerColors);
				innerColorsChunk.Finish();
			}

			// Hole data (only save tiles with holes)
			{
				// Count tiles with holes
				std::vector<std::pair<uint16, uint64>> tilesWithHoles;
				for (uint16 i = 0; i < m_holeMap.size(); ++i)
				{
					if (m_holeMap[i] != 0)
					{
						tilesWithHoles.emplace_back(i, m_holeMap[i]);
					}
				}

				// Only write chunk if there are any holes
				if (!tilesWithHoles.empty())
				{
					ChunkWriter holeChunk{constants::HoleChunk, writer};
					writer << io::write<uint16>(static_cast<uint16>(tilesWithHoles.size()));

					for (const auto &tilePair : tilesWithHoles)
					{
						writer << io::write<uint16>(tilePair.first);
						writer << io::write<uint64>(tilePair.second);
					}

					holeChunk.Finish();
				}
			}

			// Water quad data — new format: sparse quad masks + shared vertex heights
			{
				std::vector<uint16> waterTileIndices;
				for (uint16 i = 0; i < static_cast<uint16>(m_waterQuadMasks.size()); ++i)
				{
					if (m_waterQuadMasks[i] != 0)
					{
						waterTileIndices.push_back(i);
					}
				}

				if (!waterTileIndices.empty() || !m_waterMaterialName.empty())
				{
					ChunkWriter waterChunkWriter{constants::WaterQuadChunk, writer};
					writer << io::write<uint16>(static_cast<uint16>(waterTileIndices.size()));
					for (const uint16 idx : waterTileIndices)
					{
						writer << io::write<uint16>(idx)
						       << io::write<uint8>(m_waterTypes[idx])
						       << io::write<uint64>(m_waterQuadMasks[idx]);
					}
					// Write all page-level water vertex heights (129x129)
					for (const float h : m_waterVertexHeights)
					{
						writer << io::write<float>(h);
					}
					writer << io::write_dynamic_range<uint16>(m_waterMaterialName);
					waterChunkWriter.Finish();
				}
			}

			// Zones
			{
				ChunkWriter areaChunk{constants::AreaChunk, writer};
				for (const auto &zone : m_tileZones)
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

		bool Page::ReadMCMTChunk(io::Reader &reader, uint32 header, uint32 size)
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

		bool Page::ReadMCVTChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			// Read heightmap data
			if (!(reader >> io::read_range(m_heightmap)))
			{
				ELOG("Failed to read heightmap from tile " << m_x << "x" << m_z << "!");
				return false;
			}

			return reader;
		}

		bool Page::ReadMCIVChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			const bool ok = reader >> io::read_range(m_innerHeightmap);
			if (ok) m_innerHeightmapFromFile = true;
			return ok;
		}

		bool Page::ReadMCINChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			for (auto &normal : m_innerNormals)
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

		bool Page::ReadMCISChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			return reader >> io::read_range(m_innerColors);
		}

		bool Page::ReadMCHLChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			// Read hole data: only non-zero hole maps are stored
			// Format: uint16 count, then for each: uint16 tileIndex, uint64 holeMap
			uint16 holeCount;
			if (!(reader >> io::read<uint16>(holeCount)))
			{
				ELOG("Failed to read hole count from tile " << m_x << "x" << m_z << "!");
				return false;
			}

			// Clear existing hole data
			m_holeMap.clear();
			m_holeMap.resize(constants::TilesPerPage * constants::TilesPerPage, 0);

			// Read each tile's hole map
			for (uint16 i = 0; i < holeCount; ++i)
			{
				uint16 tileIndex;
				uint64 holeMap;

				if (!(reader >> io::read<uint16>(tileIndex)))
				{
					ELOG("Failed to read hole tile index from tile " << m_x << "x" << m_z << "!");
					return false;
				}

				if (!(reader >> io::read<uint64>(holeMap)))
				{
					ELOG("Failed to read hole map from tile " << m_x << "x" << m_z << "!");
					return false;
				}

				if (tileIndex < m_holeMap.size())
				{
					m_holeMap[tileIndex] = holeMap;
				}
			}

			return true;
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

		bool Page::ReadMCVTChunkV1(io::Reader &reader, uint32 header, uint32 size)
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

		bool Page::ReadMCNMChunkV1(io::Reader &reader, uint32 header, uint32 size)
		{
			std::vector<EncodedNormal8> legacy;
			legacy.resize(kOldVerticesPerPageSide * kOldVerticesPerPageSide);
			for (auto &n : legacy)
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

		bool Page::ReadMCVSChunkV1(io::Reader &reader, uint32 header, uint32 size)
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

		bool Page::ReadMCNMChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			// Read heightmap data
			for (auto &normal : m_normals)
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

		bool Page::ReadMCLYChunk(io::Reader &reader, uint32 header, uint32 size)
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

			// Recalculate normal at this vertex since height changed
			CalculateNormalAt(x, z);

			// Also recalculate normals for neighboring vertices as they depend on this vertex's height
			// This ensures smooth normal transitions across terrain
			if (x > 0)
			{
				CalculateNormalAt(x - 1, z);
			}
			if (x < constants::OuterVerticesPerPageSide - 1)
			{
				CalculateNormalAt(x + 1, z);
			}
			if (z > 0)
			{
				CalculateNormalAt(x, z - 1);
			}
			if (z < constants::OuterVerticesPerPageSide - 1)
			{
				CalculateNormalAt(x, z + 1);
			}
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

			uint32 &layers = m_layers[x + z * constants::PixelsPerPage];

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
			case 0:
				v.x = value;
				break;
			case 1:
				v.y = value;
				break;
			case 2:
				v.z = value;
				break;
			case 3:
				v.w = value;
				break;
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

		bool Page::IsHole(uint32 localTileX, uint32 localTileY, uint32 innerX, uint32 innerY) const
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage ||
				innerX >= constants::InnerVerticesPerTileSide || innerY >= constants::InnerVerticesPerTileSide)
			{
				return false;
			}

			const uint32 tileIndex = localTileX + localTileY * constants::TilesPerPage;
			const uint32 bitIndex = innerX + innerY * constants::InnerVerticesPerTileSide;
			const uint64 holeMask = m_holeMap[tileIndex];

			return (holeMask & (1ULL << bitIndex)) != 0;
		}

		void Page::SetHole(uint32 localTileX, uint32 localTileY, uint32 innerX, uint32 innerY, bool isHole)
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage ||
				innerX >= constants::InnerVerticesPerTileSide || innerY >= constants::InnerVerticesPerTileSide)
			{
				return;
			}

			const uint32 tileIndex = localTileX + localTileY * constants::TilesPerPage;
			const uint32 bitIndex = innerX + innerY * constants::InnerVerticesPerTileSide;
			const uint64 bitMask = 1ULL << bitIndex;

			if (isHole)
			{
				m_holeMap[tileIndex] |= bitMask;
			}
			else
			{
				m_holeMap[tileIndex] &= ~bitMask;
			}

			m_changed = true;

			// Update the tile's index buffer to reflect the hole change
			if (Tile *tile = GetTile(localTileX, localTileY))
			{
				tile->UpdateTerrain(0, 0, constants::OuterVerticesPerTileSide - 1, constants::OuterVerticesPerTileSide - 1);
			}
		}

		uint64 Page::GetTileHoleMap(uint32 localTileX, uint32 localTileY) const
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return 0;
			}

			return m_holeMap[localTileX + localTileY * constants::TilesPerPage];
		}

		uint64 Page::GetWaterQuadMask(uint32 localTileX, uint32 localTileY) const
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return 0;
			}
			return m_waterQuadMasks[localTileX + localTileY * constants::TilesPerPage];
		}

		void Page::SetWaterQuadMask(uint32 localTileX, uint32 localTileY, uint64 mask)
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return;
			}
			m_waterQuadMasks[localTileX + localTileY * constants::TilesPerPage] = mask;
			m_changed = true;
		}

		void Page::SetWaterQuadBit(uint32 localTileX, uint32 localTileY, uint32 qx, uint32 qz, bool hasWater)
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage ||
				qx >= 8 || qz >= 8)
			{
				return;
			}

			const uint32 tileIdx = localTileX + localTileY * constants::TilesPerPage;
			const uint64 bit = 1ULL << (qx + qz * 8);

			if (hasWater)
			{
				m_waterQuadMasks[tileIdx] |= bit;
			}
			else
			{
				m_waterQuadMasks[tileIdx] &= ~bit;
			}

			m_changed = true;
		}

		float Page::GetWaterVertexHeight(uint32 pageVertX, uint32 pageVertZ) const
		{
			if (pageVertX >= constants::OuterVerticesPerPageSide || pageVertZ >= constants::OuterVerticesPerPageSide)
			{
				return 0.0f;
			}
			return m_waterVertexHeights[pageVertX + pageVertZ * constants::OuterVerticesPerPageSide];
		}

		void Page::SetWaterVertexHeight(uint32 pageVertX, uint32 pageVertZ, float height)
		{
			if (pageVertX >= constants::OuterVerticesPerPageSide || pageVertZ >= constants::OuterVerticesPerPageSide)
			{
				return;
			}
			m_waterVertexHeights[pageVertX + pageVertZ * constants::OuterVerticesPerPageSide] = height;
			m_changed = true;
		}

		WaterType Page::GetWaterType(uint32 localTileX, uint32 localTileY) const
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return WaterType::None;
			}
			return static_cast<WaterType>(m_waterTypes[localTileX + localTileY * constants::TilesPerPage]);
		}

		void Page::SetWaterType(uint32 localTileX, uint32 localTileY, WaterType type)
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return;
			}
			m_waterTypes[localTileX + localTileY * constants::TilesPerPage] = static_cast<uint8>(type);
			m_changed = true;
		}

		bool Page::HasWater(uint32 localTileX, uint32 localTileY) const
		{
			return GetWaterQuadMask(localTileX, localTileY) != 0;
		}

		void Page::ClearWater(uint32 localTileX, uint32 localTileY)
		{
			if (localTileX >= constants::TilesPerPage || localTileY >= constants::TilesPerPage)
			{
				return;
			}
			const uint32 idx = localTileX + localTileY * constants::TilesPerPage;
			m_waterQuadMasks[idx] = 0;
			m_waterTypes[idx] = static_cast<uint8>(WaterType::None);
			m_changed = true;
		}

		void Page::SetWaterMaterialName(const String& name)
		{
			m_waterMaterialName = name;
			m_changed = true;
		}

		void Page::RebuildWaterMesh()
		{
			if (!m_waterRenderObject)
			{
				return;
			}

			m_waterRenderObject->Clear();

			// Check if any tile has active water quads
			bool hasWater = false;
			for (const uint64 mask : m_waterQuadMasks)
			{
				if (mask != 0)
				{
					hasWater = true;
					break;
				}
			}

			if (!hasWater)
			{
				return;
			}

			// Resolve material. In minimap mode we deliberately ignore the assigned (translucent)
			// water material: it samples the scene depth/refraction textures which are not bound
			// during minimap generation and would render as garbage or fully transparent. Instead
			// we use an opaque, unlit vertex-colour material so water shows up as a solid blue area.
			MaterialPtr material;
			if (m_minimapWaterMode)
			{
				material = MaterialManager::Get().Load("Editor/MinimapWater.hmat");
			}
			else if (!m_waterMaterialName.empty())
			{
				material = MaterialManager::Get().Load(m_waterMaterialName);
			}
			if (!material)
			{
				material = MaterialManager::Get().Load("Editor/Wireframe.hmat");
			}

			// Vertex colour applied to every water quad. The normal water material ignores this
			// (it samples textures), but the minimap material renders it directly: opaque blue.
			const uint32 waterColor = m_minimapWaterMode ? 0xFF3A6EA5u : 0xAAFFFFFFu;
			if (!material)
			{
				return;
			}

			auto op = m_waterRenderObject->AddTriangleListOperation(material);

			// Each outer vertex step in local page space: TileSize / 8 sub-quads per tile side
			constexpr float quadSize = static_cast<float>(constants::TileSize) / 8.0f;
			constexpr uint32 pvSide = constants::OuterVerticesPerPageSide;

			// World offset used only for UV tiling so texture is continuous across pages
			const float worldOffsetX = static_cast<float>((m_x - 32) * constants::PageSize);
			const float worldOffsetZ = static_cast<float>((m_z - 32) * constants::PageSize);
			constexpr float uvScale = 1.0f / 16.0f;

			for (uint32 tz = 0; tz < constants::TilesPerPage; ++tz)
			{
				for (uint32 tx = 0; tx < constants::TilesPerPage; ++tx)
				{
					const uint64 mask = m_waterQuadMasks[tx + tz * constants::TilesPerPage];
					if (mask == 0)
					{
						continue;
					}

					for (uint32 qz = 0; qz < 8; ++qz)
					{
						for (uint32 qx = 0; qx < 8; ++qx)
						{
							if (!(mask & (1ULL << (qx + qz * 8))))
							{
								continue;
							}

							const uint32 pvx0 = tx * 8 + qx;
							const uint32 pvz0 = tz * 8 + qz;

							const float yTL = m_waterVertexHeights[ pvx0      + pvz0      * pvSide];
							const float yTR = m_waterVertexHeights[(pvx0 + 1) + pvz0      * pvSide];
							const float yBL = m_waterVertexHeights[ pvx0      + (pvz0+1)  * pvSide];
							const float yBR = m_waterVertexHeights[(pvx0 + 1) + (pvz0+1)  * pvSide];

							const float lx1 = pvx0       * quadSize;
							const float lz1 = pvz0       * quadSize;
							const float lx2 = (pvx0 + 1) * quadSize;
							const float lz2 = (pvz0 + 1) * quadSize;

							const float u1 = (worldOffsetX + lx1) * uvScale;
							const float u2 = (worldOffsetX + lx2) * uvScale;
							const float v1 = (worldOffsetZ + lz1) * uvScale;
							const float v2 = (worldOffsetZ + lz2) * uvScale;

							const Vector3 vTL(lx1, yTL, lz1);
							const Vector3 vTR(lx2, yTR, lz1);
							const Vector3 vBR(lx2, yBR, lz2);
							const Vector3 vBL(lx1, yBL, lz2);

							// Top face (CCW winding = front-facing from above)
							{
								auto& t1 = op->AddTriangle(vTL, vBL, vTR);
								t1.SetUV(0, u1, v1); t1.SetUV(1, u1, v2); t1.SetUV(2, u2, v1);
								t1.SetColor(waterColor);

								auto& t2 = op->AddTriangle(vTR, vBL, vBR);
								t2.SetUV(0, u2, v1); t2.SetUV(1, u1, v2); t2.SetUV(2, u2, v2);
								t2.SetColor(waterColor);
							}

							// Bottom face (reversed winding so water is visible from below)
							{
								auto& t3 = op->AddTriangle(vTR, vBL, vTL);
								t3.SetUV(0, u2, v1); t3.SetUV(1, u1, v2); t3.SetUV(2, u1, v1);
								t3.SetColor(waterColor);

								auto& t4 = op->AddTriangle(vBR, vBL, vTR);
								t4.SetUV(0, u2, v2); t4.SetUV(1, u1, v2); t4.SetUV(2, u2, v1);
								t4.SetColor(waterColor);
							}
						}
					}
				}
			}
		}

		void Page::SetWaterVisible(const bool visible)
		{
			if (m_waterRenderObject)
			{
				m_waterRenderObject->SetVisible(visible);
			}
		}

		void Page::SetMinimapWaterMode(bool enabled)
		{
			if (m_minimapWaterMode == enabled)
			{
				return;
			}

			m_minimapWaterMode = enabled;

			// Rebuild so the water geometry picks up the minimap (or normal) material and colour.
			RebuildWaterMesh();
		}

		bool Page::ReadMCWLChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Legacy v1 format: per-tile flat height + type.
			// Convert to new format: all 64 quads present, flat vertex heights.
			uint16 count;
			if (!(reader >> io::read<uint16>(count)))
			{
				ELOG("Failed to read water tile count from page " << m_x << "x" << m_z);
				return false;
			}

			for (uint16 i = 0; i < count; ++i)
			{
				uint16 tileIndex;
				float  height;
				uint8  type;

				if (!(reader >> io::read<uint16>(tileIndex) >> io::read<float>(height) >> io::read<uint8>(type)))
				{
					ELOG("Failed to read water tile entry from page " << m_x << "x" << m_z);
					return false;
				}

				if (tileIndex < static_cast<uint16>(m_waterQuadMasks.size()))
				{
					// Convert: all 64 quads present, uniform flat height across 9x9 vertices
					m_waterQuadMasks[tileIndex] = 0xFFFFFFFFFFFFFFFFULL;
					m_waterTypes[tileIndex]     = type;

					const uint32 tx = tileIndex % constants::TilesPerPage;
					const uint32 tz = tileIndex / constants::TilesPerPage;
					for (uint32 vz = 0; vz <= 8; ++vz)
					{
						for (uint32 vx = 0; vx <= 8; ++vx)
						{
							const uint32 pvx = tx * 8 + vx;
							const uint32 pvz = tz * 8 + vz;
							if (pvx < constants::OuterVerticesPerPageSide && pvz < constants::OuterVerticesPerPageSide)
							{
								m_waterVertexHeights[pvx + pvz * constants::OuterVerticesPerPageSide] = height;
							}
						}
					}
				}
			}

			// Optional material name
			if (reader)
			{
				reader >> io::read_container<uint16>(m_waterMaterialName);
			}

			return reader;
		}

		bool Page::ReadMCWQChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// New v2 water format: sparse quad masks + shared 129x129 vertex heights
			uint16 count;
			if (!(reader >> io::read<uint16>(count)))
			{
				ELOG("Failed to read water quad tile count from page " << m_x << "x" << m_z);
				return false;
			}

			for (uint16 i = 0; i < count; ++i)
			{
				uint16 tileIndex;
				uint8  type;
				uint64 mask;

				if (!(reader >> io::read<uint16>(tileIndex) >> io::read<uint8>(type) >> io::read<uint64>(mask)))
				{
					ELOG("Failed to read water quad tile entry from page " << m_x << "x" << m_z);
					return false;
				}

				if (tileIndex < static_cast<uint16>(m_waterQuadMasks.size()))
				{
					m_waterQuadMasks[tileIndex] = mask;
					m_waterTypes[tileIndex]     = type;
				}
			}

			// Read all page-level water vertex heights (129x129)
			for (float& h : m_waterVertexHeights)
			{
				if (!(reader >> io::read<float>(h)))
				{
					break;
				}
			}

			// Optional material name
			if (reader)
			{
				reader >> io::read_container<uint16>(m_waterMaterialName);
			}

			return reader;
		}

		bool Page::ReadMCVRChunk(io::Reader &reader, uint32 header, uint32 size)
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
				// Inner v2 chunks (optional – absent in files saved before inner-vertex support was added)
				AddChunkHandler(*constants::InnerVertexChunk, false, *this, &Page::ReadMCIVChunk);
				AddChunkHandler(*constants::InnerNormalChunk, false, *this, &Page::ReadMCINChunk);
				AddChunkHandler(*constants::InnerVertexShadingChunk, false, *this, &Page::ReadMCISChunk);
				AddChunkHandler(*constants::HoleChunk, false, *this, &Page::ReadMCHLChunk);
				AddChunkHandler(*constants::LayerChunk, true, *this, &Page::ReadMCLYChunk);
				AddChunkHandler(*constants::AreaChunk, false, *this, &Page::ReadMCARChunk);
				AddChunkHandler(*constants::VertexShadingChunk, false, *this, &Page::ReadMCVSChunk);
				// Water chunks (optional — absent in files without liquid data)
				AddChunkHandler(*constants::WaterChunk,     false, *this, &Page::ReadMCWLChunk); // legacy
				AddChunkHandler(*constants::WaterQuadChunk, false, *this, &Page::ReadMCWQChunk); // current
			}

			return reader;
		}

		bool Page::ReadMCARChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			// Read tile zones
			for (auto &zone : m_tileZones)
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

		bool Page::ReadMCVSChunk(io::Reader &reader, uint32 header, uint32 size)
		{
			reader >> io::read_range(m_colors);
			return reader;
		}

		void Page::UpdateBoundingBox()
		{
			const Vector3 offset = Vector3(
				static_cast<float>(static_cast<double>(m_x - 32) * constants::PageSize),
				0.0f,
				static_cast<float>(static_cast<double>(m_z - 32) * constants::PageSize));

			// Bounding box
			m_boundingBox = AABB(
				offset,
				offset + Vector3(constants::PageSize, 0.0f, constants::PageSize));

			for (auto &tile : m_Tiles)
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
