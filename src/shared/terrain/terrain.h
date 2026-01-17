// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/grid.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <memory>

#include "graphics/material.h"

#include "constants.h"

namespace mmo
{
	struct Ray;
	class SceneNode;
	class Scene;
	class Camera;

	namespace terrain
	{
		class Tile;
		class Page;

		typedef uint16 TileId;

		class Terrain final
		{
		public:
			/// @brief Constructs a new terrain instance.
			/// @param scene The scene to which the terrain belongs.
			/// @param camera The camera used for LOD calculations.
			/// @param width The width of the terrain in pages.
			/// @param height The height of the terrain in pages.
			explicit Terrain(Scene &scene, Camera *camera, uint32 width, uint32 height);
			
			/// @brief Destructor.
			~Terrain();

		public:
			/// @brief Prepares a page for loading by allocating resources.
			/// @param pageX The X coordinate of the page.
			/// @param pageY The Y coordinate of the page.
			void PreparePage(uint32 pageX, uint32 pageY);

			/// @brief Loads a page from disk and adds it to the terrain.
			/// @param pageX The X coordinate of the page.
			/// @param pageY The Y coordinate of the page.
			void LoadPage(uint32 pageX, uint32 pageY);

			/// @brief Unloads a page and frees its resources.
			/// @param pageX The X coordinate of the page.
			/// @param pageY The Y coordinate of the page.
			void UnloadPage(uint32 pageX, uint32 pageY);

			/// @brief Gets the height value at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The height value at the specified position.
			float GetAt(uint32 x, uint32 z);

			/// @brief Gets the slope at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The slope value at the specified position.
			float GetSlopeAt(uint32 x, uint32 z);

			/// @brief Gets the height at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The height value at the specified position.
			float GetHeightAt(uint32 x, uint32 z);

			/// @brief Gets the color value at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The color value at the specified position.
			uint32 GetColorAt(int x, int z);

			/// @brief Gets the layer information at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The layer information as a packed uint32 value.
			uint32 GetLayersAt(uint32 x, uint32 z) const;

			/// @brief Sets the layer value at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @param layer The layer index (0-3).
			/// @param value The layer value to set (0.0 to 1.0).
			void SetLayerAt(uint32 x, uint32 z, uint8 layer, float value) const;

			/// @brief Gets the value of a specific layer at a world position.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @param layer Layer index (0-3).
			/// @return Layer value as a float (0.0 to 1.0).
			[[nodiscard]] float GetLayerValueAt(float x, float z, uint8 layer) const;

			/// @brief Gets the smoothly interpolated height at a world position.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @return The interpolated height value.
			float GetSmoothHeightAt(float x, float z);

			/// @brief Gets the position vector at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The position vector at the specified coordinate.
			Vector3 GetVectorAt(uint32 x, uint32 z);

			/// @brief Gets the smoothly interpolated normal at a world position.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @return The interpolated normal vector.
			Vector3 GetSmoothNormalAt(float x, float z);

			/// @brief Gets the tangent vector at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return The tangent vector at the specified coordinate.
			Vector3 GetTangentAt(uint32 x, uint32 z);

			/// @brief Gets the tile at a specific coordinate.
			/// @param x The X coordinate.
			/// @param z The Z coordinate.
			/// @return Pointer to the tile, or nullptr if not found.
			Tile *GetTile(int32 x, int32 z);

			/// @brief Gets the page at a specific coordinate.
			/// @param x The X coordinate of the page.
			/// @param z The Z coordinate of the page.
			/// @return Pointer to the page, or nullptr if not found.
			[[nodiscard]] Page *GetPage(uint32 x, uint32 z) const;

			/// @brief Converts a world position to page indices.
			/// @param position The world position.
			/// @param x Output parameter for the page X index.
			/// @param y Output parameter for the page Y index.
			/// @return True if the position is within the terrain bounds, false otherwise.
			bool GetPageIndexByWorldPosition(const Vector3 &position, int32 &x, int32 &y) const;

			/// @brief Sets the visibility of the terrain.
			/// @param visible True to make the terrain visible, false to hide it.
			void SetVisible(bool visible) const;

