#pragma once

#include "base/typedefs.h"

#include "base/grid.h"
#include "graphics/material.h"
#include "math/aabb.h"

#include <unordered_map>
#include <array>

namespace mmo
{
	class SceneNode;
}

namespace mmo
{
	namespace terrain
	{
		class Terrain;
		class Tile;


		class CoverageMap;

		typedef uint32 TextureId;
		typedef uint32 LayerId;
		typedef std::unordered_map<TextureId, LayerId> TextureLayerMap;
		typedef std::unordered_map<LayerId, TextureId> LayerTextureMap;

		class Page
		{
		public:
			explicit Page(Terrain& terrain, uint32 x, uint32 z);
			~Page();

		public:
			bool Prepare();

			void Load();

			void Unload();

			Tile* GetTile(uint32 x, uint32 z);

			Terrain& GetTerrain();

			uint32 GetX() const;

			uint32 GetZ() const;

			uint32 GetID() const;

			float GetHeightAt(size_t x, size_t y) const;

			float GetSmoothHeightAt(float x, float y) const;

			void UpdateTiles(int fromX, int fromZ, int toX, int toZ, bool normalsOnly = false);

			Vector3 GetVectorFromPoint(int x, int z);

			const Vector3& GetNormalAt(uint32 x, uint32 z);

			const Vector3& CalculateNormalAt(uint32 x, uint32 z);

			const Vector3& GetTangentAt(uint32 x, uint32 z);

			const Vector3& CalculateTangentAt(uint32 x, uint32 z);

			bool IsPrepared() const;

			bool IsPreparing() const;

			bool IsLoaded() const;

			bool IsLoadable() const;

			const AABB& GetBoundingBox() const;

		private:
			void UpdateBoundingBox();

			void UpdateMaterial();

		private:
			typedef Grid<std::unique_ptr<Tile>> TileGrid;

			Terrain& m_terrain;
			SceneNode* m_pageNode;
			MaterialPtr m_material;
			TileGrid m_Tiles;

			std::array<float, 129 * 129> m_heightmap;
			std::array<Vector3, 129 * 129> m_normals;
			std::array<Vector3, 129 * 129> m_tangents;
			std::vector<String> m_textures;

			uint32 m_x;
			uint32 m_z;
			bool m_preparing;
			bool m_prepared;
			bool m_loaded;
			AABB m_boundingBox;

		};
	}
}
