#pragma once

#include "base/typedefs.h"

#include "base/grid.h"
#include "graphics/material.h"
#include "math/aabb.h"
#include "terrain.h"

#include <unordered_map>
#include <array>

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"

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

		class Page final : public ChunkReader
		{
		public:
			explicit Page(Terrain& terrain, int32 x, int32 z);
			~Page();

		public:
			bool Prepare();

			void Load();

			void Unload();

			Tile* GetTile(uint32 x, uint32 y);

			Tile* GetTileAt(float x, float z);

			Terrain& GetTerrain();

			uint32 GetX() const;

			uint32 GetY() const;

			uint32 GetID() const;

			float GetHeightAt(size_t x, size_t y) const;

			float GetSmoothHeightAt(float x, float y) const;

			void UpdateTiles(int fromX, int fromZ, int toX, int toZ, bool normalsOnly = false);

			Vector3 GetVectorFromPoint(int x, int z);

			Vector3 GetNormalAt(uint32 x, uint32 z);

			Vector3 CalculateNormalAt(uint32 x, uint32 z);

			Vector3 GetTangentAt(uint32 x, uint32 z);

			Vector3 CalculateTangentAt(uint32 x, uint32 z);

			bool IsPrepared() const;

			bool IsPreparing() const;

			bool IsLoaded() const;

			bool IsLoadable() const;

			const AABB& GetBoundingBox() const;

			void UpdateTileSelectionQuery();

			bool IsChanged() const { return m_changed; }

			bool Save();

			String GetPageFilename() const;

			void NotifyTileMaterialChanged(uint32 x, uint32 y);

			void SetHeightAt(unsigned int x, unsigned int z, float value);

			void Paint(uint8 layer, int x, int y, unsigned int innerRadius, unsigned int outerRadius, float intensity = 1.0f);

			void Paint(uint8 layer, int x, int y, unsigned int innerRadius, unsigned int outerRadius, float intensity, float minSloap, float maxSloap);

			void Balance(unsigned int x, unsigned int y, unsigned int layer, int val);

		private:

			bool ReadMCVRChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCMTChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVTChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCLYChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCTIChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVTSubChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVNSubChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCTNSubChunk(io::Reader& reader, uint32 header, uint32 size);

		private:
			void UpdateBoundingBox();

			void UpdateMaterial();

		protected:
			bool IsValid() const noexcept override;

			bool OnReadFinished() noexcept override;

		private:
			typedef Grid<std::unique_ptr<Tile>> TileGrid;

			Terrain& m_terrain;
			SceneNode* m_pageNode;
			MaterialPtr m_material;
			TileGrid m_Tiles;

			std::vector<float> m_heightmap;
			std::vector<Vector3> m_normals;
			std::vector<Vector3> m_tangents;
			std::vector<MaterialPtr> m_materials;
			std::vector<Vector4> m_layers;

			int32 m_x;
			int32 m_z;
			bool m_preparing;
			bool m_prepared;
			bool m_loaded;
			bool m_changed{ false };
			AABB m_boundingBox;

		};
	}
}