			/// @brief Sets the visibility of the wireframe overlay.
			/// @param visible True to show the wireframe, false to hide it.
			void SetWireframeVisible(bool visible);

			/// @brief Checks if the wireframe overlay is visible.
			/// @return True if the wireframe is visible, false otherwise.
			bool IsWireframeVisible() const { return m_showWireframe; }

			/// Returns the global tile index x and y for the given world position. Global tile index means that 0,0 is the top left corner of the terrain,
			///	and that the maximum value for x and y is m_width * constants::TilesPerPage - 1 and m_height * constants::TilesPerPage - 1 respectively.
			bool GetTileIndexByWorldPosition(const Vector3 &position, int32 &x, int32 &y) const;

			/// @brief Converts a global tile index to a local tile index within a page.
			/// @param globalX The global tile X index.
			/// @param globalY The global tile Y index.
			/// @param localX Output parameter for the local tile X index.
			/// @param localY Output parameter for the local tile Y index.
			/// @return True if the conversion was successful, false otherwise.
			bool GetLocalTileIndexByGlobalTileIndex(int32 globalX, int32 globalY, int32 &localX, int32 &localY) const;

			/// @brief Gets the page index from a global tile index.
			/// @param globalX The global tile X index.
			/// @param globalY The global tile Y index.
			/// @param pageX Output parameter for the page X index.
			/// @param pageY Output parameter for the page Y index.
			/// @return True if the conversion was successful, false otherwise.
			bool GetPageIndexFromGlobalTileIndex(int32 globalX, int32 globalY, int32 &pageX, int32 &pageY) const;

			/// @brief Sets the base file name for terrain data files.
			/// @param name The base file name to use.
			void SetBaseFileName(const String &name);

			/// @brief Gets the base file name for terrain data files.
			/// @return The base file name.
			[[nodiscard]] const String &GetBaseFileName() const;

			/// @brief Gets the default material used for terrain rendering.
			/// @return The default material.
			[[nodiscard]] MaterialPtr GetDefaultMaterial() const;

			/// @brief Sets the default material used for terrain rendering.
			/// @param material The material to set as default.
			void SetDefaultMaterial(MaterialPtr material);

			/// @brief Enables or disables level of detail (LOD) for the terrain.
			/// @param enabled True to enable LOD, false to disable it.
			void SetLodEnabled(bool enabled);
			
			/// @brief Checks if level of detail (LOD) is enabled.
			/// @return True if LOD is enabled, false otherwise.
			bool IsLodEnabled() const;
			
			/// @brief Sets the visibility of debug LOD information.
			/// @param visible True to show debug LOD information, false to hide it.
			void SetDebugLodIsVisible(bool visible);
			
			/// @brief Checks if debug LOD information is visible.
			/// @return True if debug LOD is visible, false otherwise.
			bool IsDebugLodVisible() const;

			/// @brief Gets the scene to which this terrain belongs.
			/// @return Reference to the scene.
			[[nodiscard]] Scene &GetScene() const;

			/// @brief Gets the scene node that represents this terrain.
			/// @return Pointer to the scene node.
			[[nodiscard]] SceneNode *GetNode() const;

			/// @brief Gets the width of the terrain in pages.
			/// @return The width in pages.
			[[nodiscard]] uint32 GetWidth() const;

			/// @brief Gets the height of the terrain in pages.
			/// @return The height in pages.
			[[nodiscard]] uint32 GetHeight() const;

			/// @brief Sets the scene query flags for tiles.
			/// @param mask The query flags mask to set.
			void SetTileSceneQueryFlags(uint32 mask);

			/// @brief Gets the scene query flags for tiles.
			/// @return The query flags mask.
			[[nodiscard]] uint32 GetTileSceneQueryFlags() const { return m_tileSceneQueryFlags; }

			/// @brief Result structure for ray intersection tests.
			struct RayIntersectsResult
			{
				Tile *tile;
				Vector3 position;
			};

