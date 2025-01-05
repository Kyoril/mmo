// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/grid.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <memory>

#include "graphics/material.h"

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

		namespace constants
		{
			constexpr uint32 VerticesPerTile = 18;
			constexpr uint32 TilesPerPage = 16;
			constexpr uint32 VerticesPerPage = (VerticesPerTile - 1) * TilesPerPage + 1;
			constexpr uint32 MaxPages = 64;
			constexpr uint32 MaxPagesSquared = MaxPages * MaxPages;
			constexpr double TileSize = 33.33333f;
			constexpr double PageSize = TileSize * TilesPerPage;
		}


		typedef uint16 TileId;


		class Terrain final
		{
		public:
			explicit Terrain(Scene& scene, Camera* camera, uint32 width, uint32 height);
			~Terrain();

		public:

			void PreparePage(uint32 tileX, uint32 tileZ);

			void LoadPage(uint32 x, uint32 y);

			void UnloadPage(uint32 x, uint32 y);

			float GetAt(uint32 x, uint32 z);

			float GetSlopeAt(uint32 x, uint32 z);

			float GetHeightAt(uint32 x, uint32 z);

			const Vector4& GetLayersAt(uint32 x, uint32 z);

			void SetLayerAt(uint32 x, uint32 z, uint8 layer, float value);

			float GetSmoothHeightAt(float x, float z);

			Vector3 GetVectorAt(uint32 x, uint32 z);

			Vector3 GetNormalAt(uint32 x, uint32 z);

			Vector3 GetSmoothedNormalAt(uint32 x, uint32 z);

			Vector3 GetTangentAt(uint32 x, uint32 z);

			Tile* GetTile(int32 x, int32 z);

			Page* GetPage(uint32 x, uint32 z);

			bool GetPageIndexByWorldPosition(const Vector3& position, int32& x, int32& y) const;

			void SetVisible(const bool visible) const;

			/// Returns the global tile index x and y for the given world position. Global tile index means that 0,0 is the top left corner of the terrain,
			///	and that the maximum value for x and y is m_width * constants::TilesPerPage - 1 and m_height * constants::TilesPerPage - 1 respectively.
			bool GetTileIndexByWorldPosition(const Vector3& position, int32& x, int32& y) const;

			bool GetLocalTileIndexByGlobalTileIndex(int32 globalX, int32 globalY, int32& localX, int32& localY) const;

			bool GetPageIndexFromGlobalTileIndex(int32 globalX, int32 globalY, int32& pageX, int32& pageY) const;

			void SetBaseFileName(const String& name);

			const String& GetBaseFileName() const;

			MaterialPtr GetDefaultMaterial() const;

			void SetDefaultMaterial(MaterialPtr material);

			Scene& GetScene();

			SceneNode* GetNode();

			uint32 GetWidth() const;

			uint32 GetHeight() const;

			void SetTileSceneQueryFlags(uint32 mask);

			uint32 GetTileSceneQueryFlags() const { return m_tileSceneQueryFlags; }

			struct RayIntersectsResult
			{
				Tile* tile;
				Vector3 position;
			};

			std::pair<bool, RayIntersectsResult> RayIntersects(const Ray& ray);

			void GetTerrainVertex(float x, float z, uint32& vertexX, uint32& vertexZ);

			void Deform(int x, int z, int innerRadius, int outerRadius, float power);

			void Smooth(int x, int z, int innerRadius, int outerRadius, float power);

			void Flatten(int x, int z, int innerRadius, int outerRadius, float power, float avgHeight);

			void Paint(uint8 layer, int x, int z, int innerRadius, int outerRadius, float power);

			void Paint(uint8 layer, int x, int z, int innerRadius, int outerRadius, float power, float minSloap, float maxSloap);

			void SetHeightAt(int x, int y, float height);

			void UpdateTiles(int fromX, int fromZ, int toX, int toZ);

		private:

			// Helper methods
			bool RayAABBIntersection(const Ray& ray, float& tmin, float& tmax) const;

			bool RayTriangleIntersection(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2, float& t, Vector3& intersectionPoint) const;

			float GetHeightAtGridPoint(int gridX, int gridZ) const;

		private:

			typedef Grid<std::unique_ptr<Page>> Pages;

			Pages m_pages;
			String m_baseFileName;
			Scene& m_scene;
			SceneNode* m_terrainNode;
			Camera* m_camera;
			uint32 m_width;
			uint32 m_height;
			int32 m_lastX;
			int32 m_lastZ;
			uint32 m_tileSceneQueryFlags = 0;
			MaterialPtr m_defaultMaterial;
		};
	}

}
