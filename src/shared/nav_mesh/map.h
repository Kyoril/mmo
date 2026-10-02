
#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "terrain/constants.h"
#include "base/filesystem.h"
#include "math/ray.h"
#include "tile.h"

#include "DetourNavMeshQuery.h"

#include <unordered_map>
#include <memory>
#include <vector>

namespace mmo::nav
{
	/// @brief The parsed contents of one nav page file, ready to be installed into a Map.
	/// @details Reading a page (Map::ReadPage) only touches the asset registry and is safe on any
	/// thread. Installing it (Map::AddPage) mutates the Detour nav mesh and must happen on the
	/// thread that owns the Map.
	struct PageData
	{
		struct TileData
		{
			int32 x = 0;
			int32 y = 0;
			std::vector<uint8> meshData;
		};

		int32 x = 0;
		int32 y = 0;
		std::vector<TileData> tiles;
	};

#pragma pack(push, 1)
	struct MapHeader
	{
		uint32 sig;
		uint32 ver;
		uint32 kind;
		uint32 x;
		uint32 y;
		uint32 tileCount;

		[[nodiscard]] bool Verify() const;
	};
#pragma pack(pop)

	class Map final : public NonCopyable
	{
		friend class Tile;

	public:
		explicit Map(const std::string& mapName);
		~Map() override = default;

	public:
		[[nodiscard]] bool HasPage(int32 x, int32 y) const;

		[[nodiscard]] bool HasPages() const { return m_hasPages; }

		[[nodiscard]] bool IsPageLoaded(int32 x, int32 y) const;

		/// @brief Reads and installs a single page. Equivalent to ReadPage followed by AddPage.
		bool LoadPage(int32 x, int32 y);

		/// @brief Reads and parses a nav page file without touching any Map.
		/// @details Thread-safe: only the (internally locked) asset registry is accessed, so
		/// this can run on a background thread while the owning thread keeps querying the map.
		/// @param mapName Name of the nav map (its directory name).
		/// @param x Page x coordinate.
		/// @param y Page y coordinate.
		/// @param out Receives the page's tiles.
		/// @return false if the page file is missing or malformed.
		static bool ReadPage(const std::string& mapName, int32 x, int32 y, PageData& out);

		/// @brief Installs a page previously produced by ReadPage into this map's nav mesh.
		/// @details Must be called on the thread that owns this map. A page that is already
		/// loaded is left untouched.
		/// @return true if the page is loaded afterwards.
		bool AddPage(PageData&& page);

		void UnloadPage(int32 x, int32 y);

		int32 LoadAllPages();

		void UnloadAllPages();

		bool FindPath(const Vector3& start, const Vector3& end, std::vector<Vector3>& output, bool allowPartial = false) const;

		/// @brief Checks whether two world positions have an unobstructed line of sight on the nav mesh.
		/// @param start The source position.
		/// @param end The destination position.
		/// @return true if nothing on the nav mesh blocks the ray between start and end.
		bool LineOfSight(const Vector3& start, const Vector3& end) const;

		/// @brief Like LineOfSight but also reports where the ray was blocked.
		/// @param start The source position.
		/// @param end The destination position.
		/// @param hitPoint Receives the world position of the first obstruction (equals end when unobstructed).
		/// @return true if the ray reaches end without obstruction.
		bool LineOfSightEx(const Vector3& start, const Vector3& end, Vector3& hitPoint) const;

		//bool FindHeight(const Vector3& source, float x, float z, float& y) const;

		//bool FindHeights(float x, float z, std::vector<float>& output) const;

		//bool ZoneAndArea(const Vector3& position, unsigned int& zone, unsigned int& area) const;

		//bool LineOfSight(const Vector3& start, const Vector3& stop, bool doodads) const;

		bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const;

		//bool FindPointInBetweenVectors(const Vector3& start, const Vector3& end, const float distance, Vector3& inBetweenPoint) const;

		[[nodiscard]] const dtNavMesh& GetNavMesh() const { return m_navMesh; }

		[[nodiscard]] const dtNavMeshQuery& GetNavMeshQuery() const { return m_navQuery; }

		[[nodiscard]] const std::string& GetName() const { return m_mapName; }

	private:
		struct TileCoordHash
		{
			std::size_t operator()(const std::pair<int32, int32>& coordinate) const
			{
				return std::hash<uint64>()((static_cast<uint64>(static_cast<uint32>(coordinate.first)) << 32) | static_cast<uint32>(coordinate.second));
			}
		};

	private:
		[[nodiscard]] const Tile* GetTile(float x, float y) const;

		//bool GetPageHeight(const Tile* tile, float x, float y, float& height, unsigned int* zone = nullptr, unsigned int* area = nullptr) const;

		// find the next floor y below the given hint
		//bool FindNextY(const Tile* tile, float x, float y, float zHint, bool includeTerrain, float& result) const;

		//bool RayCast(Ray& ray, bool doodads) const;

		//bool RayCast(Ray& ray, const std::vector<const Tile*>& tiles, bool doodads, unsigned int* zone = nullptr, unsigned int* area = nullptr) const;

	private:
		static constexpr int MaxStackedPolys = 128;
		static constexpr int MaxPathPolys = 256;
		static constexpr int MaxSmoothPathPoints = 1024;

		// this is false when the map is based on a global world object
		bool m_hasPages = false;

		bool m_hasPage[terrain::constants::MaxPages][terrain::constants::MaxPages]{};
		bool m_loadedPage[terrain::constants::MaxPages][terrain::constants::MaxPages]{};

		const std::filesystem::path m_dataPath;
		const std::string m_mapName;

		dtNavMesh m_navMesh;
		dtNavMeshQuery m_navQuery;
		dtQueryFilter m_queryFilter;

		std::unordered_map<std::pair<int32, int32>, std::unique_ptr<Tile>, TileCoordHash> m_tiles;
	};
}
