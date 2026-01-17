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

			/// @brief Constructs a new terrain tile.
			/// @param name The name of the tile.
			/// @param page The page that contains this tile.
			/// @param startX The starting X coordinate of the tile within the page.
			/// @param startZ The starting Z coordinate of the tile within the page.
			explicit Tile(const String& name, Page& page, size_t startX, size_t startZ);
			
			/// @brief Destructor.
			~Tile() override;

		public:
			/// @brief Gets the X coordinate of this tile.
			/// @return The X coordinate as an int32.
			int32 GetX() const { return static_cast<int32>(m_tileX); }

			/// @brief Gets the Y coordinate of this tile.
			/// @return The Y coordinate as an int32.
			int32 GetY() const { return static_cast<int32>(m_tileY); }

			/// @brief Prepares the render operation for this tile.
			/// @param operation The render operation to prepare.
			void PrepareRenderOperation(RenderOperation& operation) override;

			/// @brief Gets the world transform matrix for this tile.
			/// @return Reference to the world transform matrix.
			[[nodiscard]] const Matrix4& GetWorldTransform() const override;

			/// @brief Gets the squared view depth from the camera.
			/// @param camera The camera to calculate distance from.
			/// @return The squared distance from the camera.
			[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

			/// @brief Gets the material used for rendering this tile.
			/// @return Pointer to the material.
			[[nodiscard]] MaterialPtr GetMaterial() const override;

			/// @brief Gets the base material without instance-specific modifications.
			/// @return Pointer to the base material.
			[[nodiscard]] MaterialPtr GetBaseMaterial() const;

			/// @brief Sets the material used for rendering this tile.
			/// @param material The material to set.
			void SetMaterial(MaterialPtr material);

			/// @brief Gets the movable type identifier.
			/// @return Reference to the movable type string.
			[[nodiscard]] const String& GetMovableType() const override;

			/// @brief Gets the axis-aligned bounding box for this tile.
			/// @return Reference to the bounding box.
			[[nodiscard]] const AABB& GetBoundingBox() const override;

			/// @brief Gets the bounding radius of this tile.
			/// @return The bounding radius.
			[[nodiscard]] float GetBoundingRadius() const override;

			/// @brief Visits all renderables in this tile.
			/// @param visitor The visitor to accept.
			/// @param debugRenderables True to include debug renderables.
			void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

			/// @brief Populates the render queue with this tile's render operations.
			/// @param queue The render queue to populate.
			void PopulateRenderQueue(RenderQueue& queue) override;

			/// @brief Pre-render callback before the tile is rendered.
			/// @param scene The scene being rendered.
			/// @param graphicsDevice The graphics device.
			/// @param camera The camera being used for rendering.
			/// @return True if the tile should be rendered, false otherwise.
			bool PreRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera) override;

			/// @brief Indicates whether this tile currently contains renderable terrain geometry.
			/// @details When all inner cells of the tile are marked as holes, no triangles are generated
			///          and the tile becomes non-renderable. In this case, index buffer creation is skipped
			///          and the tile will not be added to the render queue.
			/// @return True if geometry exists and the tile can be rendered; false if the tile is empty.
			[[nodiscard]] bool HasRenderableGeometry() const;

			/// @brief Gets the page that contains this tile.
			/// @return Reference to the parent page.
			Page& GetPage() const { return m_page; }

			/// @brief Gets the terrain that contains this tile.
			/// @return Reference to the terrain.
			Terrain& GetTerrain() const;

			/// @brief Updates the terrain geometry in a specified region.
			/// @param startx The starting X coordinate.
			/// @param startz The starting Z coordinate.
			/// @param endx The ending X coordinate.
			/// @param endz The ending Z coordinate.
			void UpdateTerrain(size_t startx, size_t startz, size_t endx, size_t endz);

			/// @brief Updates the coverage map texture for texture splatting.
			void UpdateCoverageMap();

			/// @brief Updates the level of detail based on camera distance.
			/// @param camera The camera to calculate LOD from.
			void UpdateLOD(const Camera& camera);

			/// @brief Returns the current LOD level for this tile.
			/// @return The current LOD level (0 = highest detail, 3 = lowest detail)
			[[nodiscard]] uint32 GetCurrentLOD() const;

			/// @brief Gets the collidable interface for this tile.
			/// @return Pointer to the collidable interface.
			ICollidable* GetCollidable() override { return this; }

			/// @brief Gets the collidable interface for this tile (const version).
			/// @return Pointer to the collidable interface.
			const ICollidable* GetCollidable() const override { return this; }

		private:
			/// @brief Creates the vertex data for this tile.
			/// @param startX The starting X coordinate within the page.
			/// @param startZ The starting Z coordinate within the page.
			void CreateVertexData(size_t startX, size_t startZ);

			/// @brief Creates the index data for this tile with LOD and edge stitching.
			/// @param lod The LOD level for this tile.
			/// @param northLod The LOD level of the north neighbor.
			/// @param eastLod The LOD level of the east neighbor.
			/// @param southLod The LOD level of the south neighbor.
			/// @param westLod The LOD level of the west neighbor.
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

			/// @brief Checks if this tile is collidable.
			/// @return True as terrain tiles are always collidable.
			bool IsCollidable() const override { return true; }

			/// @brief Tests collision between a ray and this terrain tile.
			/// @param ray The ray to test collision against.
			/// @param result Output parameter for collision result.
			/// @return True if a collision was detected, false otherwise.
			bool TestRayCollision(const Ray& ray, CollisionResult& result) const override;

		};
	}
}
