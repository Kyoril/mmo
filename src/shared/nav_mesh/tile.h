#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "DetourNavMesh.h"

#include <vector>

namespace mmo::nav
{
	class Map;

	/// @brief One Detour tile installed into a Map's nav mesh. Owns the tile's mesh data, which
	/// Detour references (but does not own) for as long as the tile stays added.
	class Tile final : public NonCopyable
	{
	public:
		/// @brief Adds the given Detour tile data to the map's nav mesh.
		/// @param map The map whose nav mesh receives the tile.
		/// @param x Tile x coordinate.
		/// @param y Tile y coordinate.
		/// @param meshData Serialized Detour tile data (may be empty for a tile without mesh).
		explicit Tile(Map& map, int32 x, int32 y, std::vector<uint8>&& meshData);
		~Tile() override;

		int32 GetX() const { return m_x; }
		int32 GetY() const { return m_y; }

	public:
		dtTileRef m_ref { 0 };

	private:
		Map& m_map;

		int32 m_x = 0;
		int32 m_y = 0;

		std::vector<uint8> m_tileData;
	};
}
