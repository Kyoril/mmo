// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "terrain.h"

#include "page.h"
#include "tile.h"
#include "scene_graph/scene.h"

namespace mmo
{
	namespace terrain
	{
		Terrain::Terrain(Scene& scene, Camera* camera, uint32 width, uint32 height)
			: m_pages(width, height)
			, m_scene(scene)
			, m_terrainNode(nullptr)
			, m_camera(camera)
			, m_width(width)
			, m_height(height)
			, m_lastX(255)
			, m_lastZ(255)
		{
			m_terrainNode = m_scene.GetRootSceneNode().CreateChildSceneNode();

			for (unsigned int i = 0; i < width; i++)
			{
				for (unsigned int j = 0; j < height; j++)
				{
					m_pages(i, j) = std::make_unique<Page>(*this, i, j);
				}
			}
		}

		Terrain::~Terrain()
		{
			m_pages.clear();
			m_scene.DestroySceneNode(*m_terrainNode);
		}

		void Terrain::PreparePage(uint32 x, uint32 y)
		{
			if (!(x < m_width) || !(y < m_height)) {
				return;
			}

			m_pages(x, y)->Prepare();
		}

		void Terrain::LoadPage(uint32 x, uint32 y)
		{
			if (!(x < m_width) || !(y < m_height)) {
				return;
			}

			Page* const page = m_pages(x, y).get();
			if (!page->IsPrepared())
			{
				return;
			}

			page->Load();
		}

		void Terrain::UnloadPage(uint32 x, uint32 y)
		{
			if (!(x < m_width) || !(y < m_height)) {
				return;
			}

			m_pages(x, y)->Unload();
		}

		float Terrain::GetAt(uint32 x, uint32 z)
		{
			return 0.0f;
		}

		float Terrain::GetSlopeAt(uint32 x, uint32 z)
		{
			return 0.0f;
		}

		float Terrain::GetHeightAt(uint32 x, uint32 z)
		{
			return 0.0f;
		}

		float Terrain::GetSmoothHeightAt(float x, float z)
		{
			return 0.0f;
		}

		Vector3 Terrain::GetVectorAt(uint32 x, uint32 z)
		{
			return Vector3();
		}

		Vector3 Terrain::GetNormalAt(uint32 x, uint32 z)
		{
			return Vector3();
		}

		Vector3 Terrain::GetSmoothedNormalAt(uint32 x, uint32 z)
		{
			return Vector3();
		}

		Vector3 Terrain::GetTangentAt(uint32 x, uint32 z)
		{
			return Vector3();
		}

		Tile* Terrain::GetTile(int32 x, int32 z)
		{
			const size_t tileCount = 16;

			unsigned int pageX = (x / tileCount);
			unsigned int pageZ = (z / tileCount);

			if (pageX >= m_width || pageZ >= m_height)
			{
				return nullptr;
			}

			Page* page = GetPage(pageX, pageZ);
			ASSERT(page);

			if (!page->IsLoaded()) {
				return nullptr;
			}

			unsigned int tileX = (x % tileCount);
			unsigned int tileZ = (z % tileCount);

			return page->GetTile(tileX, tileZ);
		}

		Page* Terrain::GetPage(uint32 x, uint32 z)
		{
			if (x >= m_width || z >= m_height) {
				return nullptr;
			}

			return m_pages(x, z).get();
		}

		void Terrain::SetBaseFileName(const String& name)
		{
			m_baseFileName = name;
		}

		const String& Terrain::GetBaseFileName() const
		{
			return m_baseFileName;
		}

		Scene& Terrain::GetScene()
		{
			return m_scene;
		}

		SceneNode* Terrain::GetNode()
		{
			ASSERT(m_terrainNode);
			return m_terrainNode;
		}

		uint32 Terrain::GetWidth() const
		{
			return m_width;
		}

		uint32 Terrain::GetHeight() const
		{
			return m_height;
		}
	}
}
