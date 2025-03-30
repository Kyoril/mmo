// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain.h"

#include <utility>

#include "page.h"
#include "tile.h"
#include "game/constants.h"
#include "log/default_log_levels.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"

namespace mmo
{
	namespace terrain
	{
		Terrain::Terrain(Scene& scene, Camera* camera, const uint32 width, const uint32 height)
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

			m_defaultMaterial = m_scene.GetDefaultMaterial();
			ASSERT(m_defaultMaterial);

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

		void Terrain::PreparePage(const uint32 pageX, const uint32 pageY)
		{
			if (pageX >= m_width || pageY >= m_height) 
			{
				return;
			}

			m_pages(pageX, pageY)->Prepare();
		}

		void Terrain::LoadPage(const uint32 pageX, const uint32 pageY)
		{
			if (pageX >= m_width || pageY >= m_height)
			{
				return;
			}

			Page* const page = m_pages(pageX, pageY).get();
			if (!page->IsPrepared())
			{
				return;
			}

			page->Load();
		}

		void Terrain::UnloadPage(const uint32 pageX, const uint32 pageY)
		{
			if (pageX >= m_width || pageY >= m_height)
			{
				return;
			}

			m_pages(pageX, pageY)->Unload();
		}

		namespace
		{
			void GetPageAndLocalVertex(const uint32 vertexIndex, uint32& pageIndex, uint32& localVertexIndex)
			{
				pageIndex = std::min(vertexIndex / (constants::VerticesPerPage - 1), 63u);
				localVertexIndex = (vertexIndex - (pageIndex * (constants::VerticesPerPage - 1))) % constants::VerticesPerPage;
			}

			void GetPageAndLocalPixel(const uint32 pixelIndex, uint32& pageIndex, uint32& localPixelIndex)
			{
				pageIndex = std::min(pixelIndex / (constants::PixelsPerPage - 1), 63u);
				localPixelIndex = (pixelIndex - (pageIndex * (constants::PixelsPerPage - 1))) % constants::PixelsPerPage;
			}
		}

		float Terrain::GetAt(const uint32 x, const uint32 z)
		{
			// Validate indices
			const uint32 totalVertices = m_width * (constants::VerticesPerPage - 1) + 1;
			if (x >= totalVertices || z >= totalVertices)
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
			// TODO!
			return 0.0f;
		}

		float Terrain::GetHeightAt(const uint32 x, const uint32 z)
		{
			return GetAt(x, z);
		}

		const uint32 Terrain::GetLayersAt(const uint32 x, const uint32 z)
		{
			// Validate indices
			const uint32 TotalVertices = m_width * (constants::PixelsPerPage - 1) + 1;
			if (x >= TotalVertices || z >= TotalVertices)
			{
				return 0;
			}

			// Compute page and local vertex indices
			uint32 pageX, pageY, localVertexX, localVertexY;
			GetPageAndLocalPixel(x, pageX, localVertexX);
			GetPageAndLocalPixel(z, pageY, localVertexY);

			// Retrieve the page at (pageX, pageY)
			Page* page = GetPage(pageX, pageY); // Implement GetPage accordingly

			// Retrieve the height at the local vertex within the page
			return page->GetLayersAt(localVertexX, localVertexY);
		}

		void Terrain::SetLayerAt(const uint32 x, const uint32 y, const uint8 layer, const float value)
		{
			ASSERT(layer < 4);

			// Determine page
			const uint32 TotalVertices = m_width * (constants::PixelsPerPage - 1) + 1;
			if (x >= TotalVertices || y >= TotalVertices)
			{
				return;
			}

			// Compute page and local vertex indices
			uint32 pageX, pageY, localPixelX, localPixelY;
			GetPageAndLocalPixel(x, pageX, localPixelX);
			GetPageAndLocalPixel(y, pageY, localPixelY);

			const bool isLeftEdge = localPixelX == 0 && pageX > 0;
			const bool isTopEdge = localPixelY == 0 && pageY > 0;

			Page* page = GetPage(pageX, pageY);
			if (page &&
				page->IsPrepared())
			{
				page->SetLayerAt(localPixelX, localPixelY, layer, value);
			}

			// Vertex on left edge
			if (isLeftEdge)
			{
				page = GetPage(pageX - 1, pageY);
				if (page &&
					page->IsPrepared())
				{
					page->SetLayerAt(constants::PixelsPerPage - 1, localPixelY, layer, value);
				}
			}

			// Vertex on top edge
			if (isTopEdge)
			{
				page = GetPage(pageX, pageY - 1);
				if (page &&
					page->IsPrepared())
				{
					page->SetLayerAt(localPixelX, constants::PixelsPerPage - 1, layer, value);
				}
			}

			// All four pages!
			if (isLeftEdge && isTopEdge)
			{
				page = GetPage(pageX - 1, pageY - 1);
				if (page &&
					page->IsPrepared())
				{
					page->SetLayerAt(constants::PixelsPerPage - 1, constants::PixelsPerPage - 1, layer, value);
				}
			}
		}

