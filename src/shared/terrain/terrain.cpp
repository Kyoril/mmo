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
		Terrain::Terrain(Scene &scene, Camera *camera, const uint32 width, const uint32 height)
			: m_pages(width, height), m_scene(scene), m_terrainNode(nullptr), m_camera(camera), m_width(width), m_height(height), m_lastX(255), m_lastZ(255)
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

			Page *const page = m_pages(pageX, pageY).get();
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
			void GetPageAndLocalVertex(const uint32 vertexIndex, uint32 &pageIndex, uint32 &localVertexIndex)
			{
				pageIndex = std::min(vertexIndex / (constants::OuterVerticesPerPageSide - 1), 63u);
				localVertexIndex = (vertexIndex - (pageIndex * (constants::OuterVerticesPerPageSide - 1))) % constants::OuterVerticesPerPageSide;
			}

			void GetPageAndLocalPixel(const uint32 pixelIndex, uint32 &pageIndex, uint32 &localPixelIndex)
			{
				pageIndex = std::min(pixelIndex / (constants::PixelsPerPage - 1), 63u);
				localPixelIndex = (pixelIndex - (pageIndex * (constants::PixelsPerPage - 1))) % constants::PixelsPerPage;
			}
		}

		float Terrain::GetAt(const uint32 x, const uint32 z)
		{
			// Validate indices
			const uint32 totalVertices = m_width * (constants::OuterVerticesPerPageSide - 1) + 1;
			if (x >= totalVertices || z >= totalVertices)
			{
				return 0.0f;
			}

			// Compute page and local vertex indices
			uint32 pageX, pageY, localVertexX, localVertexY;
			GetPageAndLocalVertex(x, pageX, localVertexX);
			GetPageAndLocalVertex(z, pageY, localVertexY);

			// Retrieve the page at (pageX, pageY)
			Page *page = GetPage(pageX, pageY); // Implement GetPage accordingly

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

		uint32 Terrain::GetColorAt(int x, int z)
		{
			// Outer vertices use non-negative indices; inner vertices are encoded as negative: (-(ix+1), -(iz+1)).
			if (x >= 0 && z >= 0)
			{
				// Validate outer vertex indices
				const uint32 totalOuterVertices = m_width * (constants::OuterVerticesPerPageSide - 1) + 1;
				if (static_cast<uint32>(x) >= totalOuterVertices || static_cast<uint32>(z) >= totalOuterVertices)
				{
					return 0U;
				}

				// Compute page and local outer vertex indices
				uint32 pageX, pageY, localVertexX, localVertexY;
				GetPageAndLocalVertex(static_cast<uint32>(x), pageX, localVertexX);
				GetPageAndLocalVertex(static_cast<uint32>(z), pageY, localVertexY);

				Page *page = GetPage(pageX, pageY);
				return page ? page->GetColorAt(localVertexX, localVertexY) : 0U;
			}

			// Decode inner vertex indices
			const int ix = -(x + 1);
			const int iz = -(z + 1);

			// Validate inner vertex indices (range: [0, width*(OuterVerticesPerPageSide-1) - 1])
			const int maxInnerX = static_cast<int>(m_width * (constants::OuterVerticesPerPageSide - 1) - 1);
			const int maxInnerZ = static_cast<int>(m_height * (constants::OuterVerticesPerPageSide - 1) - 1);
			if (ix < 0 || iz < 0 || ix > maxInnerX || iz > maxInnerZ)
			{
				return 0U;
			}

			const uint32 pageX = static_cast<uint32>(ix) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 pageZ = static_cast<uint32>(iz) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 localInnerX = static_cast<uint32>(ix) % (constants::OuterVerticesPerPageSide - 1);
			const uint32 localInnerZ = static_cast<uint32>(iz) % (constants::OuterVerticesPerPageSide - 1);

			Page *page = GetPage(pageX, pageZ);
			return page ? page->GetInnerColorAt(localInnerX, localInnerZ) : 0U;
		}

		uint32 Terrain::GetLayersAt(const uint32 x, const uint32 z) const
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
			Page *page = GetPage(pageX, pageY); // Implement GetPage accordingly

			// Retrieve the height at the local vertex within the page
			return page->GetLayersAt(localVertexX, localVertexY);
		}

		void Terrain::SetLayerAt(const uint32 x, const uint32 y, const uint8 layer, const float value) const
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

			Page *page = GetPage(pageX, pageY);
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

			Page *page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				// TODO
				return 0.0f;
			}

			const float value = page->GetSmoothHeightAt(
				fmod(x + constants::PageSize * pageX, terrain::constants::PageSize),
				fmod(z + constants::PageSize * pageY, terrain::constants::PageSize));

			return value;
		}

		float Terrain::GetLayerValueAt(const float x, const float z, const uint8 layer) const
		{
			if (layer > 3)
			{
				return 0.0f;
			}

			const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
			const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

			// Convert world position to pixel position
			constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::PixelsPerPage - 1));
			const int32 pixelX = static_cast<int32>((x + halfTerrainWidth) / scale);
			const int32 pixelZ = static_cast<int32>((z + halfTerrainHeight) / scale);

			// Bounds check
			const int32 totalPixels = m_width * (constants::PixelsPerPage - 1) + 1;
			if (pixelX < 0 || pixelX >= totalPixels || pixelZ < 0 || pixelZ >= totalPixels)
			{
				return 0.0f;
			}

			const uint32 layers = GetLayersAt(pixelX, pixelZ);
			const uint8 layerValue = (layers >> (layer * 8)) & 0xFF;
			return layerValue / 255.0f;
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

			Page *page = GetPage(pageX, pageY);
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

		Tile *Terrain::GetTile(const int32 x, const int32 z)
		{
			// Can that tile possibly exist?
			if (x < 0 || z < 0 || static_cast<uint32>(x) >= m_width * constants::TilesPerPage || static_cast<uint32>(z) >= m_height * constants::TilesPerPage)
			{
				return nullptr;
			}

			const uint32 pageX = x / constants::TilesPerPage;
			const uint32 pageZ = z / constants::TilesPerPage;

			// Check if page is loaded
			Page *page = GetPage(pageX, pageZ);
			if (!page->IsLoaded())
			{
				return nullptr;
			}

			// Normalize tile index
			const uint32 tileX = x % constants::TilesPerPage;
			const uint32 tileY = z % constants::TilesPerPage;
			return page->GetTile(tileX, tileY);
		}

		Page *Terrain::GetPage(const uint32 x, const uint32 z) const
		{
			if (x >= m_width || z >= m_height)
			{
				return nullptr;
			}

			return m_pages(x, z).get();
		}

		bool Terrain::GetPageIndexByWorldPosition(const Vector3 &position, int32 &x, int32 &y) const
		{
			x = floor(position.x / terrain::constants::PageSize) + (m_width / 2);
			y = static_cast<uint32>(floor(position.z / terrain::constants::PageSize)) + (m_height / 2);
			return true;
		}

		void Terrain::SetVisible(const bool visible) const
		{
			m_terrainNode->SetVisible(visible, true);
		}

		void Terrain::SetWireframeVisible(const bool visible)
		{
			m_showWireframe = visible;
		}

		bool Terrain::GetTileIndexByWorldPosition(const Vector3 &position, int32 &x, int32 &y) const
		{
			const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
			const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

			const int32 px = static_cast<int32>((position.x + halfTerrainWidth) / constants::TileSize);
			const int32 py = static_cast<int32>((position.z + halfTerrainHeight) / constants::TileSize);

			if (px < 0 || py < 0 || static_cast<uint32>(px) >= m_width * constants::TilesPerPage || static_cast<uint32>(py) >= m_height * constants::TilesPerPage)
			{
				return false;
			}

			x = px;
			y = py;
			return true;
		}

		bool Terrain::GetLocalTileIndexByGlobalTileIndex(const int32 globalX, const int32 globalY, int32 &localX, int32 &localY) const
		{
			if (globalX < 0 || globalY < 0 || static_cast<uint32>(globalX) >= m_width * constants::TilesPerPage || static_cast<uint32>(globalY) >= m_height * constants::TilesPerPage)
			{
				return false;
			}

			localX = globalX % static_cast<int32>(constants::TilesPerPage);
			localY = globalY % static_cast<int32>(constants::TilesPerPage);
			return true;
		}

		bool Terrain::GetPageIndexFromGlobalTileIndex(const int32 globalX, const int32 globalY, int32 &pageX, int32 &pageY) const
		{
			if (globalX < 0 || globalY < 0 || static_cast<uint32>(globalX) >= m_width * constants::TilesPerPage || static_cast<uint32>(globalY) >= m_height * constants::TilesPerPage)
			{
				return false;
			}

			pageX = globalX / static_cast<int32>(constants::TilesPerPage);
			pageY = globalY / static_cast<int32>(constants::TilesPerPage);
			return true;
		}

		void Terrain::SetLodEnabled(bool enabled)
		{
			m_lodEnabled = enabled;
		}

		bool Terrain::IsLodEnabled() const
		{
			return m_lodEnabled;
		}

		void Terrain::SetDebugLodIsVisible(bool visible)
		{
			m_debugLod = visible;
		}

		bool Terrain::IsDebugLodVisible() const
		{
			return m_debugLod;
		}

		void Terrain::SetBaseFileName(const String &name)
		{
			m_baseFileName = name;
		}

		const String &Terrain::GetBaseFileName() const
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

		Scene &Terrain::GetScene() const
		{
			return m_scene;
		}

		SceneNode *Terrain::GetNode() const
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

		std::pair<bool, Terrain::RayIntersectsResult> Terrain::RayIntersects(const Ray &ray)
		{
			float closestHit = std::numeric_limits<float>::max();
			Vector3 hitPoint = Vector3::Zero;
			Page *hitPage = nullptr;

			// First do a broad phase - find which pages the ray potentially intersects
			std::vector<std::pair<Page *, float>> potentialPages;
			potentialPages.reserve(16); // Reserve space for some pages to avoid reallocation

			for (unsigned int x = 0; x < m_width; x++)
			{
				for (unsigned int y = 0; y < m_height; y++)
				{
					// Get page
					Page *page = m_pages(x, y).get();
					if (!page || !page->IsPrepared())
					{
						continue;
					}

					// Get axis aligned box of that page
					const AABB &box = page->GetBoundingBox();

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
					  [](const auto &a, const auto &b)
					  { return a.second < b.second; });

			// Detailed check phase - check triangles in pages ordered by distance
			for (const auto &[page, distance] : potentialPages)
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
				static constexpr int coarse_step = 4;
				bool potentialHit = false;
				float coarseHitT = std::numeric_limits<float>::max();
				unsigned int coarseHitX = 0, coarseHitZ = 0;

				// Coarse pass - check every 4th cell (outer grid)
				for (unsigned int vx = 0; vx < constants::OuterVerticesPerPageSide - 1; vx += coarse_step)
				{
					for (unsigned int vz = 0; vz < constants::OuterVerticesPerPageSide - 1; vz += coarse_step)
					{
						// Ensure we don't go out of bounds
						if (vx + coarse_step >= constants::OuterVerticesPerPageSide || vz + coarse_step >= constants::OuterVerticesPerPageSide)
							continue;

						// Get the four corners of this quad
						const uint32 globalVx = pageX * (constants::OuterVerticesPerPageSide - 1) + vx;
						const uint32 globalVz = pageY * (constants::OuterVerticesPerPageSide - 1) + vz;

						// Get world positions for the quad vertices
						float wx1, wz1, wx2, wz2;
						GetGlobalVertexWorldPosition(globalVx, globalVz, &wx1, &wz1);
						GetGlobalVertexWorldPosition(globalVx + coarse_step, globalVz + coarse_step, &wx2, &wz2);

						// Get heights for the four corners
						const float h1 = GetHeightAt(globalVx, globalVz);
						const float h2 = GetHeightAt(globalVx + coarse_step, globalVz);
						const float h3 = GetHeightAt(globalVx, globalVz + coarse_step);
						const float h4 = GetHeightAt(globalVx + coarse_step, globalVz + coarse_step);

						// Create the four corner vertices and center inner vertex
						const Vector3 vTL(wx1, h1, wz1);
						const Vector3 vTR(wx2, h2, wz1);
						const Vector3 vBL(wx1, h3, wz2);
						const Vector3 vBR(wx2, h4, wz2);
						const Vector3 vC((wx1 + wx2) * 0.5f, (h1 + h2 + h3 + h4) * 0.25f, (wz1 + wz2) * 0.5f);

						// Check four triangles around the center
						float tTmp;
						Vector3 ip;
						// Top
						if (RayTriangleIntersection(ray, vC, vTR, vTL, tTmp, ip))
						{
							if (tTmp < coarseHitT)
							{
								coarseHitT = tTmp;
								coarseHitX = vx;
								coarseHitZ = vz;
								potentialHit = true;
							}
						}
						// Right
						if (RayTriangleIntersection(ray, vC, vBR, vTR, tTmp, ip))
						{
							if (tTmp < coarseHitT)
							{
								coarseHitT = tTmp;
								coarseHitX = vx;
								coarseHitZ = vz;
								potentialHit = true;
							}
						}
						// Bottom
						if (RayTriangleIntersection(ray, vC, vBL, vBR, tTmp, ip))
						{
							if (tTmp < coarseHitT)
							{
								coarseHitT = tTmp;
								coarseHitX = vx;
								coarseHitZ = vz;
								potentialHit = true;
							}
						}
						// Left
						if (RayTriangleIntersection(ray, vC, vTL, vBL, tTmp, ip))
						{
							if (tTmp < coarseHitT)
							{
								coarseHitT = tTmp;
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
					unsigned int startX = (coarseHitX > coarse_step) ? (coarseHitX - coarse_step) : 0;
					unsigned int startZ = (coarseHitZ > coarse_step) ? (coarseHitZ - coarse_step) : 0;
					unsigned int endX = std::min(coarseHitX + coarse_step, constants::OuterVerticesPerPageSide - 2);
					unsigned int endZ = std::min(coarseHitZ + coarse_step, constants::OuterVerticesPerPageSide - 2);

					// Detailed pass - check every vertex in the refined area
					for (unsigned int vx = startX; vx <= endX; vx++)
					{
						for (unsigned int vz = startZ; vz <= endZ; vz++)
						{
							// Get the four corners of this quad
							const uint32 globalVx = pageX * (constants::OuterVerticesPerPageSide - 1) + vx;
							const uint32 globalVz = pageY * (constants::OuterVerticesPerPageSide - 1) + vz;

							// Get world positions for the quad vertices
							float wx1, wz1, wx2, wz2;
							GetGlobalVertexWorldPosition(globalVx, globalVz, &wx1, &wz1);
							GetGlobalVertexWorldPosition(globalVx + 1, globalVz + 1, &wx2, &wz2);

							// Get heights for the four corners
							const float h1 = GetHeightAt(globalVx, globalVz);
							const float h2 = GetHeightAt(globalVx + 1, globalVz);
							const float h3 = GetHeightAt(globalVx, globalVz + 1);
							const float h4 = GetHeightAt(globalVx + 1, globalVz + 1);

							// Create the four corner vertices and center inner vertex
							const Vector3 vTL(wx1, h1, wz1);
							const Vector3 vTR(wx2, h2, wz1);
							const Vector3 vBL(wx1, h3, wz2);
							const Vector3 vBR(wx2, h4, wz2);
							const Vector3 vC((wx1 + wx2) * 0.5f, (h1 + h2 + h3 + h4) * 0.25f, (wz1 + wz2) * 0.5f);

							// Check four triangles around the center
							Vector3 ip1;
							if (float t1; RayTriangleIntersection(ray, vC, vTR, vTL, t1, ip1))
							{
								if (t1 < closestHit)
								{
									closestHit = t1;
									hitPoint = ip1;
									hitPage = page;
								}
							}

							float t2;
							Vector3 ip2;
							if (RayTriangleIntersection(ray, vC, vBR, vTR, t2, ip2))
							{
								if (t2 < closestHit)
								{
									closestHit = t2;
									hitPoint = ip2;
									hitPage = page;
								}
							}

							float t3;
							Vector3 ip3;
							if (RayTriangleIntersection(ray, vC, vBL, vBR, t3, ip3))
							{
								if (t3 < closestHit)
								{
									closestHit = t3;
									hitPoint = ip3;
									hitPage = page;
								}
							}

							float t4;
							Vector3 ip4;
							if (RayTriangleIntersection(ray, vC, vTL, vBL, t4, ip4))
							{
								if (t4 < closestHit)
								{
									closestHit = t4;
									hitPoint = ip4;
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
				Tile *tile = nullptr;
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

		void Terrain::GetTerrainVertex(const float x, const float z, uint32 &vertexX, uint32 &vertexZ)
		{
			// Get page coordinate from world coordinate
			const int32 pageX = 32 - static_cast<int32>(floor(x / terrain::constants::PageSize));
			const int32 pageY = 32 - static_cast<int32>(floor(z / terrain::constants::PageSize));

			// Now calculate the offset relative to the pages origin
			const float pageOriginX = pageX * terrain::constants::PageSize;
			const float pageOriginZ = pageY * terrain::constants::PageSize;

			// Now get the vertex scale of the page
			const float scale = static_cast<float>(terrain::constants::PageSize / static_cast<double>(terrain::constants::OuterVerticesPerPageSide - 1));

			vertexX = static_cast<int32>((x - pageOriginX) / scale) + (pageX * constants::OuterVerticesPerPageSide);
			vertexZ = static_cast<int32>((z - pageOriginZ) / scale) + (pageY * constants::OuterVerticesPerPageSide);
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
				::pow(static_cast<float>(outerRadius - y), 2.0f));

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
					if (vx >= 0 && vy >= 0)
					{
						// Outer vertex
						float height = GetHeightAt(vx, vy);
						height += (power * factor);
						SetHeightAt(vx, vy, height);
					}
					else
					{
						// Inner vertex (encoded as negative indices)
						const int32 ix = -vx - 1;
						const int32 iz = -vy - 1;
						const uint32 pageX = ix / (constants::OuterVerticesPerPageSide - 1);
						const uint32 pageZ = iz / (constants::OuterVerticesPerPageSide - 1);
						const uint32 localInnerX = ix % (constants::OuterVerticesPerPageSide - 1);
						const uint32 localInnerZ = iz % (constants::OuterVerticesPerPageSide - 1);
						
						Page* page = GetPage(pageX, pageZ);
						if (page && page->IsPrepared())
						{
							float height = page->GetInnerHeightAt(localInnerX, localInnerZ);
							height += (power * factor);
							page->SetInnerHeightAt(localInnerX, localInnerZ, height);
						}
					} });
		}

		void Terrain::Smooth(const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power)
		{
			// First collect average height value
			float sumHeight = 0.0f;
			uint32 heightCount = 0;
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, false, &GetBrushIntensityLinear, [this, &sumHeight, &heightCount](const int32 vx, const int32 vy, float)
							   {
					if (vx >= 0 && vy >= 0)
					{
						// Outer vertex
						float height = GetHeightAt(vx, vy);
						sumHeight += height;
						heightCount++;
					}
					else
					{
						// Inner vertex (encoded as negative indices)
						const int32 ix = -vx - 1;
						const int32 iz = -vy - 1;
						const uint32 pageX = ix / (constants::OuterVerticesPerPageSide - 1);
						const uint32 pageZ = iz / (constants::OuterVerticesPerPageSide - 1);
						const uint32 localInnerX = ix % (constants::OuterVerticesPerPageSide - 1);
						const uint32 localInnerZ = iz % (constants::OuterVerticesPerPageSide - 1);
						
						Page* page = GetPage(pageX, pageZ);
						if (page && page->IsPrepared())
						{
							float height = page->GetInnerHeightAt(localInnerX, localInnerZ);
							sumHeight += height;
							heightCount++;
						}
					} });

			const float avgHeight = sumHeight / static_cast<float>(heightCount);
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, avgHeight, power](const int32 vx, const int32 vy, const float factor)
							   {
					if (vx >= 0 && vy >= 0)
					{
						// Outer vertex
						const float height = GetHeightAt(vx, vy);
						float delta = height - avgHeight;
						delta *= factor * power;
						SetHeightAt(vx, vy, height - delta);
					}
					else
					{
						// Inner vertex (encoded as negative indices)
						const int32 ix = -vx - 1;
						const int32 iz = -vy - 1;
						const uint32 pageX = ix / (constants::OuterVerticesPerPageSide - 1);
						const uint32 pageZ = iz / (constants::OuterVerticesPerPageSide - 1);
						const uint32 localInnerX = ix % (constants::OuterVerticesPerPageSide - 1);
						const uint32 localInnerZ = iz % (constants::OuterVerticesPerPageSide - 1);
						
						Page* page = GetPage(pageX, pageZ);
						if (page && page->IsPrepared())
						{
							const float height = page->GetInnerHeightAt(localInnerX, localInnerZ);
							float delta = height - avgHeight;
							delta *= factor * power;
							page->SetInnerHeightAt(localInnerX, localInnerZ, height - delta);
						}
					} });
		}

		void Terrain::Flatten(const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power, float targetHeight)
		{
			// Track affected area bounds for inner vertex and tile updates
			int minX = std::numeric_limits<int>::max();
			int minZ = std::numeric_limits<int>::max();
			int maxX = std::numeric_limits<int>::min();
			int maxZ = std::numeric_limits<int>::min();

			// Only modify outer vertices; inner vertices will be interpolated afterward
			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, targetHeight, power, &minX, &minZ, &maxX, &maxZ](const int32 vx, const int32 vy, const float factor)
							   {
								   if (vx >= 0 && vy >= 0)
								   {
									   // Outer vertex
									   const float height = GetHeightAt(vx, vy);
									   float delta = height - targetHeight;
									   delta *= factor * power;
									   SetHeightAt(vx, vy, height - delta);

									   // Track bounds
									   minX = std::min(minX, vx);
									   minZ = std::min(minZ, vy);
									   maxX = std::max(maxX, vx);
									   maxZ = std::max(maxZ, vy);
								   }
								   // Skip inner vertices - they will be updated via interpolation
							   });

			// Update inner vertices by interpolating from modified outer vertices
			if (minX <= maxX && minZ <= maxZ)
			{
				// Inner vertices exist between outer vertices, so update range [minX, maxX-1] x [minZ, maxZ-1]
				const int innerMinX = std::max(0, minX - 1);
				const int innerMinZ = std::max(0, minZ - 1);
				const int innerMaxX = maxX;
				const int innerMaxZ = maxZ;

				UpdateInnerVertices(innerMinX, innerMinZ, innerMaxX, innerMaxZ);

				// Update tiles affected by the height changes
				UpdateTiles(minX, minZ, maxX, maxZ);
			}
		}

		void Terrain::Paint(const uint8 layer, const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, const float power)
		{
			Paint(layer, brushCenterX, brushCenterZ, innerRadius, outerRadius, power, 0.0f, 1.0f);
		}

		void Terrain::Paint(const uint8 layer, const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, const float power, const float minValue, const float maxValue)
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

					SetLayerAt(vx, vy, layer, Clamp(value, 0.0f, 1.0f)); });
		}

		namespace
		{
			Vector3 BlendColor(const Vector3 &current, const Vector3 &target, const float dt)
			{
				float newR = current.x + (target.x - current.x) * dt;
				float newG = current.y + (target.y - current.y) * dt;
				float newB = current.z + (target.z - current.z) * dt;

				// Clamp to [0, 1]
				newR = Clamp(newR, 0.0f, 1.0f);
				newG = Clamp(newG, 0.0f, 1.0f);
				newB = Clamp(newB, 0.0f, 1.0f);
				return {newR, newG, newB};
			}
		}

		void Terrain::Color(const float brushCenterX, const float brushCenterZ, const float innerRadius, const float outerRadius, float power, const uint32 color)
		{
			Vector3 i;
			i.x = ((color >> 0) & 0xFF) / 255.0f;
			i.y = ((color >> 8) & 0xFF) / 255.0f;
			i.z = ((color >> 16) & 0xFF) / 255.0f;

			TerrainVertexBrush(brushCenterX, brushCenterZ, innerRadius, outerRadius, true, &GetBrushIntensityLinear, [this, power, i](const int32 vx, const int32 vy, const float factor)
							   {
					const uint32 c = GetColorAt(vx, vy);

					Vector3 v;
					v.x = ((c >> 0) & 0xFF) / 255.0f;
					v.y = ((c >> 8) & 0xFF) / 255.0f;
					v.z = ((c >> 16) & 0xFF) / 255.0f;

					// Blend current color with input color based on factor
					const Vector3 blended = BlendColor(v, i, power * factor);

					// Convert back to uint32 BGRA with alpha=0xff
					const uint32 color =
						(static_cast<uint32>(blended.x * 255.0f)) |
						(static_cast<uint32>(blended.y * 255.0f) << 8) |
						(static_cast<uint32>(blended.z * 255.0f) << 16) |
						(0xFF << 24);

					SetColorAt(vx, vy, color); });
		}

		void Terrain::SetHeightAt(const int x, const int y, const float height) const
		{
			// Determine page
			const uint32 TotalVertices = m_width * (constants::OuterVerticesPerPageSide - 1) + 1;
			if (static_cast<uint32>(x) >= TotalVertices || static_cast<uint32>(y) >= TotalVertices)
			{
				return;
			}

			// Compute page and local vertex indices
			uint32 pageX, pageY, localVertexX, localVertexY;
			GetPageAndLocalVertex(x, pageX, localVertexX);
			GetPageAndLocalVertex(y, pageY, localVertexY);

			const bool isLeftEdge = localVertexX == 0 && pageX > 0;
			const bool isTopEdge = localVertexY == 0 && pageY > 0;

			Page *page = GetPage(pageX, pageY);
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
					page->SetHeightAt(constants::OuterVerticesPerPageSide - 1, localVertexY, height);
				}
			}

			// Vertex on top edge
			if (isTopEdge)
			{
				page = GetPage(pageX, pageY - 1);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(localVertexX, constants::OuterVerticesPerPageSide - 1, height);
				}
			}

			// All four pages!
			if (isLeftEdge && isTopEdge)
			{
				page = GetPage(pageX - 1, pageY - 1);
				if (page &&
					page->IsPrepared())
				{
					page->SetHeightAt(constants::OuterVerticesPerPageSide - 1, constants::OuterVerticesPerPageSide - 1, height);
				}
			}
		}

		void Terrain::SetColorAt(const int x, const int y, const uint32 color) const
		{
			// Handle outer vs inner vertices. Outer use non-negative indices; inner are encoded as negative.
			if (x >= 0 && y >= 0)
			{
				// Determine page for outer vertex
				const uint32 totalOuterVertices = m_width * (constants::OuterVerticesPerPageSide - 1) + 1;
				if (static_cast<uint32>(x) >= totalOuterVertices || static_cast<uint32>(y) >= totalOuterVertices)
				{
					return;
				}

				uint32 pageX, pageY, localVertexX, localVertexY;
				GetPageAndLocalVertex(static_cast<uint32>(x), pageX, localVertexX);
				GetPageAndLocalVertex(static_cast<uint32>(y), pageY, localVertexY);

				const bool isLeftEdge = localVertexX == 0 && pageX > 0;
				const bool isTopEdge = localVertexY == 0 && pageY > 0;

				Page *page = GetPage(pageX, pageY);
				if (page && page->IsPrepared())
				{
					page->SetColorAt(localVertexX, localVertexY, color);
				}

				// Mirror across page boundaries for shared outer vertices
				if (isLeftEdge)
				{
					page = GetPage(pageX - 1, pageY);
					if (page && page->IsPrepared())
					{
						page->SetColorAt(constants::OuterVerticesPerPageSide - 1, localVertexY, color);
					}
				}

				if (isTopEdge)
				{
					page = GetPage(pageX, pageY - 1);
					if (page && page->IsPrepared())
					{
						page->SetColorAt(localVertexX, constants::OuterVerticesPerPageSide - 1, color);
					}
				}

				if (isLeftEdge && isTopEdge)
				{
					page = GetPage(pageX - 1, pageY - 1);
					if (page && page->IsPrepared())
					{
						page->SetColorAt(constants::OuterVerticesPerPageSide - 1, constants::OuterVerticesPerPageSide - 1, color);
					}
				}
				return;
			}

			// Inner vertex path
			const int ix = -(x + 1);
			const int iz = -(y + 1);

			const int maxInnerX = static_cast<int>(m_width * (constants::OuterVerticesPerPageSide - 1) - 1);
			const int maxInnerZ = static_cast<int>(m_height * (constants::OuterVerticesPerPageSide - 1) - 1);
			if (ix < 0 || iz < 0 || ix > maxInnerX || iz > maxInnerZ)
			{
				return;
			}

			const uint32 pageX = static_cast<uint32>(ix) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 pageZ = static_cast<uint32>(iz) / (constants::OuterVerticesPerPageSide - 1);
			const uint32 localInnerX = static_cast<uint32>(ix) % (constants::OuterVerticesPerPageSide - 1);
			const uint32 localInnerZ = static_cast<uint32>(iz) % (constants::OuterVerticesPerPageSide - 1);

			Page *page = GetPage(pageX, pageZ);
			if (page && page->IsPrepared())
			{
				page->SetInnerColorAt(localInnerX, localInnerZ, color);
			}
		}

		void Terrain::UpdateInnerVertices(const int fromX, const int fromZ, const int toX, const int toZ)
		{
			// Inner vertices are cell-centered between outer vertices
			// Interpolate inner vertex heights from 4 surrounding outer vertices
			const int minInnerX = std::max(0, fromX);
			const int minInnerZ = std::max(0, fromZ);
			const int maxInnerX = std::min(toX, static_cast<int>(m_width * (constants::OuterVerticesPerPageSide - 1) - 1));
			const int maxInnerZ = std::min(toZ, static_cast<int>(m_height * (constants::OuterVerticesPerPageSide - 1) - 1));

			for (int ix = minInnerX; ix <= maxInnerX; ++ix)
			{
				for (int iz = minInnerZ; iz <= maxInnerZ; ++iz)
				{
					// Get the 4 surrounding outer vertices
					const float h00 = GetHeightAt(ix, iz);
					const float h10 = GetHeightAt(ix + 1, iz);
					const float h01 = GetHeightAt(ix, iz + 1);
					const float h11 = GetHeightAt(ix + 1, iz + 1);

					// Interpolate to get inner vertex height (center of quad)
					const float innerHeight = (h00 + h10 + h01 + h11) * 0.25f;

					// Determine which page this inner vertex belongs to
					const uint32 pageX = ix / (constants::OuterVerticesPerPageSide - 1);
					const uint32 pageZ = iz / (constants::OuterVerticesPerPageSide - 1);
					const uint32 localInnerX = ix % (constants::OuterVerticesPerPageSide - 1);
					const uint32 localInnerZ = iz % (constants::OuterVerticesPerPageSide - 1);

					Page *page = GetPage(pageX, pageZ);
					if (page && page->IsPrepared())
					{
						page->SetInnerHeightAt(localInnerX, localInnerZ, innerHeight);
					}
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
			for (int32 x = static_cast<int32>(fromPageX); x <= static_cast<int32>(toPageX); x++)
			{
				// Invalid page
				if (static_cast<uint32>(x) >= m_width)
				{
					continue;
				}

				// Get page start vertex (X)
				const int32 pageStartX = std::max<int32>(fromX - x * static_cast<int32>(constants::OuterVerticesPerPageSide - 1), 0);
				const int32 pageEndX = toX - x * static_cast<int32>(constants::OuterVerticesPerPageSide - 1);
				for (int32 z = static_cast<int32>(fromPageZ); z <= static_cast<int32>(toPageZ); z++)
				{
					// Invalid page
					if (static_cast<uint32>(z) >= m_height)
					{
						continue;
					}

					// Get page start vertex (Z)
					const int32 pageStartZ = std::max<int32>(fromZ - z * static_cast<int32>(constants::OuterVerticesPerPageSide - 1), 0);
					const int32 pageEndZ = toZ - z * static_cast<int32>(constants::OuterVerticesPerPageSide - 1);
					// Update the tiles if necessary
					if (Page *page = GetPage(x, z); page != nullptr)
					{
						if (page->IsLoaded())
						{
							page->UpdateTiles(pageStartX, pageStartZ, pageEndX, pageEndZ, false);
						}
					}
				}
			}
		}

		void Terrain::UpdateTileCoverage(const int fromX, const int fromZ, const int toX, const int toZ) const
		{
			uint32 fromPageX, fromPageZ, localVertexX, localVertexY;
			GetPageAndLocalPixel(fromX, fromPageX, localVertexX);
			GetPageAndLocalPixel(fromZ, fromPageZ, localVertexY);

			uint32 toPageX, toPageZ, localVertexToX, localVertexToY;
			GetPageAndLocalPixel(toX, toPageX, localVertexToX);
			GetPageAndLocalPixel(toZ, toPageZ, localVertexToY);

			// Iterate through all pages in the area
			for (int32 x = static_cast<int32>(fromPageX); x <= static_cast<int32>(toPageX); x++)
			{
				// Invalid page
				if (static_cast<uint32>(x) >= m_width)
				{
					continue;
				}

				// Get page start vertex (X)
				const int32 pageStartX = std::max<int32>(fromX - x * static_cast<int32>(constants::PixelsPerPage - 1), 0);
				const int32 pageEndX = toX - x * static_cast<int32>(constants::PixelsPerPage - 1);

				for (int32 z = static_cast<int32>(fromPageZ); z <= static_cast<int32>(toPageZ); z++)
				{
					// Invalid page
					if (static_cast<uint32>(z) >= m_height)
					{
						continue;
					}

					// Get page start vertex (Z)
					const int32 pageStartZ = std::max<int32>(fromZ - z * static_cast<int32>(constants::PixelsPerPage - 1), 0);
					const int32 pageEndZ = toZ - z * static_cast<int32>(constants::PixelsPerPage - 1);

					// Update the tiles if necessary
					if (Page *pPage = GetPage(x, z); pPage != nullptr)
					{
						if (pPage->IsLoaded())
						{
							pPage->UpdateTileCoverage(pageStartX, pageStartZ, pageEndX, pageEndZ);
						}
					}
				}
			}
		}

		void Terrain::SetArea(const Vector3 &position, const uint32 area) const
		{
			// Calculate tile position from world position
			int32 tileX, tileY;
			if (!GetTileIndexByWorldPosition(position, tileX, tileY))
			{
				return;
			}

			SetAreaForTile(tileX, tileY, area);
		}

		void Terrain::SetAreaForTile(const uint32 globalTileX, const uint32 globalTileY, const uint32 area) const
		{
			ASSERT(globalTileX < m_width * constants::TilesPerPage && globalTileY < m_height * constants::TilesPerPage);

			// Determine page from tile
			const uint32 pageX = globalTileX / constants::TilesPerPage;
			const uint32 pageY = globalTileY / constants::TilesPerPage;

			Page *page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				return;
			}

			// Now lets set the actual tile area
			page->SetArea(globalTileX % constants::TilesPerPage, globalTileY % constants::TilesPerPage, area);
		}

		uint32 Terrain::GetArea(const Vector3 &position) const
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

			const Page *page = GetPage(pageX, pageY);
			if (!page || !page->IsPrepared())
			{
				return 0;
			}

			// Now lets get the actual tile area
			return page->GetArea(globalTileX % constants::TilesPerPage, globalTileY % constants::TilesPerPage);
		}

		void Terrain::SetWireframeMaterial(const MaterialPtr &wireframeMaterial)
		{
			ASSERT(wireframeMaterial);

			// Ensure material instance is created and used
			m_wireframeMaterial = std::make_shared<MaterialInstance>("TerrainGridWireframe", wireframeMaterial);
			m_wireframeMaterial->SetWireframe(true);
		}

		void Terrain::GetGlobalPixelWorldPosition(const int x, const int y, float *out_x, float *out_z) const
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

		void Terrain::GetGlobalVertexWorldPosition(const int x, const int y, float *out_x, float *out_z) const
		{
			// Only outer vertices form an addressable grid
			constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::OuterVerticesPerPageSide - 1));
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

		bool Terrain::RayTriangleIntersection(const Ray &ray, const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, float &t, Vector3 &intersectionPoint)
		{
			constexpr float epsilon = 1e-6f;

			Vector3 edge1 = v1 - v0;
			Vector3 edge2 = v2 - v0;
			Vector3 h = ray.GetDirection().Cross(edge2);
			float a = edge1.Dot(h);

			if (std::abs(a) < epsilon)
			{
				return false; // Ray is parallel to triangle
			}

			const float f = 1.0f / a;
			const Vector3 s = ray.origin - v0;
			const float u = f * s.Dot(h);

			if (u < 0.0f || u > 1.0f)
			{
				return false;
			}

			const Vector3 q = s.Cross(edge1);

			if (const float v = f * ray.GetDirection().Dot(q); v < 0.0f || u + v > 1.0f)
				return false;

			// At this stage, we can compute t to find out where the intersection point is on the line
			t = f * edge2.Dot(q);

			if (t > epsilon) // Ray intersection
			{
				intersectionPoint = ray.origin + ray.GetDirection() * t;
				return true;
			}

			// Line intersection but not a ray intersection
			return false;
		}

		bool Terrain::IsHoleAt(const float x, const float z) const
		{
			const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
			const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

			// Calculate page indices
			const int32 pageX = static_cast<int32>(std::floor((x + halfTerrainWidth) / constants::PageSize));
			const int32 pageZ = static_cast<int32>(std::floor((z + halfTerrainHeight) / constants::PageSize));

			// Bounds check
			if (pageX < 0 || pageX >= static_cast<int32>(m_width) || pageZ < 0 || pageZ >= static_cast<int32>(m_height))
			{
				return true; // Outside terrain bounds - treat as hole
			}

			Page* page = GetPage(pageX, pageZ);
			if (!page || !page->IsPrepared())
			{
				return true; // Page not loaded - treat as hole
			}

			// Calculate local position within page
			const float pageOriginX = pageX * constants::PageSize - halfTerrainWidth;
			const float pageOriginZ = pageZ * constants::PageSize - halfTerrainHeight;
			const float localX = x - pageOriginX;
			const float localZ = z - pageOriginZ;

			// Calculate tile indices within the page
			const uint32 tileX = std::min(static_cast<uint32>(localX / constants::TileSize), static_cast<uint32>(constants::TilesPerPage - 1));
			const uint32 tileZ = std::min(static_cast<uint32>(localZ / constants::TileSize), static_cast<uint32>(constants::TilesPerPage - 1));

			// Calculate position within tile
			const float tileLocalX = localX - tileX * constants::TileSize;
			const float tileLocalZ = localZ - tileZ * constants::TileSize;

			// Calculate inner vertex indices (8 inner vertices per tile side)
			const float innerCellSize = static_cast<float>(constants::TileSize / constants::InnerVerticesPerTileSide);
			const uint32 innerX = std::min(static_cast<uint32>(tileLocalX / innerCellSize), constants::InnerVerticesPerTileSide - 1);
			const uint32 innerZ = std::min(static_cast<uint32>(tileLocalZ / innerCellSize), constants::InnerVerticesPerTileSide - 1);

			return page->IsHole(tileX, tileZ, innerX, innerZ);
		}

		void Terrain::PaintHoles(float brushCenterX, float brushCenterZ, float radius, bool addHole)
		{
			// Convert brush center from world space to page coordinates
			const float halfTerrainWidth = (m_width * constants::PageSize) * 0.5f;
			const float halfTerrainHeight = (m_height * constants::PageSize) * 0.5f;

			// Calculate affected pages
			const float minX = brushCenterX - radius;
			const float maxX = brushCenterX + radius;
			const float minZ = brushCenterZ - radius;
			const float maxZ = brushCenterZ + radius;

			const int32 minPageX = std::max(0, static_cast<int32>(std::floor((minX + halfTerrainWidth) / constants::PageSize)));
			const int32 maxPageX = std::min(static_cast<int32>(m_width) - 1, static_cast<int32>(std::floor((maxX + halfTerrainWidth) / constants::PageSize)));
			const int32 minPageZ = std::max(0, static_cast<int32>(std::floor((minZ + halfTerrainHeight) / constants::PageSize)));
			const int32 maxPageZ = std::min(static_cast<int32>(m_height) - 1, static_cast<int32>(std::floor((maxZ + halfTerrainHeight) / constants::PageSize)));

			// Process each affected page
			for (int32 pageZ = minPageZ; pageZ <= maxPageZ; ++pageZ)
			{
				for (int32 pageX = minPageX; pageX <= maxPageX; ++pageX)
				{
					Page *page = GetPage(pageX, pageZ);
					if (!page)
					{
						continue;
					}

					// Iterate through all tiles in this page
					for (uint32 tileY = 0; tileY < constants::TilesPerPage; ++tileY)
					{
						for (uint32 tileX = 0; tileX < constants::TilesPerPage; ++tileX)
						{
							// Check each inner vertex in the tile
							for (uint32 innerY = 0; innerY < constants::InnerVerticesPerTileSide; ++innerY)
							{
								for (uint32 innerX = 0; innerX < constants::InnerVerticesPerTileSide; ++innerX)
								{
									// Calculate global inner vertex position
									const uint32 globalInnerX = pageX * constants::InnerVerticesPerPageSide + tileX * constants::InnerVerticesPerTileSide + innerX;
									const uint32 globalInnerZ = pageZ * constants::InnerVerticesPerPageSide + tileY * constants::InnerVerticesPerTileSide + innerY;

									// Convert to world position (inner vertices are offset by 0.5 from outer vertices)
									constexpr float scale = static_cast<float>(constants::PageSize / static_cast<double>(constants::OuterVerticesPerPageSide - 1));
									const float worldX = (globalInnerX + 0.5f) * scale - halfTerrainWidth;
									const float worldZ = (globalInnerZ + 0.5f) * scale - halfTerrainHeight;

									// Check if this inner vertex is within the brush radius
									const float dx = worldX - brushCenterX;
									const float dz = worldZ - brushCenterZ;
									const float distSq = dx * dx + dz * dz;

									if (distSq <= radius * radius)
									{
										// Mark or unmark this vertex as a hole
										page->SetHole(tileX, tileY, innerX, innerY, addHole);
									}
								}
							}
						}
					}
				}
			}
		}
	}
}