			/// @brief Tests if a ray intersects with the terrain.
			/// @param ray The ray to test.
			/// @return A pair containing a boolean indicating if there was an intersection, and the result structure with intersection details.
			std::pair<bool, RayIntersectsResult> RayIntersects(const Ray &ray);

			/// @brief Converts world coordinates to terrain vertex indices.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @param vertexX Output parameter for the vertex X index.
			/// @param vertexZ Output parameter for the vertex Z index.
			static void GetTerrainVertex(float x, float z, uint32 &vertexX, uint32 &vertexZ);

			/// @brief Deforms the terrain by raising or lowering it in a brush area.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param power The strength of the deformation effect.
			void Deform(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power);

			/// @brief Smooths the terrain height in a brush area.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param power The strength of the smoothing effect.
			void Smooth(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power);

			/// @brief Flattens the terrain to a target height in a brush area.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param power The strength of the flattening effect.
			/// @param targetHeight The target height to flatten to.
			void Flatten(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power, float targetHeight);

			/// @brief Paints a texture layer on the terrain in a brush area.
			/// @param layer The layer index (0-3) to paint.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param power The strength of the painting effect.
			void Paint(uint8 layer, float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power);

			/// @brief Colors the terrain vertices in a brush area.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param power The strength of the coloring effect.
			/// @param color The color to apply as a packed uint32 value.
			void Color(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power, uint32 color);

			/// @brief Sets the height at a specific coordinate.
			/// @param x The X coordinate.
			/// @param y The Y coordinate.
			/// @param height The height value to set.
			void SetHeightAt(int x, int y, float height) const;

			/// @brief Sets the color at a specific coordinate.
			/// @param x The X coordinate.
			/// @param y The Y coordinate.
			/// @param color The color value to set.
			void SetColorAt(int x, int y, uint32 color) const;

			/// @brief Updates the inner vertices in a specified region.
			/// @param fromX The starting X coordinate.
			/// @param fromZ The starting Z coordinate.
			/// @param toX The ending X coordinate.
			/// @param toZ The ending Z coordinate.
			void UpdateInnerVertices(int fromX, int fromZ, int toX, int toZ);

			/// @brief Sets the navigation area at a world position.
			/// @param position The world position.
			/// @param area The area ID to set.
			void SetArea(const Vector3 &position, uint32 area) const;

			/// @brief Sets the navigation area for a specific tile.
			/// @param globalTileX The global tile X index.
			/// @param globalTileY The global tile Y index.
			/// @param area The area ID to set.
			void SetAreaForTile(uint32 globalTileX, uint32 globalTileY, uint32 area) const;

			/// @brief Gets the navigation area at a world position.
			/// @param position The world position.
			/// @return The area ID at the specified position.
			[[nodiscard]] uint32 GetArea(const Vector3 &position) const;

			/// @brief Gets the navigation area for a specific tile.
			/// @param globalTileX The global tile X index.
			/// @param globalTileY The global tile Y index.
			/// @return The area ID for the specified tile.
			[[nodiscard]] uint32 GetAreaForTile(uint32 globalTileX, uint32 globalTileY) const;

			/// @brief Sets the material used for wireframe rendering.
			/// @param wireframeMaterial The wireframe material to set.
			void SetWireframeMaterial(const MaterialPtr &wireframeMaterial);

			/// @brief Gets the material used for wireframe rendering.
			/// @return The wireframe material.
			[[nodiscard]] const MaterialPtr &GetWireframeMaterial() const { return m_wireframeMaterial; }

			/// @brief Add or remove terrain holes in a circular brush area
			/// @param brushCenterX World X position of brush center
			/// @param brushCenterZ World Z position of brush center
			/// @param radius Radius of the brush
			/// @param addHole True to add holes, false to remove holes
			void PaintHoles(float brushCenterX, float brushCenterZ, float radius, bool addHole);

			/// @brief Check if a world position is over a terrain hole
			/// @param x World X position
			/// @param z World Z position
			/// @return True if the position is over a hole, false otherwise
			[[nodiscard]] bool IsHoleAt(float x, float z) const;

