#pragma once

#include "graphics/vertex_index_data.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/renderable.h"

#include <memory>
#include <map>

#include "coverage_map.h"
#include "graphics/material_instance.h"
#include "scene_graph/mesh.h"

namespace mmo
{
	class Entity;
}

namespace mmo
{
	namespace terrain
	{
		class Terrain;
		class Page;

		class Tile final
			: public MovableObject
			, public Renderable
			, public ICollidable
		{
		public:

			explicit Tile(const String& name, Page& page, size_t startX, size_t startZ);
			~Tile() override;

		public:
			int32 GetX() const { return static_cast<int32>(m_tileX); }

			int32 GetY() const { return static_cast<int32>(m_tileY); }

			void PrepareRenderOperation(RenderOperation& operation) override;

			[[nodiscard]] const Matrix4& GetWorldTransform() const override;

			[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

			[[nodiscard]] MaterialPtr GetMaterial() const override;

			[[nodiscard]] MaterialPtr GetBaseMaterial() const;

			void SetMaterial(MaterialPtr material);

			[[nodiscard]] const String& GetMovableType() const override;

			[[nodiscard]] const AABB& GetBoundingBox() const override;

			[[nodiscard]] float GetBoundingRadius() const override;

			void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

			void PopulateRenderQueue(RenderQueue& queue) override;

			bool PreRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera) override;

			/// \brief Indicates whether this tile currently contains renderable terrain geometry.
			/// \details When all inner cells of the tile are marked as holes, no triangles are generated
			///          and the tile becomes non-renderable. In this case, index buffer creation is skipped
			///          and the tile will not be added to the render queue.
			/// \return True if geometry exists and the tile can be rendered; false if the tile is empty.
			[[nodiscard]] bool HasRenderableGeometry() const;

			Page& GetPage() const { return m_page; }

			Terrain& GetTerrain() const;

			void UpdateTerrain(size_t startx, size_t startz, size_t endx, size_t endz);

			void UpdateCoverageMap();

			void UpdateLOD(const Camera& camera);

			/// @brief Returns the current LOD level for this tile.
			/// @return The current LOD level (0 = highest detail, 3 = lowest detail)
			[[nodiscard]] uint32 GetCurrentLOD() const;

			ICollidable* GetCollidable() override { return this; }

			const ICollidable* GetCollidable() const override { return this; }

		private:
			void CreateVertexData(size_t startX, size_t startZ);

			void CreateIndexData(uint32 lod, uint32 northLod, uint32 eastLod, uint32 southLod, uint32 westLod);

			/// @brief Generates edge stitching triangles to connect with neighbors at different LOD levels
			/// @param indices Vector to append stitching indices to
			/// @param lod Current tile's LOD level
			/// @param northLod North neighbor's LOD level
			/// @param eastLod East neighbor's LOD level
			/// @param southLod South neighbor's LOD level
			/// @param westLod West neighbor's LOD level
			void GenerateEdgeStitching(std::vector<uint16>& indices, uint32 lod, uint32 northLod, uint32 eastLod, uint32 southLod, uint32 westLod);

			/// @brief Stitches the north edge for LOD > 0 using Faudra algorithm
			void StitchEdgeNorth(std::vector<uint16>& indices, uint32 hiLod, uint32 loLod, bool omitFirstTri, bool omitLastTri);

			/// @brief Stitches the east edge for LOD > 0 using Faudra algorithm
			void StitchEdgeEast(std::vector<uint16>& indices, uint32 hiLod, uint32 loLod, bool omitFirstTri, bool omitLastTri);

			/// @brief Stitches the south edge for LOD > 0 using Faudra algorithm
			void StitchEdgeSouth(std::vector<uint16>& indices, uint32 hiLod, uint32 loLod, bool omitFirstTri, bool omitLastTri);

			/// @brief Stitches the west edge for LOD > 0 using Faudra algorithm
			void StitchEdgeWest(std::vector<uint16>& indices, uint32 hiLod, uint32 loLod, bool omitFirstTri, bool omitLastTri);

			// Helper methods for new vertex layout (outer + inner vertices)
			/// @brief Gets the vertex index for an outer vertex at grid position (x, y)
			/// @param x X coordinate in outer vertex grid (0-8)
			/// @param y Y coordinate in outer vertex grid (0-8)
			/// @return Vertex buffer index for the outer vertex
			uint16 GetOuterVertexIndex(size_t x, size_t y) const;

			/// @brief Gets the vertex index for an inner vertex at grid position (x, y)
			/// @param x X coordinate in inner vertex grid (0-7)
			/// @param y Y coordinate in inner vertex grid (0-7)
			/// @return Vertex buffer index for the inner vertex
			uint16 GetInnerVertexIndex(size_t x, size_t y) const;

			/// @brief Determines if a grid cell is on the outer edge of the tile
			/// @param x X coordinate in the grid
			/// @param y Y coordinate in the grid
			/// @return True if on edge
			bool IsOuterEdge(size_t x, size_t y) const;

		private:
			Page& m_page;
			size_t m_tileX, m_tileY;
			size_t m_startX, m_startZ;
			std::unique_ptr<VertexData> m_vertexData;
			std::unique_ptr<IndexData> m_indexData;
			bool m_hasRenderableGeometry{ false };
			VertexBufferPtr m_mainBuffer;

			TexturePtr m_coverageTexture;
			std::shared_ptr<MaterialInstance> m_materialInstance;

			AABB m_bounds;
			float m_boundingRadius { 0.0f };
			Vector3 m_center;

			bool m_worldAABBDirty { true };
			
			/// @brief Index data cache for LOD and neighbor stitching combinations.
			/// 
			/// The key encodes the local tile LOD and the LOD of its four neighbors as:
			/// (lod | (northLod << 4) | (eastLod << 8) | (southLod << 12) | (westLod << 16)).
			/// With 4 LOD levels and 4 neighbors, this yields up to 4^5 = 1024 possible
			/// cache entries per tile in the worst case.
			///
			/// @note This cache is maintained per tile and does not currently implement
			///       an explicit size limit or eviction policy. For very large terrains
			///       with many tiles and heavily exercised LOD transitions, the memory
			///       used by all tile caches combined can be significant and should be
			///       taken into account when configuring terrain size and LOD settings.
			std::map<uint32, std::unique_ptr<IndexData>> m_lodIndexCache;
			uint32 m_currentLod = 0;
			uint32 m_currentStitchKey = 0;

		public:
			/// @brief Tests collision between a capsule and this terrain tile.
			///        Uses spatial culling to optimize performance by only testing triangles
			///        that could potentially intersect with the capsule's bounding box.
			/// @param capsule The capsule to test collision against in world space
			/// @param results Vector to store collision results
			/// @return True if any collision was detected, false otherwise
			bool TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const override;

			bool IsCollidable() const override { return true; }

			bool TestRayCollision(const Ray& ray, CollisionResult& result) const override;

		};
	}
}
