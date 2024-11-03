// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "terrain.h"

#include <utility>

#include "page.h"
#include "tile.h"
#include "game/constants.h"
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

		MaterialPtr Terrain::GetDefaultMaterial() const
		{
			return m_defaultMaterial;
		}

		void Terrain::SetDefaultMaterial(MaterialPtr material)
		{
			m_defaultMaterial = std::move(material);
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

		void Terrain::SetTileSceneQueryFlags(uint32 mask)
		{
			m_tileSceneQueryFlags = mask;

			// For each page, update the tile selection query
			for (unsigned int i = 0; i < m_width; i++)
			{
				for (unsigned int j = 0; j < m_height; j++)
				{
					m_pages(i, j)->UpdateTileSelectionQuery();
				}
			}
		}

		std::pair<bool, Terrain::RayIntersectsResult> Terrain::RayIntersects(const Ray& ray)
		{
			float closestHit = 10000.0f;
			Vector3 hitPoint = Vector3::Zero;

			const Vector3& orig = ray.origin;
			const Vector3& dir = ray.direction;

			Page* hitPage = nullptr;

			for (unsigned int x = 0; x < m_width; x += 1)
			{
				for (unsigned int y = 0; y < m_height; y += 1)
				{
					// Get page
					Page* page = m_pages(x, y).get();
					if (page)
					{
						// Get axis aligned box of that page
						const AABB& box = page->GetBoundingBox();

						// Get start and direction
						Vector3 point = ray.origin;

						// Check if the ray hits the box
						std::pair<bool, float> res = ray.IntersectsAABB(box);
						if (!res.first)
						{
							continue;
						}

						// Get the hit point
						point = ray.GetPoint(res.second);

						while (true)
						{
							// Get height
							float height = GetSmoothHeightAt(point.x, point.z);
							if (point.y < height)
							{
								// We are under the terrain! Correct y field and return hit
								point.y = height;

								// Get distance
								float distance = ray.origin.GetSquaredDistanceTo(point);
								if (distance < closestHit)
								{
									closestHit = distance;
									hitPoint = point;
									hitPage = page;
								}

								break;
							}

							// Walk along the line
							point += dir / 4.0f;

							// Check if we left the box
							if (point.x < box.min.x || point.z < box.min.z ||
								point.x > box.max.x || point.z > box.max.z)
							{
								break;
							}
						}
					}
				}
			}

			if (hitPoint != Vector3::Zero)
			{
				Tile* tile = nullptr;
				if (hitPage)
				{
					tile = hitPage->GetTileAt(hitPoint.x, hitPoint.z);
				}

				return std::make_pair(true, RayIntersectsResult(tile, hitPoint));
			}

			// We didn't hit anything
			return std::make_pair(false, RayIntersectsResult(nullptr, Vector3::Zero));
		}

		static float GetBrushIntensity(int x, int y, int innerRadius, int outerRadius)
		{
			float factor = 1.0f;
			float dist = ::sqrt(
				::pow(static_cast<float>(outerRadius - x), 2.0f) +
				::pow(static_cast<float>(outerRadius - y), 2.0f)
			);

			if (dist > innerRadius)
			{
				dist -= innerRadius;
				factor = Clamp<float>(1.0f - (dist / static_cast<float>(outerRadius - innerRadius)), 0.0f, 1.0f);
			}

			return factor;
		}

		void Terrain::Deform(int x, int z, int innerRadius, int outerRadius, float power)
		{
			x -= outerRadius;
			z -= outerRadius;

			for (int vertX = std::max<int>(0, x); vertX < x + outerRadius * 2; vertX++)
			{
				if (vertX > static_cast<int>(m_width * (constants::VerticesPerPage - 1)) + 1) {
					continue;
				}

				for (int vertZ = std::max<int>(0, z); vertZ < z + outerRadius * 2; vertZ++)
				{
					if (vertZ > static_cast<int>(m_height * (constants::VerticesPerPage - 1)) + 1) {
						continue;
					}

					// Get height at given point
					float height = GetHeightAt(vertX, vertZ);

					// Update height
					float factor = GetBrushIntensity(vertX - x, vertZ - z, innerRadius, outerRadius);
					height += power * factor;

					// Apply change
					SetHeightAt(vertX, vertZ, height);
				}
			}

			// Update terrain
			UpdateTiles(x, z, x + outerRadius * 2, z + outerRadius * 2);
		}

		void Terrain::SetHeightAt(int x, int y, float height)
		{
			bool isRightEdge = (x % constants::VerticesPerPage == 0) && (x > 0);
			bool isBottomEdge = (y % constants::VerticesPerPage == 0) && (y > 0);

			// Determine page
			int pageX = x / (constants::VerticesPerPage - 1);
			int pageY = y / (constants::VerticesPerPage - 1);
			int pageVertX = x - pageX * constants::VerticesPerPage;
			int pageVertY = y - pageY * constants::VerticesPerPage;
			Page* page = GetPage(pageX, pageY);
			if (page &&
				page->IsPrepared())
			{
				page->SetHeightAt(pageVertX, pageVertY, height);
			}

			// Vertex on right edge
			if (isRightEdge)
			{
				pageX--;
				page = GetPage(pageX, pageY);
				if (page &&
					page->IsPrepared())
				{
					pageVertX += constants::VerticesPerPage - 1;
					page->SetHeightAt(pageVertX, pageVertY, height);
				}
			}

			// Vertex on bottom edge
			if (isBottomEdge)
			{
				pageY--;
				page = GetPage(pageX, pageY);
				if (page &&
					page->IsPrepared())
				{
					pageVertY += constants::VerticesPerPage;
					page->SetHeightAt(pageVertX, pageVertY, height);
				}
			}

			// All four pages!
			if (isRightEdge && isBottomEdge)
			{
				pageX++;
				pageVertX = 0;
				page = GetPage(pageX, pageY);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(pageVertX, pageVertY, height);
				}
			}
		}

		void Terrain::UpdateTiles(int fromX, int fromZ, int toX, int toZ)
		{
			// Cap
			if (fromX < 0) fromX = 0;
			if (fromZ < 0) fromZ = 0;

			// Convert to page coordinates
			int fromPageX = fromX / constants::VerticesPerPage;
			int fromPageZ = fromZ / constants::VerticesPerPage;
			unsigned int toPageX = toX / constants::VerticesPerPage;
			unsigned int toPageZ = toZ / constants::VerticesPerPage;

			// Iterate through all pages in the area
			for (unsigned int x = fromPageX; x <= toPageX; x++)
			{
				// Invalid page
				if (x >= m_width)
				{
					continue;
				}

				// Get page start vertex (X)
				unsigned int pageStartX = std::max<int>(fromX - x * constants::VerticesPerPage, 0);
				unsigned int pageEndX = toX - x * constants::VerticesPerPage;

				for (unsigned int z = fromPageZ; z <= toPageZ; z++)
				{
					// Invalid page
					if (z >= m_height)
					{
						continue;
					}

					// Get page start vertex (Z)
					unsigned int pageStartZ = std::max<int>(fromZ - z * constants::VerticesPerPage, 0);
					unsigned int pageEndZ = toZ - z * constants::VerticesPerPage;

					// Update the tiles if necessary
					Page* pPage = GetPage(x, z);
					if (pPage != nullptr)
					{
						if (pPage->IsLoaded()) 
						{
							pPage->UpdateTiles(pageStartX, pageStartZ, pageEndX, pageEndZ);
						}
					}
				}
			}
		}
	}
}
