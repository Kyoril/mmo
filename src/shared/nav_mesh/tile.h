
#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/filesystem.h"
#include "math/aabb.h"

#include "Recast.h"
#include "DetourNavMesh.h"

#include <vector>

namespace io
{
	class Reader;
}

namespace mmo::nav
{
	class Map;

	class Tile final : public NonCopyable
	{
	public:
		explicit Tile(Map& map, io::Reader& reader, const std::filesystem::path& navPath, bool loadHeightfield = true);
		~Tile() override;

		int32 GetX() const { return m_x; }
		int32 GetY() const { return m_y; }

	public:
        dtTileRef m_ref { 0 };

        //AABB m_bounds {};

        uint32 m_zoneId = 0;
        uint32 m_areaId = 0;

        //std::vector<float> m_quadHeights;

	private:
        Map& m_map;
        const std::filesystem::path m_navPath;

        int32 m_x = 0;
        int32 m_y = 0;

        std::vector<uint8> m_tileData;

        // store this for possible delayed load of the data
        //size_t m_heightFieldSpanStart = 0;
        //rcHeightfield m_heightField;

        void LoadHeightField(io::Reader& in);
        void LoadHeightField();
	};
}
