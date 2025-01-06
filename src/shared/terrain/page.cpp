
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
		}

		Page::Page(Terrain& terrain, int32 x, int32 z)
			: m_terrain(terrain)
			, m_x(x)
			, m_z(z)
			, m_preparing(false)
			, m_prepared(false)
			, m_loaded(false)
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
			if (IsPrepared() || IsPreparing()) {
				return true;
			}

			m_preparing = true;

			m_heightmap.resize(constants::VerticesPerPage * constants::VerticesPerPage, 0.0f);
			m_normals.resize(constants::VerticesPerPage * constants::VerticesPerPage, Vector3::UnitY);
			//m_tangents.resize(constants::VerticesPerPage * constants::VerticesPerPage, Vector3::UnitZ);
			m_materials.resize(constants::TilesPerPage * constants::TilesPerPage, nullptr);
			m_layers.resize(constants::VerticesPerPage * constants::VerticesPerPage, Vector4(1.0f, 0.0f, 0.0f, 0.0f));

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

				m_changed = true;
			}
			else
			{
				WLOG("Terrain page file '" << pageFileName << "' is missing, page will be initialized as blank tile");
				m_changed = true;
			}

			m_prepared = true;
			m_preparing = false;

			return true;
		}

		bool Page::IsValid() const noexcept
		{
			return ChunkReader::IsValid();
		}

		bool Page::OnReadFinished() noexcept
		{
			return ChunkReader::OnReadFinished();
		}

		void Page::Load()
		{
			if (!IsLoadable())
			{
				return;
			}

			// Build file name
			std::stringstream stream;
			stream << m_terrain.GetBaseFileName() << "_" << std::setfill('0') << std::setw(2) << m_x << "_" << std::setfill('0') << std::setw(2) << m_z;

			// Get filename
			const String filename = stream.str();

			String pageBaseName = "Page_" + std::to_string(m_x) + "_" + std::to_string(m_z);

			m_Tiles = TileGrid(constants::TilesPerPage, constants::TilesPerPage);
			for (unsigned int i = 0; i < constants::TilesPerPage; i++)
			{
				for (unsigned int j = 0; j < constants::TilesPerPage; j++)
				{
					String tileName = pageBaseName + "_Tile_" + std::to_string(i) + "_" + std::to_string(j);
					auto& tile = m_Tiles(i, j);
					tile = std::make_unique<Tile>(tileName, *this, i * (constants::VerticesPerTile - 1), j * (constants::VerticesPerTile - 1));

					// Setup tile material assignment
					if (const int tileIndex = i + j * constants::TilesPerPage; tileIndex < m_materials.size())
					{
						tile->SetMaterial(m_materials[tileIndex]);
					}

					// Ensure tile respects selection query
					tile->SetQueryFlags(m_terrain.GetTileSceneQueryFlags());
					m_pageNode->AttachObject(*tile);
				}
			}

			m_loaded = true;
			UpdateBoundingBox();
		}

		void Page::Unload()
		{
			for (const auto& tile : m_Tiles)
			{
				tile->DetachFromParent();
			}

			m_loaded = false;
			m_Tiles.clear();
		}

		Tile* Page::GetTile(uint32 x, uint32 y)
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

		float Page::GetHeightAt(size_t x, size_t y) const
		{
			if (!IsPrepared())
			{
				return 0.0f;
			}

			if (x >= constants::VerticesPerPage ||
				y >= constants::VerticesPerPage)
			{
				return 0.0f;
			}

			return m_heightmap[x + y * constants::VerticesPerPage];
		}

		const Vector4& Page::GetLayersAt(size_t x, size_t y) const
		{
			static Vector4 s_none;

			if (!IsPrepared())
			{
				return s_none;
			}

			if (x >= constants::VerticesPerPage ||
				y >= constants::VerticesPerPage)
			{
				return s_none;
			}

			const size_t index = x + y * constants::VerticesPerPage;
			ASSERT(index < m_layers.size());
			return m_layers[index];
		}

		float Page::GetSmoothHeightAt(float x, float y) const
		{
			// scale down
			float scale = constants::PageSize / static_cast<float>(constants::VerticesPerPage - 1);
			x /= scale;
			y /= scale;

			// retrieve height from heightmap via bilinear interpolation
			size_t xi = (size_t)x, zi = (size_t)y;
			float xpct = x - xi, zpct = y - zi;
			if (xi == constants::VerticesPerPage - 1)
			{
				// one too far
				--xi;
				xpct = 1.0f;
			}
			if (zi == constants::VerticesPerPage - 1)
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

		void Page::UpdateTiles(int fromX, int fromZ, int toX, int toZ, bool normalsOnly)
		{
			if (!m_loaded)
			{
				return;
			}

			unsigned int fromTileX = fromX / (constants::VerticesPerTile - 1);
			unsigned int fromTileZ = fromZ / (constants::VerticesPerTile - 1);
			unsigned int toTileX = toX / (constants::VerticesPerTile - 1);
			unsigned int toTileZ = toZ / (constants::VerticesPerTile - 1);

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

		Vector3 Page::GetVectorFromPoint(int x, int z)
		{
			return Vector3();
		}

		const Vector3& Page::GetNormalAt(uint32 x, uint32 z)
		{
			ASSERT(x < constants::VerticesPerPage && z < constants::VerticesPerPage);
			return m_normals[x + z * constants::VerticesPerPage];
		}

		Vector3 Page::CalculateNormalAt(uint32 x, uint32 z)
		{
			const float scaling = static_cast<float>(constants::PageSize / static_cast<double>(constants::VerticesPerPage));
			float flip = 1.0f;

			size_t offsX = m_x * (constants::VerticesPerPage - 1);
			size_t offsY = m_z * (constants::VerticesPerPage - 1);

			Vector3 here(static_cast<float>(x) * scaling, m_terrain.GetAt(offsX + x, offsY + z), static_cast<float>(z) * scaling);

			Vector3 right(static_cast<float>(x + 1) * scaling, m_terrain.GetAt(offsX + x + 1, offsY + z), static_cast<float>(z * scaling));
			if (x >= m_terrain.GetWidth() * (constants::VerticesPerPage - 1) + 1)
			{
				right.y = here.y;
				flip = -1.0f;
			}

			Vector3 down(static_cast<float>(x) * scaling, m_terrain.GetAt(offsX + x, offsY + z + 1), static_cast<float>(z + 1) * scaling);
			if (z >= m_terrain.GetHeight() * (constants::VerticesPerPage - 1) + 1)
			{
				down.z = here.y;
				flip = -1.0f;
			}

			down -= here;
			here -= right;

			Vector3 norm = here.Cross(down);
			norm.y *= flip;
			norm.Normalize();

			m_normals[x + z * constants::VerticesPerPage] = norm;

			return norm;
		}

		Vector3 Page::GetTangentAt(uint32 x, uint32 z)
		{
			return CalculateTangentAt(x, z);
		}

		Vector3 Page::CalculateTangentAt(uint32 x, uint32 z)
		{
			size_t offsX = m_x * (constants::VerticesPerPage - 1);
			size_t offsY = m_z * (constants::VerticesPerPage - 1);

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

			uint32 version = 0x01;

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

			// Heightmap
			{
				ChunkWriter heightmapChunk{ constants::VertexChunk, writer };
				writer << io::write_range(m_heightmap);
				heightmapChunk.Finish();
			}

			// (Encoded) Normals
			{
				ChunkWriter normalChunk{ constants::NormalChunk, writer };
				for (const auto& normal : m_normals)
				{
					auto encodedNormal = EncodeNormalSNorm8(normal.x, normal.y, normal.z);
					writer.WritePOD(encodedNormal);
				}
				normalChunk.Finish();
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

		bool Page::ReadMCNMChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			// Read heightmap data
			for (auto& normal : m_normals)
			{
				EncodedNormal8 encodedNormal;
				reader.readPOD(encodedNormal);

				if (!reader)
				{
					ELOG("Failed to read normal from tile " << m_x << "x" << m_z << "!");
					return false;
				}

				DecodeNormalSNorm8(encodedNormal, normal.x, normal.y, normal.z);
			}

			return reader;
		}

		String Page::GetPageFilename() const
		{
			return m_terrain.GetBaseFileName() + "/" + std::to_string(m_x) + "_" + std::to_string(m_z) + ".tile";
		}

		void Page::NotifyTileMaterialChanged(uint32 x, uint32 y)
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

			MaterialPtr material = m_Tiles(x, y)->GetMaterial();
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

		void Page::SetHeightAt(unsigned int x, unsigned int z, float value)
		{
			if (x >= constants::VerticesPerPage || z >= constants::VerticesPerPage)
			{
				return;
			}

			m_heightmap[x + z * constants::VerticesPerPage] = value;
			m_changed = true;
		}

		void Page::SetLayerAt(unsigned int x, unsigned int z, uint8 layer, float value)
		{
			if (x >= constants::VerticesPerPage || z >= constants::VerticesPerPage)
			{
				return;
			}

			Vector4& layers = m_layers[x + z * constants::VerticesPerPage];

			// 1. Record the old sum (for informational purposes, or checks).
			float oldSum = layers.x + layers.y + layers.z + layers.w;

			// 2. Set the chosen channel to newValue
			switch (layer)
			{
			case 0: layers.x = value; break;
			case 1: layers.y = value; break;
			case 2: layers.z = value; break;
			case 3: layers.w = value; break;
			default:
				// Handle error, unknown channel
				return;
			}

			// 3. Compute the sum of all four *after* setting the chosen channel
			float sumAfter = layers.x + layers.y + layers.z + layers.w;

			// 4. If the sum is 0 (or extremely close to 0), we can’t scale. 
			//    Handle that case gracefully.
			if (std::fabs(sumAfter) < 1e-6f)
			{
				// e.g. just set everything else to 0, or choose a fallback strategy.
				// We'll do a simple fallback here:
				layers.x = (layer == 0) ? value : 0.f;
				layers.y = (layer == 1) ? value : 0.f;
				layers.z = (layer == 2) ? value : 0.f;
				layers.w = (layer == 3) ? value : 0.f;
				return;
			}

			// 5. We want the total to be exactly 1. So we figure out the scale factor:
			float scale = 1.0f / sumAfter;

			// 6. Multiply the entire vector by this scale to force sum == 1
			layers.x *= scale;
			layers.y *= scale;
			layers.z *= scale;
			layers.w *= scale;

			m_changed = true;
		}

		void Page::Paint(uint8 layer, int x, int y, unsigned int innerRadius, unsigned int outerRadius, float intensity)
		{
			Paint(layer, x, y, innerRadius, outerRadius, intensity, 0.0f, 1.0f);
		}

		void Page::Paint(uint8 layer, int x, int y, unsigned int innerRadius, unsigned int outerRadius, float intensity, float minSloap, float maxSloap)
		{
		}

		void Page::Balance(unsigned int x, unsigned int y, unsigned int layer, int val)
		{

		}

		bool Page::ReadMCVRChunk(io::Reader& reader, uint32 header, uint32 size)
		{
			uint32 version;
			if (!(reader >> io::read<uint32>(version)))
			{
				ELOG("Failed to read tile format version!");
				return false;
			}

			const uint32 expectedVersion = 0x01;
			if (version != expectedVersion)
			{
				ELOG("Unsupported file format version detected (detected " << log_hex_digit(version) << ", expected " << log_hex_digit(expectedVersion) << " or lower");
				return false;
			}

			// Register new chunk readers
			AddChunkHandler(*constants::MaterialChunk, false, *this, &Page::ReadMCMTChunk);
			AddChunkHandler(*constants::VertexChunk, false, *this, &Page::ReadMCVTChunk);
			AddChunkHandler(*constants::NormalChunk, false, *this, &Page::ReadMCNMChunk);

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
				const auto tileBounds = tile->GetWorldBoundingBox(true);
				m_boundingBox.Combine(tileBounds);
			}
		}

		void Page::UpdateMaterial()
		{

		}
	}
}