		private:
			/// @brief Updates tiles in a specified region.
			/// @param fromX The starting X coordinate.
			/// @param fromZ The starting Z coordinate.
			/// @param toX The ending X coordinate.
			/// @param toZ The ending Z coordinate.
			void UpdateTiles(int fromX, int fromZ, int toX, int toZ);

			/// @brief Updates tile coverage information in a specified region.
			/// @param fromX The starting X coordinate.
			/// @param fromZ The starting Z coordinate.
			/// @param toX The ending X coordinate.
			/// @param toZ The ending Z coordinate.
			void UpdateTileCoverage(int fromX, int fromZ, int toX, int toZ) const;

			/// @brief Applies a brush operation to terrain vertices.
			/// @tparam GetBrushIntensity Functor type for calculating brush intensity.
			/// @tparam VertexFunction Functor type for applying vertex modifications.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param updateTiles True to update tiles after applying the brush.
			/// @param getBrushIntensity Functor that calculates brush intensity based on distance.
			/// @param vertexFunction Functor that modifies vertices.
			template <typename GetBrushIntensity, typename VertexFunction>
			void TerrainVertexBrush(const float brushCenterX, const float brushCenterZ, float innerRadius, float outerRadius, bool updateTiles, const GetBrushIntensity &getBrushIntensity, const VertexFunction &vertexFunction)
			{
				// Convert brush center from world space to *global* vertex indices
				// We'll do it by shifting the range so that x=0 => left edge of the terrain
				// and x = m_width*(OuterVerticesPerPageSide-1)*scale => right edge.

				const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
				const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

				// scale = pageSize / (OuterVerticesPerPageSide - 1)
				constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::OuterVerticesPerPageSide - 1));

				// Move brush center from [-halfTerrain, +halfTerrain] into [0, totalWidthInVertices]
				float globalCenterX = (brushCenterX + halfTerrainWidth) / scale;
				float globalCenterZ = (brushCenterZ + halfTerrainHeight) / scale;

				// Compute min/max vertex indices in global index space (floor, ceil, clamp)
				int minVertX = static_cast<int>(std::floor(globalCenterX - (outerRadius / scale)));
				int maxVertX = static_cast<int>(std::ceil(globalCenterX + (outerRadius / scale)));
				minVertX = std::max(0, minVertX);
				maxVertX = std::min<int>(maxVertX, m_width * (constants::OuterVerticesPerPageSide - 1));

				int minVertZ = static_cast<int>(std::floor(globalCenterZ - (outerRadius / scale)));
				int maxVertZ = static_cast<int>(std::ceil(globalCenterZ + (outerRadius / scale)));
				minVertZ = std::max(0, minVertZ);
				maxVertZ = std::min<int>(maxVertZ, m_height * (constants::OuterVerticesPerPageSide - 1));

				// Loop over each outer vertex that could be within the outer radius
				for (int vx = minVertX; vx <= maxVertX; vx++)
				{
					for (int vz = minVertZ; vz <= maxVertZ; vz++)
					{
						// Convert these vertex indices back to world positions
						float worldX, worldZ;
						GetGlobalVertexWorldPosition(vx, vz, &worldX, &worldZ);

						// Compute distance from brush center
						float dx = worldX - brushCenterX;
						float dz = worldZ - brushCenterZ;
						float dist = sqrt(dx * dx + dz * dz);

						// Determine if this vertex is within the brush area
						if (dist <= outerRadius)
						{
							// Compute a weight factor for the current terrain vertex based on the brush center, inner and outer radius and so on
							const float factor = getBrushIntensity(dist, innerRadius, outerRadius);
							vertexFunction(vx, vz, factor);
						}
					}
				}

				// Also process inner vertices (cell-centered between outer vertices)
				// Inner vertices are at indices 0-127 in each dimension per page (not 0-128 like outer)
				// In global space, inner vertex (ix, iz) is positioned between outer vertices (ix, iz) and (ix+1, iz+1)
				const int minInnerX = std::max(0, minVertX - 1);
				const int maxInnerX = std::min(static_cast<int>(m_width * (constants::OuterVerticesPerPageSide - 1) - 1), maxVertX);
				const int minInnerZ = std::max(0, minVertZ - 1);
				const int maxInnerZ = std::min(static_cast<int>(m_height * (constants::OuterVerticesPerPageSide - 1) - 1), maxVertZ);

