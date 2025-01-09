
#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "terrain/constants.h"
#include "base/filesystem.h"
#include "math/ray.h"

#include "DetourNavMeshQuery.h"

#include <unordered_map>
#include <memory>


template <>
struct std::hash<std::pair<int, int>>
{
	std::size_t operator()(const std::pair<int, int>& coordinate) const noexcept
	{
		return std::hash<int>()(coordinate.first ^ coordinate.second);
	}
};

namespace mmo::nav
{
	class Tile;

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

		bool LoadPage(int32 x, int32 y);

		void UnloadPage(int32 x, int32 y);

		int32 LoadAllPages();

		bool FindPath(const Vector3& start, const Vector3& end, std::vector<Vector3>& output, bool allowPartial = false) const;

		//bool FindHeight(const Vector3& source, float x, float z, float& y) const;

		//bool FindHeights(float x, float z, std::vector<float>& output) const;

		//bool ZoneAndArea(const Vector3& position, unsigned int& zone, unsigned int& area) const;

		//bool LineOfSight(const Vector3& start, const Vector3& stop, bool doodads) const;

		bool FindRandomPointAroundCircle(const Vector3& centerPosition, float radius, Vector3& randomPoint) const;

		//bool FindPointInBetweenVectors(const Vector3& start, const Vector3& end, const float distance, Vector3& inBetweenPoint) const;

		[[nodiscard]] const dtNavMesh& GetNavMesh() const { return m_navMesh; }

		[[nodiscard]] const dtNavMeshQuery& GetNavMeshQuery() const { return m_navQuery; }

	private:
		[[nodiscard]] const Tile* GetTile(float x, float y) const;

		//bool GetPageHeight(const Tile* tile, float x, float y, float& height, unsigned int* zone = nullptr, unsigned int* area = nullptr) const;

		// find the next floor y below the given hint
		//bool FindNextY(const Tile* tile, float x, float y, float zHint, bool includeTerrain, float& result) const;

		//bool RayCast(Ray& ray, bool doodads) const;

		//bool RayCast(Ray& ray, const std::vector<const Tile*>& tiles, bool doodads, unsigned int* zone = nullptr, unsigned int* area = nullptr) const;

	private:
		static constexpr int MaxStackedPolys = 128;
		static constexpr int MaxPathHops = 4096;

		// this is false when the map is based on a global world object
		bool m_hasPages;

		bool m_hasPage[terrain::constants::MaxPages][terrain::constants::MaxPages];
		bool m_loadedPage[terrain::constants::MaxPages][terrain::constants::MaxPages];

		const std::filesystem::path m_dataPath;
		const std::string m_mapName;

		dtNavMesh m_navMesh;
		dtNavMeshQuery m_navQuery;
		dtQueryFilter m_queryFilter;

		std::unordered_map<std::pair<int, int>, std::unique_ptr<Tile>> m_tiles;
	};
}
