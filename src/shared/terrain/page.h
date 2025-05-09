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
#include "scene_graph/mesh.h"

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

			bool Load();

			void Unload();

			void Destroy();

			Tile* GetTile(uint32 x, uint32 y);

			Tile* GetTileAt(float x, float z);

			Terrain& GetTerrain();

			uint32 GetX() const;

			uint32 GetY() const;

			uint32 GetID() const;

			float GetHeightAt(size_t x, size_t y) const;

			uint32 GetColorAt(size_t x, size_t y) const;

			uint32 GetLayersAt(size_t x, size_t y) const;

			float GetSmoothHeightAt(float x, float y) const;

			Vector3 GetSmoothNormalAt(float x, float y) const;

			void UpdateTiles(int fromX, int fromZ, int toX, int toZ, bool normalsOnly = false);

			void UpdateTileCoverage(int fromX, int fromZ, int toX, int toZ);

			Vector3 GetNormalAt(uint32 x, uint32 z) const;

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

			void SetColorAt(size_t x, size_t y, uint32 color);

			void SetLayerAt(unsigned int x, unsigned int z, uint8 layer, float value);

			uint32 GetArea(uint32 localTileX, uint32 localTileY) const;

			void SetArea(uint32 localTileX, uint32 localTileY, uint32 area);

			SceneNode* GetSceneNode() const { return m_pageNode; }

		private:

			bool ReadMCVRChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCMTChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVTChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCNMChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCLYChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCTIChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVTSubChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVNSubChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCTNSubChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCARChunk(io::Reader& reader, uint32 header, uint32 size);

			bool ReadMCVSChunk(io::Reader& reader, uint32 header, uint32 size);

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
			std::vector<EncodedNormal8> m_normals;
			std::vector<MaterialPtr> m_materials;
			std::vector<uint32> m_layers;
			std::vector<uint16> m_tileZones;
			std::vector<uint32> m_colors;

			int32 m_x;
			int32 m_z;
			bool m_preparing;
			bool m_prepared;
			bool m_loaded;
			bool m_changed{ false };
			bool m_unloadRequested = false;
			AABB m_boundingBox;
		};
	}
}
