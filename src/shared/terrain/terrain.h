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
			explicit Terrain(Scene &scene, Camera *camera, uint32 width, uint32 height);
			~Terrain();

		public:
			void PreparePage(uint32 pageX, uint32 pageY);

			void LoadPage(uint32 pageX, uint32 pageY);

			void UnloadPage(uint32 pageX, uint32 pageY);

			float GetAt(uint32 x, uint32 z);

			float GetSlopeAt(uint32 x, uint32 z);

			float GetHeightAt(uint32 x, uint32 z);

			uint32 GetColorAt(int x, int z);

			uint32 GetLayersAt(uint32 x, uint32 z) const;

			void SetLayerAt(uint32 x, uint32 z, uint8 layer, float value) const;

			/// @brief Gets the value of a specific layer at a world position.
			/// @param x World X coordinate.
			/// @param z World Z coordinate.
			/// @param layer Layer index (0-3).
			/// @return Layer value as a float (0.0 to 1.0).
			[[nodiscard]] float GetLayerValueAt(float x, float z, uint8 layer) const;

			float GetSmoothHeightAt(float x, float z);

			Vector3 GetVectorAt(uint32 x, uint32 z);

			Vector3 GetSmoothNormalAt(float x, float z);

			Vector3 GetTangentAt(uint32 x, uint32 z);

			Tile *GetTile(int32 x, int32 z);

			[[nodiscard]] Page *GetPage(uint32 x, uint32 z) const;

			bool GetPageIndexByWorldPosition(const Vector3 &position, int32 &x, int32 &y) const;

			void SetVisible(bool visible) const;

			void SetWireframeVisible(bool visible);

			bool IsWireframeVisible() const { return m_showWireframe; }

			/// Returns the global tile index x and y for the given world position. Global tile index means that 0,0 is the top left corner of the terrain,
			///	and that the maximum value for x and y is m_width * constants::TilesPerPage - 1 and m_height * constants::TilesPerPage - 1 respectively.
			bool GetTileIndexByWorldPosition(const Vector3 &position, int32 &x, int32 &y) const;

			bool GetLocalTileIndexByGlobalTileIndex(int32 globalX, int32 globalY, int32 &localX, int32 &localY) const;

			bool GetPageIndexFromGlobalTileIndex(int32 globalX, int32 globalY, int32 &pageX, int32 &pageY) const;

			void SetBaseFileName(const String &name);

			[[nodiscard]] const String &GetBaseFileName() const;

			[[nodiscard]] MaterialPtr GetDefaultMaterial() const;

			void SetDefaultMaterial(MaterialPtr material);

			[[nodiscard]] Scene &GetScene() const;

			[[nodiscard]] SceneNode *GetNode() const;

			[[nodiscard]] uint32 GetWidth() const;

			[[nodiscard]] uint32 GetHeight() const;

			void SetTileSceneQueryFlags(uint32 mask);

			[[nodiscard]] uint32 GetTileSceneQueryFlags() const { return m_tileSceneQueryFlags; }

			struct RayIntersectsResult
			{
				Tile *tile;
				Vector3 position;
			};

			std::pair<bool, RayIntersectsResult> RayIntersects(const Ray &ray);

			static void GetTerrainVertex(float x, float z, uint32 &vertexX, uint32 &vertexZ);

			void Deform(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power);

			void Smooth(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power);

			void Flatten(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power, float targetHeight);

			void Paint(uint8 layer, float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power);

			void Paint(uint8 layer, float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power, float minSloap, float maxSloap);

			void Color(float brushCenterX, float brushCenterZ, float innerRadius, float outerRadius, float power, uint32 color);

			void SetHeightAt(int x, int y, float height) const;

			void SetColorAt(int x, int y, uint32 color) const;

			void UpdateInnerVertices(int fromX, int fromZ, int toX, int toZ);

			void SetArea(const Vector3 &position, uint32 area) const;

			void SetAreaForTile(uint32 globalTileX, uint32 globalTileY, uint32 area) const;

			[[nodiscard]] uint32 GetArea(const Vector3 &position) const;

			[[nodiscard]] uint32 GetAreaForTile(uint32 globalTileX, uint32 globalTileY) const;

			void SetWireframeMaterial(const MaterialPtr &wireframeMaterial);

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
			void UpdateTiles(int fromX, int fromZ, int toX, int toZ);

			void UpdateTileCoverage(int fromX, int fromZ, int toX, int toZ) const;

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

			void GetGlobalPixelWorldPosition(int x, int y, float *out_x = nullptr, float *out_z = nullptr) const;

			void GetGlobalVertexWorldPosition(int x, int y, float *out_x = nullptr, float *out_z = nullptr) const;

			// Helper methods
			bool RayAABBIntersection(const Ray &ray, float &tmin, float &tmax) const;

			static bool RayTriangleIntersection(const Ray &ray, const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, float &t, Vector3 &intersectionPoint);

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
			MaterialPtr m_defaultMaterial;
			bool m_showWireframe = false;
			MaterialPtr m_wireframeMaterial;
		};
	}

}
