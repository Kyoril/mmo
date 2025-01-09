
#include "tile.h"

#include "RecastAlloc.h"

#include "map.h"
#include "binary_io/reader.h"

namespace mmo::nav
{
	Tile::Tile(Map& map, io::Reader& reader, const std::filesystem::path& navPath, bool loadHeightfield)
		: m_map(map)
	{
		reader >> io::read<int32>(m_x) >> io::read<int32>(m_y);

		uint8 quadHeight;
		reader >> io::read<uint8>(quadHeight);
		ASSERT(quadHeight == 0 || quadHeight == 1);

		if (quadHeight)
		{
			reader >> io::read<uint32>(m_zoneId) >> io::read<uint32>(m_areaId);

			m_quadHeights.resize(terrain::constants::VerticesPerTile * terrain::constants::VerticesPerTile);
			reader >> io::read_range(m_quadHeights);
		}

		reader >> io::read<int32>(m_heightField.width) >> io::read<int32>(m_heightField.height)
			>> io::read<float>(m_heightField.bmin[0]) >> io::read<float>(m_heightField.bmin[1]) >> io::read<float>(m_heightField.bmin[2])
			>> io::read<float>(m_heightField.bmax[0]) >> io::read<float>(m_heightField.bmax[1]) >> io::read<float>(m_heightField.bmax[2])
			>> io::read<float>(m_heightField.cs) >> io::read<float>(m_heightField.ch);

		// for now, width and height must always be equal.  this check is here as a
		// way to make sure we are reading the file correctly so far
		ASSERT(m_heightField.width == m_heightField.height);

		Vector3 a, b;
		a = Vector3(m_heightField.bmin[0], m_heightField.bmin[1], m_heightField.bmin[2]);
		b = Vector3(m_heightField.bmax[0], m_heightField.bmax[1], m_heightField.bmax[2]);

		m_bounds.min.x = (std::min)(a.x, b.x);
		m_bounds.max.x = (std::max)(a.x, b.x);
		m_bounds.min.y = (std::min)(a.y, b.y);
		m_bounds.max.y = (std::max)(a.y, b.y);
		m_bounds.min.z = (std::min)(a.z, b.z);
		m_bounds.max.z = (std::max)(a.z, b.z);

		m_heightFieldSpanStart = reader.getSource()->position();

		if (loadHeightfield)
		{
			LoadHeightField(reader);
		}	
		else
		{
			m_heightField.spans = nullptr;

			for (auto i = 0; i < m_heightField.width * m_heightField.height; ++i)
			{
				uint32 columnSize;
				reader >> io::read<uint32>(columnSize);

				reader.skip(3 * columnSize * sizeof(uint32));
			}
		}

		uint32 meshSize;
		reader >> io::read<uint32>(meshSize);

		if (meshSize > 0)
		{
			m_tileData.resize(meshSize);
			reader >> io::read_range(m_tileData);

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

		if (!!m_heightField.spans)
		{
			for (auto i = 0; i < m_heightField.width * m_heightField.height; ++i)
			{
				rcFree(m_heightField.spans[i]);
			}
		}
	}

	void Tile::LoadHeightField(io::Reader& in)
	{
		ASSERT(!m_heightField.spans);

		m_heightField.spans = reinterpret_cast<rcSpan**>(rcAlloc(m_heightField.width * m_heightField.height * sizeof(rcSpan*), RC_ALLOC_PERM));

		for (auto i = 0; i < m_heightField.width * m_heightField.height; ++i)
		{
			uint32 columnSize;
			in >> io::read<uint32>(columnSize);

			if (!columnSize)
			{
				m_heightField.spans[i] = nullptr;
				continue;
			}

			m_heightField.spans[i] = reinterpret_cast<rcSpan*>(rcAlloc(columnSize * sizeof(rcSpan), RC_ALLOC_PERM));

			for (auto s = 0u; s < columnSize; ++s)
			{
				uint32 smin, smax, area;
				in >> io::read<uint32>(smin) >> io::read<uint32>(smax) >> io::read<uint32>(area);

				m_heightField.spans[i][s].smin = smin;
				m_heightField.spans[i][s].smax = smax;
				m_heightField.spans[i][s].area = area;
				m_heightField.spans[i][s].next = nullptr;

				if (s > 0)
				{
					m_heightField.spans[i][s - 1].next = &m_heightField.spans[i][s];
				}
			}
		}
	}

	void Tile::LoadHeightField()
	{
		/*
		utility::BinaryStream in(m_navPath);
		in.rpos(m_heightFieldSpanStart);
		LoadHeightField(in);
		*/
	}
}
