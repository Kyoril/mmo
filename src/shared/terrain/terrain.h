// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/grid.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <memory>

namespace mmo
{
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
			explicit Terrain(Scene& scene, Camera* camera, uint32 width, uint32 height);
			~Terrain();

		public:

			void PreparePage(TileId tileX, TileId tileZ);

			void LoadPage(TileId x, TileId y);

			void UnloadPage(TileId x, TileId y);

			float GetAt(uint32 x, uint32 z);

			float GetSlopeAt(uint32 x, uint32 z);

			float GetHeightAt(uint32 x, uint32 z);

			float GetSmoothHeightAt(float x, float z);

			Vector3 GetVectorAt(uint32 x, uint32 z);

			Vector3 GetNormalAt(uint32 x, uint32 z);

			Vector3 GetSmoothedNormalAt(uint32 x, uint32 z);

			Vector3 GetTangentAt(uint32 x, uint32 z);

			Tile* GetTile(int32 x, int32 z);

			Page* GetPage(TileId x, TileId z);

			void SetBaseFileName(const String& name);

			const String& GetBaseFileName() const;

			Scene& GetScene();

			SceneNode* GetNode();

			uint32 GetWidth() const;

			uint32 GetHeight() const;

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
		};
	}

}