				for (int ix = minInnerX; ix < maxInnerX; ix++)
				{
					for (int iz = minInnerZ; iz < maxInnerZ; iz++)
					{
						// Inner vertex (ix, iz) is positioned at the center of the quad formed by
						// outer vertices (ix, iz), (ix+1, iz), (ix, iz+1), (ix+1, iz+1)
						float v0x, v0z, v1x, v1z, v2x, v2z, v3x, v3z;
						GetGlobalVertexWorldPosition(ix, iz, &v0x, &v0z);
						GetGlobalVertexWorldPosition(ix + 1, iz, &v1x, &v1z);
						GetGlobalVertexWorldPosition(ix, iz + 1, &v2x, &v2z);
						GetGlobalVertexWorldPosition(ix + 1, iz + 1, &v3x, &v3z);

						// Compute inner vertex position as average of 4 surrounding outer vertices
						float worldX = (v0x + v1x + v2x + v3x) * 0.25f;
						float worldZ = (v0z + v1z + v2z + v3z) * 0.25f;

						// Compute distance from brush center
						float dx = worldX - brushCenterX;
						float dz = worldZ - brushCenterZ;
						float dist = sqrt(dx * dx + dz * dz);

						// Determine if this inner vertex is within the brush area
						if (dist <= outerRadius)
						{
							// Compute a weight factor for the current inner vertex
							const float factor = getBrushIntensity(dist, innerRadius, outerRadius);

							// We need to encode inner vertices specially since they're not in the outer grid
							// Use a sentinel value: negative indices indicate an inner vertex
							// Inner vertex at (ix, iz) is encoded as (-(ix+1), -(iz+1))
							const int32 innerVx = -(ix + 1);
							const int32 innerVz = -(iz + 1);
							vertexFunction(innerVx, innerVz, factor);
						}
					}
				}

				if (updateTiles)
				{
					UpdateTiles(minVertX, minVertZ, maxVertX, maxVertZ);
				}
			}

