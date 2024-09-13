
#include "page.h"

#include "terrain.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include <sstream>

#include "tile.h"

namespace mmo
{
	namespace terrain
	{
		Page::Page(Terrain& terrain, int32 x, int32 z)
			: m_terrain(terrain)
			, m_x(x)
			, m_z(z)
			, m_preparing(false)
			, m_prepared(false)
			, m_loaded(false)
		{
			const Vector3 offset = Vector3(
				static_cast<float>(static_cast<double>(32 - m_x) * constants::PageSize),
				0.0f,
				static_cast<float>(static_cast<double>(32 - m_z) * constants::PageSize)
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

			m_heightmap.fill(0.0f);
			m_normals.fill(Vector3::UnitY);
			m_tangents.fill(Vector3::UnitZ);

			// TODO: Read page file

			m_prepared = true;
			m_preparing = false;

			return true;
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

					m_pageNode->AttachObject(*tile);
				}
			}

			m_loaded = true;
			UpdateBoundingBox();
		}

		void Page::Unload()
		{
			m_loaded = false;
			m_Tiles.clear();
		}

		Tile* Page::GetTile(uint32 x, uint32 z)
		{
			if (x >= m_Tiles.width() ||
				z >= m_Tiles.height()) {
				return nullptr;
			}

			return m_Tiles(x, z).get();
		}

		Terrain& Page::GetTerrain()
		{
			return m_terrain;
		}

		uint32 Page::GetX() const
		{
			return m_x;
		}

		uint32 Page::GetZ() const
		{
			return m_z;
		}

		uint32 Page::GetID() const
		{
			return m_x + m_z * m_terrain.GetWidth();
		}

		float Page::GetHeightAt(size_t x, size_t y) const
		{
			if (x >= constants::VerticesPerPage ||
				y >= constants::VerticesPerPage)
			{
				return 0.0f;
			}

			return m_heightmap[x + y * constants::VerticesPerPage];
		}

		float Page::GetSmoothHeightAt(float x, float y) const
		{
			return 0.0f;
		}

		void Page::UpdateTiles(int fromX, int fromZ, int toX, int toZ, bool normalsOnly)
		{
			unsigned int fromTileX = fromX / (constants::VerticesPerTile - 1);
			unsigned int fromTileZ = fromZ / (constants::VerticesPerTile - 1);
			unsigned int toTileX = toX / (constants::VerticesPerTile - 1);
			unsigned int toTileZ = toZ / (constants::VerticesPerTile - 1);

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
							//pTile->UpdateTerrain(fromX, fromZ, toX, toZ);
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
			return Vector3::UnitY;
		}

		const Vector3& Page::CalculateNormalAt(uint32 x, uint32 z)
		{
			return Vector3::UnitY;
		}

		const Vector3& Page::GetTangentAt(uint32 x, uint32 z)
		{
			return Vector3::UnitZ;
		}

		const Vector3& Page::CalculateTangentAt(uint32 x, uint32 z)
		{
			return Vector3::UnitZ;
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

		void Page::UpdateBoundingBox()
		{
			m_boundingBox = AABB(Vector3(m_x * constants::PageSize, 0.0f, m_z * constants::PageSize),
				Vector3(m_x * constants::PageSize + constants::PageSize, 0.0f, m_z * constants::PageSize + constants::PageSize));

			for (auto& tile : m_Tiles)
			{
				m_boundingBox.Combine(tile->GetBoundingBox());
			}
		}

		void Page::UpdateMaterial()
		{

		}
	}
}