		float Terrain::GetSmoothHeightAt(const float x, const float z)
		{
			int32 pageX, pageY;
			if (!GetPageIndexByWorldPosition(Vector3(x, 0.0f, z), pageX, pageY))
			{
				// TODO
				return 0.0f;
			}

			Page* page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				// TODO
				return 0.0f;
			}

			const float value = page->GetSmoothHeightAt(
				fmod(x + constants::PageSize * pageX, terrain::constants::PageSize),
				fmod(z + constants::PageSize * pageY, terrain::constants::PageSize)
				);

			return value;
		}

		Vector3 Terrain::GetVectorAt(uint32 x, uint32 z)
		{
			return Vector3();
		}

		Vector3 Terrain::GetSmoothNormalAt(float x, float z)
		{
			int32 pageX, pageY;
			if (!GetPageIndexByWorldPosition(Vector3(x, 0.0f, z), pageX, pageY))
			{
				// TODO
				return Vector3::UnitY;
			}

			Page* page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				// TODO
				return Vector3::UnitY;
			}

			return page->GetSmoothNormalAt(
				fmod(x + constants::PageSize * pageX, terrain::constants::PageSize),
				fmod(z + constants::PageSize * pageY, terrain::constants::PageSize));
		}

		Vector3 Terrain::GetTangentAt(uint32 x, uint32 z)
		{
			return Vector3();
		}

		Tile* Terrain::GetTile(const int32 x, const int32 z)
		{
			// Can that tile possibly exist?
			if (x < 0 || z < 0 || x >= m_width * constants::TilesPerPage || z >= m_height * constants::TilesPerPage)
			{
				return nullptr;
			}

			const uint32 pageX = x / constants::TilesPerPage;
			const uint32 pageZ = z / constants::TilesPerPage;

			// Check if page is loaded
			Page* page = GetPage(pageX, pageZ);
			if (!page->IsLoaded())
			{
				return nullptr;
			}

			// Normalize tile index
			const uint32 tileX = x % constants::TilesPerPage;
			const uint32 tileY = z % constants::TilesPerPage;
			return page->GetTile(tileX, tileY);
		}

		Page* Terrain::GetPage(const uint32 x, const uint32 z) const
		{
			if (x >= m_width || z >= m_height)
			{
				return nullptr;
			}

			return m_pages(x, z).get();
		}

		bool Terrain::GetPageIndexByWorldPosition(const Vector3& position, int32& x, int32& y) const
		{
			x = floor(position.x / terrain::constants::PageSize) + (m_width / 2);
			y = static_cast<uint32>(floor(position.z / terrain::constants::PageSize)) + (m_height / 2);
			return true;
		}

		void Terrain::SetVisible(const bool visible) const
		{
			m_terrainNode->SetVisible(visible, true);
		}

		bool Terrain::GetTileIndexByWorldPosition(const Vector3& position, int32& x, int32& y) const
		{
			const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
			const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

			const int32 px = static_cast<int32>((position.x + halfTerrainWidth) / constants::TileSize);
			const int32 py = static_cast<int32>((position.z + halfTerrainHeight) / constants::TileSize);

			if (px < 0 || py < 0 || px >= m_width * constants::TilesPerPage || py >= m_height * constants::TilesPerPage)
			{
				return false;
			}

			x = px;
			y = py;
			return true;
		}

		bool Terrain::GetLocalTileIndexByGlobalTileIndex(const int32 globalX, const int32 globalY, int32& localX, int32& localY) const
		{
			if (globalX < 0 || globalY < 0 || globalX >= m_width * constants::TilesPerPage || globalY >= m_height * constants::TilesPerPage)
			{
				return false;
			}

			localX = globalX % static_cast<int32>(constants::TilesPerPage);
			localY = globalY % static_cast<int32>(constants::TilesPerPage);
			return true;
		}

		bool Terrain::GetPageIndexFromGlobalTileIndex(const int32 globalX, const int32 globalY, int32& pageX, int32& pageY) const
		{
			if (globalX < 0 || globalY < 0 || globalX >= m_width * constants::TilesPerPage || globalY >= m_height * constants::TilesPerPage)
			{
				return false;
			}

			pageX = globalX / static_cast<int32>(constants::TilesPerPage);
			pageY = globalY / static_cast<int32>(constants::TilesPerPage);
			return true;
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

		void Terrain::SetTileSceneQueryFlags(const uint32 mask)
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
			float closestHit = std::numeric_limits<float>::max();
			Vector3 hitPoint = Vector3::Zero;
			Page* hitPage = nullptr;

			// First do a broad phase - find which pages the ray potentially intersects
			std::vector<std::pair<Page*, float>> potentialPages;
			potentialPages.reserve(16); // Reserve space for some pages to avoid reallocation

			for (unsigned int x = 0; x < m_width; x++)
			{
				for (unsigned int y = 0; y < m_height; y++)
				{
					// Get page
					Page* page = m_pages(x, y).get();
					if (!page || !page->IsPrepared())
					{
						continue;
					}

					// Get axis aligned box of that page
					const AABB& box = page->GetBoundingBox();

					// Check if the ray hits the box
					std::pair<bool, float> boxHit = ray.IntersectsAABB(box);
					if (boxHit.first)
					{
						// Store the page and distance for sorting
						potentialPages.emplace_back(page, boxHit.second);
					}
				}
			}

			// Early out if no pages hit
			if (potentialPages.empty())
			{
				return std::make_pair(false, RayIntersectsResult(nullptr, Vector3::Zero));
			}

			// Sort pages by distance from ray origin
			std::sort(potentialPages.begin(), potentialPages.end(),
				[](const auto& a, const auto& b) { return a.second < b.second; });

			// Detailed check phase - check triangles in pages ordered by distance
			for (const auto& [page, distance] : potentialPages)
			{
				// Skip if we already found a hit closer than this page's bounding box
				if (distance > closestHit)
				{
					continue;
				}

				// Get the page coordinates
				unsigned int pageX = 0, pageY = 0;
				for (unsigned int x = 0; x < m_width; x++)
				{
					for (unsigned int y = 0; y < m_height; y++)
					{
						if (m_pages(x, y).get() == page)
						{
							pageX = x;
							pageY = y;
							break;
						}
					}
				}

				// Performance optimization: Only check every 4th vertex for initial pass
				// Then refine around potential hits
				static const int COARSE_STEP = 4;
				bool potentialHit = false;
				float coarseHitT = std::numeric_limits<float>::max();
				unsigned int coarseHitX = 0, coarseHitZ = 0;

				// Coarse pass - check every 4th vertex
				for (unsigned int vx = 0; vx < constants::VerticesPerPage - 1; vx += COARSE_STEP)
				{
					for (unsigned int vz = 0; vz < constants::VerticesPerPage - 1; vz += COARSE_STEP)
					{
						// Ensure we don't go out of bounds
						if (vx + COARSE_STEP >= constants::VerticesPerPage || vz + COARSE_STEP >= constants::VerticesPerPage)
							continue;

						// Get the four corners of this quad
						const uint32 globalVx = pageX * (constants::VerticesPerPage - 1) + vx;
						const uint32 globalVz = pageY * (constants::VerticesPerPage - 1) + vz;

						// Get world positions for the quad vertices
						float wx1, wz1, wx2, wz2;
						GetGlobalVertexWorldPosition(globalVx, globalVz, &wx1, &wz1);
						GetGlobalVertexWorldPosition(globalVx + COARSE_STEP, globalVz + COARSE_STEP, &wx2, &wz2);

						// Get heights for the four corners
						const float h1 = GetHeightAt(globalVx, globalVz);
						const float h2 = GetHeightAt(globalVx + COARSE_STEP, globalVz);
						const float h3 = GetHeightAt(globalVx, globalVz + COARSE_STEP);
						const float h4 = GetHeightAt(globalVx + COARSE_STEP, globalVz + COARSE_STEP);

						// Create the four corner vertices
						const Vector3 v1(wx1, h1, wz1);
						const Vector3 v2(wx2, h2, wz1);
						const Vector3 v3(wx1, h3, wz2);
						const Vector3 v4(wx2, h4, wz2);

						// Check first triangle (v1, v2, v3)
						float t1;
						Vector3 intersectionPoint1;
						if (RayTriangleIntersection(ray, v1, v2, v3, t1, intersectionPoint1))
						{
							if (t1 < coarseHitT)
							{
								coarseHitT = t1;
								coarseHitX = vx;
								coarseHitZ = vz;
								potentialHit = true;
							}
						}

						// Check second triangle (v2, v4, v3)
						float t2;
						Vector3 intersectionPoint2;
						if (RayTriangleIntersection(ray, v2, v4, v3, t2, intersectionPoint2))
						{
							if (t2 < coarseHitT)
							{
								coarseHitT = t2;
								coarseHitX = vx;
								coarseHitZ = vz;
								potentialHit = true;
							}
						}
					}
				}

				// If we found a potential hit in the coarse pass, refine it
				if (potentialHit)
				{
					// Define the refinement area
					unsigned int startX = (coarseHitX > COARSE_STEP) ? (coarseHitX - COARSE_STEP) : 0;
					unsigned int startZ = (coarseHitZ > COARSE_STEP) ? (coarseHitZ - COARSE_STEP) : 0;
					unsigned int endX = std::min(coarseHitX + COARSE_STEP, constants::VerticesPerPage - 2);
					unsigned int endZ = std::min(coarseHitZ + COARSE_STEP, constants::VerticesPerPage - 2);

					// Detailed pass - check every vertex in the refined area
					for (unsigned int vx = startX; vx <= endX; vx++)
					{
						for (unsigned int vz = startZ; vz <= endZ; vz++)
						{
							// Get the four corners of this quad
							const uint32 globalVx = pageX * (constants::VerticesPerPage - 1) + vx;
							const uint32 globalVz = pageY * (constants::VerticesPerPage - 1) + vz;

							// Get world positions for the quad vertices
							float wx1, wz1, wx2, wz2;
							GetGlobalVertexWorldPosition(globalVx, globalVz, &wx1, &wz1);
							GetGlobalVertexWorldPosition(globalVx + 1, globalVz + 1, &wx2, &wz2);

							// Get heights for the four corners
							const float h1 = GetHeightAt(globalVx, globalVz);
							const float h2 = GetHeightAt(globalVx + 1, globalVz);
							const float h3 = GetHeightAt(globalVx, globalVz + 1);
							const float h4 = GetHeightAt(globalVx + 1, globalVz + 1);

							// Create the four corner vertices
							const Vector3 v1(wx1, h1, wz1);
							const Vector3 v2(wx2, h2, wz1);
							const Vector3 v3(wx1, h3, wz2);
							const Vector3 v4(wx2, h4, wz2);

							// Check first triangle (v1, v2, v3)
							float t1;
							Vector3 intersectionPoint1;
							if (RayTriangleIntersection(ray, v1, v2, v3, t1, intersectionPoint1))
							{
								if (t1 < closestHit)
								{
									closestHit = t1;
									hitPoint = intersectionPoint1;
									hitPage = page;
								}
							}

							// Check second triangle (v2, v4, v3)
							float t2;
							Vector3 intersectionPoint2;
							if (RayTriangleIntersection(ray, v2, v4, v3, t2, intersectionPoint2))
							{
								if (t2 < closestHit)
								{
									closestHit = t2;
									hitPoint = intersectionPoint2;
									hitPage = page;
								}
							}
						}
					}

					// Early out if we found a hit in this page
					if (hitPage == page)
					{
						break;
					}
				}
			}

			if (hitPoint != Vector3::Zero)
			{
				Tile* tile = nullptr;
				if (hitPage)
				{
					int32 globalTileX, globalTileY;
					if (GetTileIndexByWorldPosition(hitPoint, globalTileX, globalTileY))
					{
						int32 localTileX, localTileY;
						if (GetLocalTileIndexByGlobalTileIndex(globalTileX, globalTileY, localTileX, localTileY))
						{
							tile = hitPage->GetTile(localTileX, localTileY);
						}
					}
				}

				return std::make_pair(true, RayIntersectsResult(tile, hitPoint));
			}

			// We didn't hit anything
			return std::make_pair(false, RayIntersectsResult(nullptr, Vector3::Zero));
		}


		void Terrain::GetTerrainVertex(const float x, const float z, uint32& vertexX, uint32& vertexZ)
		{
			// Get page coordinate from world coordinate
			const int32 pageX = 32 - static_cast<int32>(floor(x / terrain::constants::PageSize));
			const int32 pageY = 32 - static_cast<int32>(floor(z / terrain::constants::PageSize));

			// Now calculate the offset relative to the pages origin
			const float pageOriginX = pageX * terrain::constants::PageSize;
			const float pageOriginZ = pageY * terrain::constants::PageSize;

			// Now get the vertex scale of the page
			const float scale = static_cast<float>(terrain::constants::PageSize / static_cast<double>(terrain::constants::VerticesPerPage - 1));

			vertexX = static_cast<int32>((x - pageOriginX) / scale) + (pageX * constants::VerticesPerPage);
			vertexZ = static_cast<int32>((z - pageOriginZ) / scale) + (pageY * constants::VerticesPerPage);
		}

		namespace
		{
			float GetBrushIntensityLinear(const float dist, const float innerRadius, const float outerRadius)
			{
				// If the distance is beyond the outer radius, intensity is zero
				if (dist >= outerRadius)
					return 0.0f;

				// If the distance is within the inner radius, intensity is maximum (1.0)
				if (dist <= innerRadius)
					return 1.0f;

				// Otherwise, linearly interpolate between [innerRadius, outerRadius]
				// so that dist = innerRadius => 1.0, dist = outerRadius => 0.0
				float t = (dist - innerRadius) / (outerRadius - innerRadius);
				return 1.0f - t;
			}
		}
		
		static float GetBrushIntensity(const int x, const int y, const int innerRadius, const int outerRadius)
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

		void Terrain::Deform(const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power)
		{
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, power](const int32 vx, const int32 vy, const float factor)
				{
					float height = GetHeightAt(vx, vy);
					height += (power * factor);
					SetHeightAt(vx, vy, height);
				});
		}

		void Terrain::Smooth(const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power)
		{
			// First collect average height value
			float sumHeight = 0.0f;
			uint32 heightCount = 0;
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, false, &GetBrushIntensityLinear, [this, &sumHeight, &heightCount](const int32 vx, const int32 vy, float)
				{
					float height = GetHeightAt(vx, vy);
					sumHeight += height;
					heightCount++;
				});

			const float avgHeight = sumHeight / static_cast<float>(heightCount);
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, avgHeight, power](const int32 vx, const int32 vy, const float factor)
				{
					const float height = GetHeightAt(vx, vy);
					float delta = height - avgHeight;
					delta *= factor * power;
					SetHeightAt(vx, vy, height - delta);
				});
		}

		void Terrain::Flatten(const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power, float targetHeight)
		{
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, targetHeight, power](const int32 vx, const int32 vy, const float factor)
				{
					const float height = GetHeightAt(vx, vy);
					float delta = height - targetHeight;
					delta *= factor * power;
					SetHeightAt(vx, vy, height - delta);
				});
		}

		void Terrain::Paint(const uint8 layer, const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, const float power)
		{
			Paint(layer, brushCenterX, brushCenterZ, innerRadius, outerRadius, power, 0.0f, 1.0f);
		}

		void Terrain::Paint(uint8 layer, const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power, float minSloap, float maxSloap)
		{
			TerrainPixelBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, layer, power](const int32 vx, const int32 vy, const float factor)
				{
					const uint32 layers = GetLayersAt(vx, vy);

					Vector4 v;
					v.x = ((layers >> 0) & 0xFF) / 255.0f;
					v.y = ((layers >> 8) & 0xFF) / 255.0f;
					v.z = ((layers >> 16) & 0xFF) / 255.0f;
					v.w = ((layers >> 24) & 0xFF) / 255.0f;

					float value = v[layer];
					value += power * factor;

					SetLayerAt(vx, vy, layer, Clamp(value, 0.0f, 1.0f));
				});
		}

		void Terrain::SetHeightAt(const int x, const int y, const float height)
		{
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

			const bool isLeftEdge = localVertexX == 0 && pageX > 0;
			const bool isTopEdge = localVertexY == 0 && pageY > 0;

			Page* page = GetPage(pageX, pageY);
			if (page &&
				page->IsPrepared())
			{
				page->SetHeightAt(localVertexX, localVertexY, height);
			}

			// Vertex on left edge
			if (isLeftEdge)
			{
				page = GetPage(pageX - 1, pageY);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(constants::VerticesPerPage - 1, localVertexY, height);
				}
			}

			// Vertex on top edge
			if (isTopEdge)
			{
				page = GetPage(pageX, pageY - 1);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(localVertexX, constants::VerticesPerPage - 1, height);
				}
			}

			// All four pages!
			if (isLeftEdge && isTopEdge)
			{
				page = GetPage(pageX - 1, pageY - 1);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(constants::VerticesPerPage - 1, constants::VerticesPerPage - 1, height);
				}
			}
		}

		void Terrain::UpdateTiles(const int fromX, const int fromZ, const int toX, const int toZ)
		{
			uint32 fromPageX, fromPageZ, localVertexX, localVertexY;
			GetPageAndLocalVertex(fromX, fromPageX, localVertexX);
			GetPageAndLocalVertex(fromZ, fromPageZ, localVertexY);

			uint32 toPageX, toPageZ, localVertexToX, localVertexToY;
			GetPageAndLocalVertex(toX, toPageX, localVertexToX);
			GetPageAndLocalVertex(toZ, toPageZ, localVertexToY);

			// Iterate through all pages in the area
			for (unsigned int x = fromPageX; x <= toPageX; x++)
			{
				// Invalid page
				if (x >= m_width)
				{
					continue;
				}

				// Get page start vertex (X)
				unsigned int pageStartX = std::max<int>(fromX - x * (constants::VerticesPerPage - 1), 0);
				unsigned int pageEndX = toX - x * (constants::VerticesPerPage - 1);

				for (unsigned int z = fromPageZ; z <= toPageZ; z++)
				{
					// Invalid page
					if (z >= m_height)
					{
						continue;
					}

					// Get page start vertex (Z)
					unsigned int pageStartZ = std::max<int>(fromZ - z * (constants::VerticesPerPage - 1), 0);
					unsigned int pageEndZ = toZ - z * (constants::VerticesPerPage - 1);

					// Update the tiles if necessary
					Page* pPage = GetPage(x, z);
					if (pPage != nullptr)
					{
						if (pPage->IsLoaded()) 
						{
							pPage->UpdateTiles(pageStartX, pageStartZ, pageEndX, pageEndZ, false);
						}
					}
				}
			}
		}

		void Terrain::UpdateTileCoverage(const int fromX, const int fromZ, const int toX, const int toZ)
		{
			uint32 fromPageX, fromPageZ, localVertexX, localVertexY;
			GetPageAndLocalPixel(fromX, fromPageX, localVertexX);
			GetPageAndLocalPixel(fromZ, fromPageZ, localVertexY);

			uint32 toPageX, toPageZ, localVertexToX, localVertexToY;
			GetPageAndLocalPixel(toX, toPageX, localVertexToX);
			GetPageAndLocalPixel(toZ, toPageZ, localVertexToY);

			// Iterate through all pages in the area
			for (unsigned int x = fromPageX; x <= toPageX; x++)
			{
				// Invalid page
				if (x >= m_width)
				{
					continue;
				}

				// Get page start vertex (X)
				unsigned int pageStartX = std::max<int>(fromX - x * (constants::PixelsPerPage - 1), 0);
				unsigned int pageEndX = toX - x * (constants::PixelsPerPage - 1);

				for (unsigned int z = fromPageZ; z <= toPageZ; z++)
				{
					// Invalid page
					if (z >= m_height)
					{
						continue;
					}

					// Get page start vertex (Z)
					unsigned int pageStartZ = std::max<int>(fromZ - z * (constants::PixelsPerPage - 1), 0);
					unsigned int pageEndZ = toZ - z * (constants::PixelsPerPage - 1);

					// Update the tiles if necessary
					if (Page* pPage = GetPage(x, z); pPage != nullptr)
					{
						if (pPage->IsLoaded())
						{
							pPage->UpdateTileCoverage(pageStartX, pageStartZ, pageEndX, pageEndZ);
						}
					}
				}
			}
		}

		void Terrain::SetArea(const Vector3& position, uint32 area)
		{
			// Calculate tile position from world position
			int32 tileX, tileY;
			if (!GetTileIndexByWorldPosition(position, tileX, tileY))
			{
				return;
			}

			SetAreaForTile(tileX, tileY, area);
		}

		void Terrain::SetAreaForTile(uint32 globalTileX, uint32 globalTileY, uint32 area)
		{
			ASSERT(globalTileX < m_width * constants::TilesPerPage && globalTileY < m_height * constants::TilesPerPage);

			// Determine page from tile
			const uint32 pageX = globalTileX / constants::TilesPerPage;
			const uint32 pageY = globalTileY / constants::TilesPerPage;

			Page* page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				return;
			}

			// Now lets set the actual tile area
			page->SetArea(globalTileX % constants::TilesPerPage, globalTileY % constants::TilesPerPage, area);
		}

		uint32 Terrain::GetArea(const Vector3& position) const
		{
			// Calculate tile position from world position
			int32 tileX, tileY;
			if (!GetTileIndexByWorldPosition(position, tileX, tileY))
			{
				return 0;
			}

			return GetAreaForTile(tileX, tileY);
		}

		uint32 Terrain::GetAreaForTile(const uint32 globalTileX, const uint32 globalTileY) const
		{
			ASSERT(globalTileX < m_width * constants::TilesPerPage && globalTileY < m_height * constants::TilesPerPage);

			// Determine page from tile
			const uint32 pageX = globalTileX / constants::TilesPerPage;
			const uint32 pageY = globalTileY / constants::TilesPerPage;

			const Page* page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				return 0;
			}

			// Now lets get the actual tile area
			return page->GetArea(globalTileX % constants::TilesPerPage, globalTileY % constants::TilesPerPage);
		}

		void Terrain::GetGlobalPixelWorldPosition(const int x, const int y, float* out_x, float* out_z) const
		{
			constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::PixelsPerPage - 1));

			if (out_x)
			{
				const float worldCenterX = static_cast<double>(m_width) / 2.0 * constants::PageSize;
				*out_x = static_cast<float>(x) * scale - worldCenterX;
			}

			if (out_z)
			{
				const float worldCenterY = static_cast<double>(m_height) / 2.0 * constants::PageSize;
				*out_z = static_cast<float>(y) * scale - worldCenterY;
			}
		}

		void Terrain::GetGlobalVertexWorldPosition(const int x, const int y, float* out_x, float* out_z) const
		{
			constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::VerticesPerPage - 1));

			if (out_x)
			{
				const float worldCenterX = static_cast<float>(static_cast<double>(m_width) / 2.0 * constants::PageSize);
				*out_x = static_cast<float>(x) * scale - worldCenterX;
			}

			if (out_z)
			{
				const float worldCenterY = static_cast<float>(static_cast<double>(m_height) / 2.0 * constants::PageSize);
				*out_z = static_cast<float>(y) * scale - worldCenterY;
			}
		}

		bool Terrain::RayTriangleIntersection(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2, float& t, Vector3& intersectionPoint) const
		{
			const float EPSILON = 1e-6f;

			Vector3 edge1 = v1 - v0;
			Vector3 edge2 = v2 - v0;
			Vector3 h = ray.direction.Cross(edge2);
			float a = edge1.Dot(h);

			if (std::abs(a) < EPSILON)
			{
				return false; // Ray is parallel to triangle
			}

			float f = 1.0f / a;
			Vector3 s = ray.origin - v0;
			float u = f * s.Dot(h);

			if (u < 0.0f || u > 1.0f)
			{
				return false;
			}

			Vector3 q = s.Cross(edge1);
			float v = f * ray.direction.Dot(q);

			if (v < 0.0f || u + v > 1.0f)
				return false;

			// At this stage, we can compute t to find out where the intersection point is on the line
			t = f * edge2.Dot(q);

			if (t > EPSILON) // Ray intersection
			{
				intersectionPoint = ray.origin + ray.direction * t;
				return true;
			}
			
			// Line intersection but not a ray intersection
			return false;
		}
	}
}
