
#include "tile.h"

#include "map.h"
#include "base/macros.h"

namespace mmo::nav
{
	Tile::Tile(Map& map, const int32 x, const int32 y, std::vector<uint8>&& meshData)
		: m_map(map)
		, m_x(x)
		, m_y(y)
		, m_tileData(std::move(meshData))
	{
		if (!m_tileData.empty())
		{
			auto const result = m_map.m_navMesh.addTile(m_tileData.data(), static_cast<int>(m_tileData.size()), 0, 0, &m_ref);
			ASSERT(result == DT_SUCCESS);
		}
	}

	Tile::~Tile()
	{
		if (!!m_ref)
		{
			auto const result =
				m_map.m_navMesh.removeTile(m_ref, nullptr, nullptr);
			ASSERT(result == DT_SUCCESS);
		}
	}
}