			/// @brief Applies a brush operation to terrain pixels.
			/// @tparam GetBrushIntensity Functor type for calculating brush intensity.
			/// @tparam PixelFunction Functor type for applying pixel modifications.
			/// @param brushCenterX World X position of the brush center.
			/// @param brushCenterZ World Z position of the brush center.
			/// @param innerRadius The inner radius of the brush where full effect is applied.
			/// @param outerRadius The outer radius of the brush where effect fades out.
			/// @param updateTiles True to update tiles after applying the brush.
			/// @param getBrushIntensity Functor that calculates brush intensity based on distance.
			/// @param pixelFunction Functor that modifies pixels.
			template <typename GetBrushIntensity, typename PixelFunction>
			void TerrainPixelBrush(const float brushCenterX, const float brushCenterZ, float innerRadius, float outerRadius, bool updateTiles, const GetBrushIntensity &getBrushIntensity, const PixelFunction &pixelFunction)
			{
				// Convert brush center from world space to *global* vertex indices
				// We'll do it by shifting the range so that x=0 => left edge of the terrain
				// and x = m_width*(PixelsPerPage-1)*scale => right edge.

				const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
				const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

				// scale = pageSize / (PixelsPerPage - 1)
				constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::PixelsPerPage - 1));

				// Move brush center from [-halfTerrain, +halfTerrain] into [0, totalWidthInVertices]
				float globalCenterX = (brushCenterX + halfTerrainWidth) / scale;
				float globalCenterZ = (brushCenterZ + halfTerrainHeight) / scale;

				// Compute min/max vertex indices in global index space (floor, ceil, clamp)
				int minVertX = static_cast<int>(std::floor(globalCenterX - (outerRadius / scale)));
				int maxVertX = static_cast<int>(std::ceil(globalCenterX + (outerRadius / scale)));
				minVertX = std::max(0, minVertX);
				maxVertX = std::min<int>(maxVertX, m_width * (constants::PixelsPerPage - 1));

				int minVertZ = static_cast<int>(std::floor(globalCenterZ - (outerRadius / scale)));
				int maxVertZ = static_cast<int>(std::ceil(globalCenterZ + (outerRadius / scale)));
				minVertZ = std::max(0, minVertZ);
				maxVertZ = std::min<int>(maxVertZ, m_height * (constants::PixelsPerPage - 1));

				// Loop over each vertex that could be within the outer radius
				for (int vx = minVertX; vx <= maxVertX; vx++)
				{
					for (int vz = minVertZ; vz <= maxVertZ; vz++)
					{
						// Convert these vertex indices back to world positions
						float worldX, worldZ;
						GetGlobalPixelWorldPosition(vx, vz, &worldX, &worldZ);

						// Compute distance from brush center
						float dx = worldX - brushCenterX;
						float dz = worldZ - brushCenterZ;
						float dist = sqrt(dx * dx + dz * dz);

						// Determine if this vertex is within the brush area
						if (dist <= outerRadius)
						{
							// Compute a weight factor for the current terrain vertex based on the brush center, inner and outer radius and so on
							const float factor = getBrushIntensity(dist, innerRadius, outerRadius);
							pixelFunction(vx, vz, factor);
						}
					}
				}

				if (updateTiles)
				{
					UpdateTileCoverage(minVertX, minVertZ, maxVertX, maxVertZ);
				}
			}

			/// @brief Converts global pixel coordinates to world position.
			/// @param x The pixel X coordinate.
			/// @param y The pixel Y coordinate.
			/// @param out_x Optional output parameter for world X position.
			/// @param out_z Optional output parameter for world Z position.
			void GetGlobalPixelWorldPosition(int x, int y, float *out_x = nullptr, float *out_z = nullptr) const;

			/// @brief Converts global vertex coordinates to world position.
			/// @param x The vertex X coordinate.
			/// @param y The vertex Y coordinate.
			/// @param out_x Optional output parameter for world X position.
			/// @param out_z Optional output parameter for world Z position.
			void GetGlobalVertexWorldPosition(int x, int y, float *out_x = nullptr, float *out_z = nullptr) const;

			/// @brief Tests if a ray intersects with an axis-aligned bounding box.
			/// @param ray The ray to test.
			/// @param tmin Output parameter for the minimum intersection distance.
			/// @param tmax Output parameter for the maximum intersection distance.
			/// @return True if the ray intersects the AABB, false otherwise.
			bool RayAABBIntersection(const Ray &ray, float &tmin, float &tmax) const;

			/// @brief Tests if a ray intersects with a triangle.
			/// @param ray The ray to test.
			/// @param v0 First vertex of the triangle.
			/// @param v1 Second vertex of the triangle.
			/// @param v2 Third vertex of the triangle.
			/// @param t Output parameter for the intersection distance.
			/// @param intersectionPoint Output parameter for the intersection point.
			/// @return True if the ray intersects the triangle, false otherwise.
			static bool RayTriangleIntersection(const Ray &ray, const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, float &t, Vector3 &intersectionPoint);

			/// @brief Gets the height value at a specific grid point.
			/// @param gridX The grid X coordinate.
			/// @param gridZ The grid Z coordinate.
			/// @return The height value at the specified grid point.
			[[nodiscard]] float GetHeightAtGridPoint(int gridX, int gridZ) const;

		private:
			friend class Tile;

			typedef Grid<std::unique_ptr<Page>> Pages;

			Pages m_pages;
			String m_baseFileName;
			Scene &m_scene;
			SceneNode *m_terrainNode;
			Camera *m_camera;
			uint32 m_width;
			uint32 m_height;
			int32 m_lastX;
			int32 m_lastZ;
			uint32 m_tileSceneQueryFlags = 0;
			bool m_lodEnabled { true };
			bool m_debugLod { false };
			MaterialPtr m_defaultMaterial;
			bool m_showWireframe = false;
			MaterialPtr m_wireframeMaterial;
		};
	}

}
