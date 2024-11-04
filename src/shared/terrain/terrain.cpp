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

		static void GetPageAndLocalVertex(uint32 vertexIndex, uint32& pageIndex, uint32& localVertexIndex)
		{
			pageIndex = std::min(vertexIndex / (constants::VerticesPerPage - 1), 63u);
			localVertexIndex = vertexIndex - pageIndex * (constants::VerticesPerPage - 1);
		}

		float Terrain::GetAt(uint32 x, uint32 z)
		{
			// Validate indices
			const uint32 TotalVertices = m_width * (constants::VerticesPerPage - 1) + 1;
			if (x >= TotalVertices || z >= TotalVertices)
			{
				return 0.0f;
			}

			// Compute page and local vertex indices
			uint32 pageX, pageY, localVertexX, localVertexY;
			GetPageAndLocalVertex(x, pageX, localVertexX);
			GetPageAndLocalVertex(z, pageY, localVertexY);

			// Retrieve the page at (pageX, pageY)
			Page* page = GetPage(pageX, pageY); // Implement GetPage accordingly

			// Retrieve the height at the local vertex within the page
			float height = page->GetHeightAt(localVertexX, localVertexY);

			return height;
		}

		float Terrain::GetSlopeAt(uint32 x, uint32 z)
		{
			return 0.0f;
		}

		float Terrain::GetHeightAt(uint32 x, uint32 z)
		{
			return GetAt(x, z);
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

		void Terrain::GetTerrainVertex(float x, float z, uint32& vertexX, uint32& vertexZ)
		{
			// Get page coordinate from world coordinate
			const int32 pageX = 32 - static_cast<int32>(floor(x / terrain::constants::PageSize));
			const int32 pageY = 32 - static_cast<int32>(floor(z / terrain::constants::PageSize));

			// Now calculate the offset relative to the pages origin
			const float pageOriginX = pageX * terrain::constants::PageSize;
			const float pageOriginZ = pageY * terrain::constants::PageSize;

			// Now get the vertex scale of the page
			const float scale = terrain::constants::PageSize / (terrain::constants::VerticesPerPage - 1);

			vertexX = static_cast<int32>((x - pageOriginX) / scale) + (pageX * constants::VerticesPerPage);
			vertexZ = static_cast<int32>((z - pageOriginZ) / scale) + (pageY * constants::VerticesPerPage);
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
				if (vertX > static_cast<int>(m_width * (constants::VerticesPerPage - 1)) + 1)
				{
					continue;
				}

				for (int vertZ = std::max<int>(0, z); vertZ < z + outerRadius * 2; vertZ++)
				{
					if (vertZ > static_cast<int>(m_height * (constants::VerticesPerPage - 1)) + 1)
					{
						continue;
					}

					// Get height at given point
					float height = GetHeightAt(vertX, vertZ);

					// Update height
					float factor = 1.0f;// GetBrushIntensity(vertX - x, vertZ - z, innerRadius, outerRadius);
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
			const uint32 TotalVertices = m_width * (constants::VerticesPerPage - 1) + 1;
			if (x >= TotalVertices || y >= TotalVertices)
			{
				return;
			}

			// Compute page and local vertex indices
			uint32 pageX, pageY, localVertexX, localVertexY;
			GetPageAndLocalVertex(x, pageX, localVertexX);
			GetPageAndLocalVertex(y, pageY, localVertexY);
			Page* page = GetPage(pageX, pageY);
			if (page &&
				page->IsPrepared())
			{
				page->SetHeightAt(localVertexX, localVertexY, height);
			}

			// Vertex on right edge
			if (isRightEdge)
			{
				pageX++;
				page = GetPage(pageX, pageY);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(0, localVertexY, height);
				}
			}

			// Vertex on bottom edge
			if (isBottomEdge)
			{
				pageY++;
				page = GetPage(pageX, pageY);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(localVertexX, localVertexY, height);
				}
			}

			// All four pages!
			if (isRightEdge && isBottomEdge)
			{
				page = GetPage(pageX, pageY);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(0, 0, height);
				}
			}
		}

		void Terrain::UpdateTiles(int fromX, int fromZ, int toX, int toZ)
		{
			uint32 fromPageX, fromPageZ, localVertexX, localVertexY;
			GetPageAndLocalVertex(fromX, fromPageX, localVertexX);
			GetPageAndLocalVertex(fromZ, fromPageZ, localVertexY);

			uint32 toPageX, toPageZ, localVertexToX, localVertexToY;
			GetPageAndLocalVertex(toX, toPageX, localVertexX);
			GetPageAndLocalVertex(toZ, toPageZ, localVertexY);

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
							pPage->UpdateTiles(0, 0, constants::TilesPerPage * constants::VerticesPerTile, constants::TilesPerPage * constants::VerticesPerTile, false);
						}
					}
				}
			}
		}
	}
}
