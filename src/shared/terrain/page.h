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
			explicit Page(Terrain &terrain, int32 x, int32 z);
			~Page();

		public:
			bool Prepare();

			bool Load();

			void Unload();

			void Destroy();

			Tile *GetTile(uint32 x, uint32 y);

			Tile *GetTileAt(float x, float z);

			Terrain &GetTerrain();

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
			/** \brief Get inner-vertex height at page-local inner grid coordinates.
			 *
			 * Inner grid is sized constants::InnerVerticesPerPageSide per side.
			 */
			float GetInnerHeightAt(size_t x, size_t y) const;

			/** \brief Set inner-vertex height at page-local inner grid coordinates. */
			void SetInnerHeightAt(size_t x, size_t y, float value);

			/** \brief Get inner-vertex vertex color (ARGB) at page-local inner grid coordinates. */
			uint32 GetInnerColorAt(size_t x, size_t y) const;

			/** \brief Set inner-vertex vertex color (ARGB) at page-local inner grid coordinates. */
			void SetInnerColorAt(size_t x, size_t y, uint32 color);

			/** \brief Get inner-vertex normal (decoded) at page-local inner grid coordinates. */
			Vector3 GetInnerNormalAt(size_t x, size_t y) const;

			/** \brief Set inner-vertex normal (encoded) at page-local inner grid coordinates. */
			void SetInnerNormalAt(size_t x, size_t y, const Vector3 &normal);

			Vector3 CalculateNormalAt(uint32 x, uint32 z);

			Vector3 GetTangentAt(uint32 x, uint32 z);

			Vector3 CalculateTangentAt(uint32 x, uint32 z);

			bool IsPrepared() const;

			bool IsPreparing() const;

			bool IsLoaded() const;

			bool IsLoadable() const;

			const AABB &GetBoundingBox() const;

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

			SceneNode *GetSceneNode() const { return m_pageNode; }

			/// @brief Check if an inner vertex is marked as a hole
			/// @param localTileX Tile X coordinate (0-15)
			/// @param localTileY Tile Y coordinate (0-15)
			/// @param innerX Inner vertex X within tile (0-7)
			/// @param innerY Inner vertex Y within tile (0-7)
			/// @return True if the vertex is a hole
			bool IsHole(uint32 localTileX, uint32 localTileY, uint32 innerX, uint32 innerY) const;

			/// @brief Set or clear the hole flag for an inner vertex
			/// @param localTileX Tile X coordinate (0-15)
			/// @param localTileY Tile Y coordinate (0-15)
			/// @param innerX Inner vertex X within tile (0-7)
			/// @param innerY Inner vertex Y within tile (0-7)
			/// @param isHole True to mark as hole, false to mark as solid
			void SetHole(uint32 localTileX, uint32 localTileY, uint32 innerX, uint32 innerY, bool isHole);

			/// @brief Get the hole map for a specific tile
			/// @param localTileX Tile X coordinate (0-15)
			/// @param localTileY Tile Y coordinate (0-15)
			/// @return 64-bit hole map where each bit represents an inner vertex
			uint64 GetTileHoleMap(uint32 localTileX, uint32 localTileY) const;

		private:
			bool ReadMCVRChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCMTChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCVTChunk(io::Reader &reader, uint32 header, uint32 size);

			// Legacy v1 readers (273x273 grid), will be converted to new outer grid
			bool ReadMCVTChunkV1(io::Reader &reader, uint32 header, uint32 size);
			bool ReadMCNMChunkV1(io::Reader &reader, uint32 header, uint32 size);
			bool ReadMCVSChunkV1(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCNMChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCLYChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCTIChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCVTSubChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCVNSubChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCTNSubChunk(io::Reader &reader, uint32 header, uint32 size);
			// V2 inner vertex chunks
			bool ReadMCIVChunk(io::Reader &reader, uint32 header, uint32 size);
			bool ReadMCINChunk(io::Reader &reader, uint32 header, uint32 size);
			bool ReadMCISChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCHLChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCARChunk(io::Reader &reader, uint32 header, uint32 size);

			bool ReadMCVSChunk(io::Reader &reader, uint32 header, uint32 size);

		private:
			void UpdateBoundingBox();

			void UpdateMaterial();

		protected:
			bool IsValid() const override;

			bool OnReadFinished() override;

		private:
			typedef Grid<std::unique_ptr<Tile>> TileGrid;

			Terrain &m_terrain;
			SceneNode *m_pageNode;
			MaterialPtr m_material;
			TileGrid m_Tiles;

			std::vector<float> m_heightmap;
			std::vector<EncodedNormal8> m_normals;
			// Inner per-page vertex data for full editor precision
			std::vector<float> m_innerHeightmap;
			std::vector<EncodedNormal8> m_innerNormals;
			std::vector<uint32> m_innerColors;
			std::vector<MaterialPtr> m_materials;
			std::vector<uint32> m_layers;
			std::vector<uint16> m_tileZones;
			std::vector<uint32> m_colors;
			/// Hole data: one 64-bit value per tile (8x8 inner vertices)
			/// Each bit represents one inner vertex (1 = hole, 0 = solid)
			std::vector<uint64> m_holeMap;

			int32 m_x;
			int32 m_z;
			bool m_preparing;
			bool m_prepared;
			bool m_loaded;
			bool m_changed{false};
			bool m_unloadRequested = false;
			AABB m_boundingBox;
		};
	}
}
