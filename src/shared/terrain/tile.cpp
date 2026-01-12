#include "tile.h"

#include "page.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/scene_node.h"
#include "terrain.h"
#include "graphics/material_instance.h"
#include "graphics/texture_mgr.h"
#include "math/capsule.h"
#include "math/collision.h"
#include "math/matrix3.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene.h"

namespace mmo
{
	namespace terrain
	{
		constexpr uint32 WireframeRenderGroupId = RenderQueueGroupId::Main + 1;

		Tile::Tile(const String &name, Page &page, size_t startX, size_t startZ)
			: MovableObject(name), Renderable(), m_page(page), m_startX(startX), m_startZ(startZ)
		{
			SetRenderQueueGroup(WorldGeometry1);

			SetCastShadows(false);

			// Calculate tile coordinates based on outer vertex spacing
			m_tileX = m_startX / (constants::OuterVerticesPerTileSide - 1);
			m_tileY = m_startZ / (constants::OuterVerticesPerTileSide - 1);

			CreateVertexData(m_startX, m_startZ);
			CreateIndexData(0, 0, 0, 0, 0);

			m_coverageTexture = TextureManager::Get().CreateManual(m_name, constants::PixelsPerTile, constants::PixelsPerTile, R8G8B8A8, BufferUsage::StaticWriteOnly);
			ASSERT(m_coverageTexture);

			std::vector<uint32> buffer(constants::PixelsPerTile * constants::PixelsPerTile);

			const size_t pixelStartX = m_tileX * (constants::PixelsPerTile - 1);
			const size_t pixelEndX = pixelStartX + constants::PixelsPerTile;
			const size_t pixelStartY = m_tileY * (constants::PixelsPerTile - 1);
			const size_t pixelEndY = pixelStartY + constants::PixelsPerTile;

			auto &scene = m_page.GetTerrain().GetScene();
			for (size_t x = pixelStartX; x < pixelEndX; ++x)
			{
				for (size_t y = pixelStartY; y < pixelEndY; ++y)
				{
					const uint32 layers = m_page.GetLayersAt(x, y);
					const size_t localX = x - pixelStartX;
					const size_t localY = y - pixelStartY;
					buffer[localY + localX * constants::PixelsPerTile] = layers;
				}
			}

			m_coverageTexture->LoadRaw(buffer.data(), CoverageMapSize * CoverageMapSize * 4);
			m_coverageTexture->SetTextureAddressMode(TextureAddressMode::Clamp);
			m_coverageTexture->SetFilter(TextureFilter::Anisotropic);

			m_materialInstance = std::make_shared<MaterialInstance>(m_name, page.GetTerrain().GetDefaultMaterial());
			m_materialInstance->SetTextureParameter("Splatting", m_coverageTexture);
		}

		Tile::~Tile() = default;

		void Tile::PrepareRenderOperation(RenderOperation &operation)
		{
			operation.vertexData = m_vertexData.get();
			
			auto it = m_lodIndexCache.find(m_currentStitchKey);
			if (it != m_lodIndexCache.end() && it->second)
			{
				operation.indexData = it->second.get();
			}
			else
			{
				// Fallback to LOD 0 with no stitching
				it = m_lodIndexCache.find(0);
				if (it != m_lodIndexCache.end() && it->second)
				{
					operation.indexData = it->second.get();
				}
				else
				{
					operation.indexData = nullptr;
				}
			}

			operation.topology = TopologyType::TriangleList;

			// A little hack to use the wireframe material for rendering instead
			if (operation.GetRenderGroupId() == WireframeRenderGroupId)
			{
				operation.material = m_page.GetTerrain().GetWireframeMaterial();
			}
			else
			{
				operation.material = GetMaterial();
			}
		}

		const Matrix4 &Tile::GetWorldTransform() const
		{
			return GetParentNodeFullTransform();
		}

		float Tile::GetSquaredViewDepth(const Camera &camera) const
		{
			// Determine the tile's center position from bounds
			const Vector3 center = (GetParentNodeFullTransform() * GetBoundingBox()).GetCenter();

			// Calculate squared distance from camera to tile center
			return (camera.GetDerivedPosition() - center).GetSquaredLength();
		}

		MaterialPtr Tile::GetMaterial() const
		{
			if (!m_materialInstance)
			{
				return GetTerrain().GetDefaultMaterial();
			}

			return m_materialInstance;
		}

		MaterialPtr Tile::GetBaseMaterial() const
		{
			if (!m_materialInstance)
			{
				return GetTerrain().GetDefaultMaterial();
			}

			return m_materialInstance->GetParent();
		}

		void Tile::SetMaterial(MaterialPtr material)
		{
			if (!material)
			{
				return;
			}

			m_materialInstance = std::make_shared<MaterialInstance>(GetName(), material);
			m_materialInstance->SetTextureParameter("Splatting", m_name);

			m_page.NotifyTileMaterialChanged(m_tileX, m_tileY);
		}

		const String &Tile::GetMovableType() const
		{
			static const String type = "Tile";
			return type;
		}

		const AABB &Tile::GetBoundingBox() const
		{
			return m_bounds;
		}

		float Tile::GetBoundingRadius() const
		{
			return m_boundingRadius;
		}

		void Tile::VisitRenderables(Visitor &visitor, bool debugRenderables)
		{
			visitor.Visit(*this, 0, false);
		}

		void Tile::PopulateRenderQueue(RenderQueue &queue)
		{
			if (HasRenderableGeometry())
			{
				queue.AddRenderable(*this, m_renderQueueId);

				if (m_page.GetTerrain().IsWireframeVisible())
				{
					queue.AddRenderable(*this, WireframeRenderGroupId);
				}
			}
		}

		bool Tile::PreRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera)
		{
			if (m_page.GetTerrain().IsLodEnabled())
			{
				UpdateLOD(camera);
			}
			else
			{
				m_currentLod = 0;
			}
			
			return Renderable::PreRender(scene, graphicsDevice, camera);
		}

		Terrain &Tile::GetTerrain() const
		{
			return m_page.GetTerrain();
		}

		void Tile::UpdateTerrain(size_t startx, size_t startz, size_t endx, size_t endz)
		{
			constexpr float outerScale = static_cast<float>(constants::TileSize / static_cast<double>(constants::OuterVerticesPerTileSide - 1));

			struct VertexStruct
			{
				Vector3 position;
				uint32 color;
				Vector3 normal;
				Vector3 binormal;
				Vector3 tangent;
				float u, v;
			};

			float minHeight = std::numeric_limits<float>::max();
			float maxHeight = std::numeric_limits<float>::lowest();

			VertexStruct *vert = (VertexStruct *)m_mainBuffer->Map(LockOptions::Normal);

			// First, create all outer vertices (9x9 = 81 vertices)
			for (size_t j = 0; j < constants::OuterVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::OuterVerticesPerTileSide; ++i)
				{
					const size_t globalX = m_startX + i;
					const size_t globalZ = m_startZ + j;

					const float height = m_page.GetHeightAt(globalX, globalZ);
					vert->position = Vector3(outerScale * globalX, height, outerScale * globalZ);
					vert->normal = m_page.GetNormalAt(globalX, globalZ);

					// Calculate tangent and binormal
					const Vector3 arbitrary = std::abs(vert->normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					vert->tangent = (arbitrary - vert->normal * vert->normal.Dot(arbitrary)).NormalizedCopy();
					vert->binormal = vert->normal.Cross(vert->tangent).NormalizedCopy();

					vert->color = Color(m_page.GetColorAt(globalX, globalZ)).GetABGR();
					vert->u = static_cast<float>(j) / static_cast<float>(constants::OuterVerticesPerTileSide - 1);
					vert->v = static_cast<float>(i) / static_cast<float>(constants::OuterVerticesPerTileSide - 1);

					minHeight = std::min(height, minHeight);
					maxHeight = std::max(height, maxHeight);

					vert++;
				}
			}

			// Second, create all inner vertices (8x8 = 64 vertices)
			// Inner vertices are positioned at the center of each quad formed by outer vertices
			for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
				{
					// Inner vertex is at the center of the quad formed by outer vertices
					const float centerOffsetX = 0.5f;
					const float centerOffsetZ = 0.5f;
					const size_t globalX = m_startX + i;
					const size_t globalZ = m_startZ + j;

					// Sample height at the center position
					// For now, we'll interpolate from the four surrounding outer vertices
					const float h00 = m_page.GetHeightAt(globalX, globalZ);
					const float h10 = m_page.GetHeightAt(globalX + 1, globalZ);
					const float h01 = m_page.GetHeightAt(globalX, globalZ + 1);
					const float h11 = m_page.GetHeightAt(globalX + 1, globalZ + 1);
					const float height = (h00 + h10 + h01 + h11) * 0.25f;

					const float worldX = outerScale * (globalX + centerOffsetX);
					const float worldZ = outerScale * (globalZ + centerOffsetZ);

					vert->position = Vector3(worldX, height, worldZ);

					// Calculate normal by interpolating surrounding outer vertex normals
					const Vector3 n00 = m_page.GetNormalAt(globalX, globalZ);
					const Vector3 n10 = m_page.GetNormalAt(globalX + 1, globalZ);
					const Vector3 n01 = m_page.GetNormalAt(globalX, globalZ + 1);
					const Vector3 n11 = m_page.GetNormalAt(globalX + 1, globalZ + 1);
					vert->normal = ((n00 + n10 + n01 + n11) * 0.25f).NormalizedCopy();

					// Calculate tangent and binormal
					const Vector3 arbitrary = std::abs(vert->normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					vert->tangent = (arbitrary - vert->normal * vert->normal.Dot(arbitrary)).NormalizedCopy();
					vert->binormal = vert->normal.Cross(vert->tangent).NormalizedCopy();

					// Interpolate color from surrounding vertices
					const uint32 c00 = m_page.GetColorAt(globalX, globalZ);
					const uint32 c10 = m_page.GetColorAt(globalX + 1, globalZ);
					const uint32 c01 = m_page.GetColorAt(globalX, globalZ + 1);
					const uint32 c11 = m_page.GetColorAt(globalX + 1, globalZ + 1);
					const Color col00(c00), col10(c10), col01(c01), col11(c11);
					const Color avgColor = (col00 + col10 + col01 + col11) * 0.25f;
					vert->color = avgColor.GetABGR();

					vert->u = (static_cast<float>(j) + 0.5f) / static_cast<float>(constants::InnerVerticesPerTileSide);
					vert->v = (static_cast<float>(i) + 0.5f) / static_cast<float>(constants::InnerVerticesPerTileSide);

					minHeight = std::min(height, minHeight);
					maxHeight = std::max(height, maxHeight);

					vert++;
				}
			}

			m_mainBuffer->Unmap();

			m_bounds.min.y = minHeight;
			m_bounds.max.y = maxHeight;
			m_center = m_bounds.GetCenter();
			m_boundingRadius = (m_bounds.max - m_center).GetLength();
			m_worldAABBDirty = true;

		// Regenerate index data in case holes have changed
		m_lodIndexCache.clear();
		CreateIndexData(0, 0, 0, 0, 0);
	}

	void Tile::UpdateCoverageMap()
	{
		ASSERT(m_coverageTexture);

		std::vector<uint32> buffer(constants::PixelsPerTile * constants::PixelsPerTile);

		const size_t pixelStartX = m_tileX * (constants::PixelsPerTile - 1);
		const size_t pixelEndX = pixelStartX + constants::PixelsPerTile;
			const size_t pixelStartY = m_tileY * (constants::PixelsPerTile - 1);
			const size_t pixelEndY = pixelStartY + constants::PixelsPerTile;

			for (size_t x = pixelStartX; x < pixelEndX; ++x)
			{
				for (size_t y = pixelStartY; y < pixelEndY; ++y)
				{
					const uint32 layers = m_page.GetLayersAt(x, y);
					const size_t localX = x - pixelStartX;
					const size_t localY = y - pixelStartY;
					buffer[localY + localX * constants::PixelsPerTile] = layers;
				}
			}

			// Update texture
			m_coverageTexture->UpdateFromMemory(buffer.data(), buffer.size() * 4);
		}

		void Tile::CreateVertexData(size_t startX, size_t startZ)
		{
			m_vertexData = std::make_unique<VertexData>();
			m_vertexData->vertexStart = 0;
			m_vertexData->vertexCount = constants::VerticesPerTile; // 145 vertices (81 outer + 64 inner)

			VertexDeclaration *decl = m_vertexData->vertexDeclaration;
			VertexBufferBinding *bind = m_vertexData->vertexBufferBinding;

			uint32 offset = 0;
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

			float minHeight = m_page.GetHeightAt(0, 0);
			float maxHeight = minHeight;

			// Scale for outer vertices (9x9 grid)
			constexpr float outerScale = static_cast<float>(constants::TileSize / static_cast<double>(constants::OuterVerticesPerTileSide - 1));
			// Scale for inner vertices (positioned between outer vertices)
			constexpr float innerScale = outerScale;

			struct VertexStruct
			{
				Vector3 position;
				uint32 color;
				Vector3 normal;
				Vector3 binormal;
				Vector3 tangent;
				float u, v;
			};

			std::vector<VertexStruct> vertices(m_vertexData->vertexCount);
			VertexStruct *vert = vertices.data();

			const float minX = outerScale * startX;
			const float minZ = outerScale * startZ;
			const float maxX = outerScale * (startX + constants::OuterVerticesPerTileSide - 1);
			const float maxZ = outerScale * (startZ + constants::OuterVerticesPerTileSide - 1);

			// First, create all outer vertices (9x9 = 81 vertices)
			for (size_t j = 0; j < constants::OuterVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::OuterVerticesPerTileSide; ++i)
				{
					const size_t globalX = startX + i;
					const size_t globalZ = startZ + j;

					const float height = m_page.GetHeightAt(globalX, globalZ);
					vert->position = Vector3(outerScale * globalX, height, outerScale * globalZ);
					vert->normal = m_page.GetNormalAt(globalX, globalZ);

					// Calculate tangent and binormal
					const Vector3 arbitrary = std::abs(vert->normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					vert->tangent = (arbitrary - vert->normal * vert->normal.Dot(arbitrary)).NormalizedCopy();
					vert->binormal = vert->normal.Cross(vert->tangent).NormalizedCopy();

					vert->color = Color(m_page.GetColorAt(globalX, globalZ)).GetABGR();
					vert->u = static_cast<float>(j) / static_cast<float>(constants::OuterVerticesPerTileSide - 1);
					vert->v = static_cast<float>(i) / static_cast<float>(constants::OuterVerticesPerTileSide - 1);

					minHeight = std::min(height, minHeight);
					maxHeight = std::max(height, maxHeight);

					vert++;
				}
			}

			// Second, create all inner vertices (8x8 = 64 vertices)
			// Inner vertices are positioned at the center of each quad formed by outer vertices
			for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
				{
					// Inner vertex is at the center of the quad formed by outer vertices
					const float centerOffsetX = 0.5f;
					const float centerOffsetZ = 0.5f;
					const size_t globalX = startX + i;
					const size_t globalZ = startZ + j;

					// Sample height at the center position
					// For now, we'll interpolate from the four surrounding outer vertices
					const float h00 = m_page.GetHeightAt(globalX, globalZ);
					const float h10 = m_page.GetHeightAt(globalX + 1, globalZ);
					const float h01 = m_page.GetHeightAt(globalX, globalZ + 1);
					const float h11 = m_page.GetHeightAt(globalX + 1, globalZ + 1);
					const float height = (h00 + h10 + h01 + h11) * 0.25f;

					const float worldX = outerScale * (globalX + centerOffsetX);
					const float worldZ = outerScale * (globalZ + centerOffsetZ);

					vert->position = Vector3(worldX, height, worldZ);

					// Calculate normal by interpolating surrounding outer vertex normals
					const Vector3 n00 = m_page.GetNormalAt(globalX, globalZ);
					const Vector3 n10 = m_page.GetNormalAt(globalX + 1, globalZ);
					const Vector3 n01 = m_page.GetNormalAt(globalX, globalZ + 1);
					const Vector3 n11 = m_page.GetNormalAt(globalX + 1, globalZ + 1);
					vert->normal = ((n00 + n10 + n01 + n11) * 0.25f).NormalizedCopy();

					// Calculate tangent and binormal
					const Vector3 arbitrary = std::abs(vert->normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					vert->tangent = (arbitrary - vert->normal * vert->normal.Dot(arbitrary)).NormalizedCopy();
					vert->binormal = vert->normal.Cross(vert->tangent).NormalizedCopy();

					// Interpolate color from surrounding vertices
					const uint32 c00 = m_page.GetColorAt(globalX, globalZ);
					const uint32 c10 = m_page.GetColorAt(globalX + 1, globalZ);
					const uint32 c01 = m_page.GetColorAt(globalX, globalZ + 1);
					const uint32 c11 = m_page.GetColorAt(globalX + 1, globalZ + 1);
					const Color col00(c00), col10(c10), col01(c01), col11(c11);
					const Color avgColor = (col00 + col10 + col01 + col11) * 0.25f;
					vert->color = avgColor.GetABGR();

					vert->u = (static_cast<float>(j) + 0.5f) / static_cast<float>(constants::InnerVerticesPerTileSide);
					vert->v = (static_cast<float>(i) + 0.5f) / static_cast<float>(constants::InnerVerticesPerTileSide);

					minHeight = std::min(height, minHeight);
					maxHeight = std::max(height, maxHeight);

					vert++;
				}
			}

			m_mainBuffer = GraphicsDevice::Get().CreateVertexBuffer(m_vertexData->vertexCount, decl->GetVertexSize(0), BufferUsage::DynamicWriteOnly, vertices.data());
			bind->SetBinding(0, m_mainBuffer);

			if (minHeight == maxHeight)
			{
				maxHeight = minHeight + 0.1f;
			}

			m_bounds = AABB(
				Vector3(minX, minHeight, minZ),
				Vector3(maxX, maxHeight, maxZ));

			m_center = m_bounds.GetCenter();

			m_boundingRadius = (m_bounds.max - m_center).GetLength();
		}

		uint16 Tile::GetOuterVertexIndex(size_t x, size_t y) const
		{
			ASSERT(x < constants::OuterVerticesPerTileSide && y < constants::OuterVerticesPerTileSide);
			return static_cast<uint16>(x + y * constants::OuterVerticesPerTileSide);
		}

		uint16 Tile::GetInnerVertexIndex(size_t x, size_t y) const
		{
			ASSERT(x < constants::InnerVerticesPerTileSide && y < constants::InnerVerticesPerTileSide);
			// Inner vertices start after all outer vertices
			return static_cast<uint16>(constants::OuterVerticesPerTile + x + y * constants::InnerVerticesPerTileSide);
		}

		bool Tile::IsOuterEdge(size_t x, size_t y) const
		{
			return x == 0 || y == 0 || x >= constants::OuterVerticesPerTileSide - 1 || y >= constants::OuterVerticesPerTileSide - 1;
		}

		void Tile::CreateIndexData(uint32 lod, uint32 northLod, uint32 eastLod, uint32 southLod, uint32 westLod)
		{
			// Calculate a unique key for this LOD + neighbor LOD combination
			// Format: lod | (northLod << 4) | (eastLod << 8) | (southLod << 12) | (westLod << 16)
			const uint32 stitchKey = lod | (northLod << 4) | (eastLod << 8) | (southLod << 12) | (westLod << 16);

			// Check if we already have this configuration cached
			if (m_lodIndexCache.find(stitchKey) != m_lodIndexCache.end())
			{
				m_currentStitchKey = stitchKey;
				return;
			}

			// Estimate index count
			std::vector<uint16> indices;
			indices.reserve(800);

			// Get the hole map for this tile
			const uint64 holeMap = m_page.GetTileHoleMap(m_tileX, m_tileY);

			// Calculate step size for this tile's LOD
			// LOD 0 = step 1, LOD 1 = step 1, LOD 2 = step 2, LOD 3 = step 4
			const size_t ourStep = (lod <= 1) ? 1 : (static_cast<size_t>(1) << (lod - 1));

			// Calculate step sizes for neighbor tiles
			const size_t northStep = (northLod <= 1) ? 1 : (static_cast<size_t>(1) << (northLod - 1));
			const size_t eastStep = (eastLod <= 1) ? 1 : (static_cast<size_t>(1) << (eastLod - 1));
			const size_t southStep = (southLod <= 1) ? 1 : (static_cast<size_t>(1) << (southLod - 1));
			const size_t westStep = (westLod <= 1) ? 1 : (static_cast<size_t>(1) << (westLod - 1));

			const size_t maxIdx = constants::OuterVerticesPerTileSide - 1;

			if (lod == 0)
			{
				// For LOD 0, we use the inner vertex diamond pattern
				// Each inner vertex connects to 4 outer vertices forming 4 triangles
				//
				// When a neighbor has lower detail (higher LOD), we need to skip triangles
				// that use intermediate edge vertices that the neighbor doesn't have.

				for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
				{
					for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
					{
						// Check if this inner vertex is marked as a hole
						const uint32 bitIndex = static_cast<uint32>(i + j * constants::InnerVerticesPerTileSide);
						const bool isHole = (holeMap & (1ULL << bitIndex)) != 0;

						if (isHole)
						{
							continue;
						}

						const uint16 innerIdx = GetInnerVertexIndex(i, j);

						// Get the 4 surrounding outer vertices
						const uint16 outerTL = GetOuterVertexIndex(i, j);
						const uint16 outerTR = GetOuterVertexIndex(i + 1, j);
						const uint16 outerBL = GetOuterVertexIndex(i, j + 1);
						const uint16 outerBR = GetOuterVertexIndex(i + 1, j + 1);

						// Determine which edges this cell touches
						const bool touchesNorth = (j == 0);
						const bool touchesEast = (i == constants::InnerVerticesPerTileSide - 1);
						const bool touchesSouth = (j == constants::InnerVerticesPerTileSide - 1);
						const bool touchesWest = (i == 0);

						// Helper lambda to check if a vertex is an intermediate on an edge
						// (i.e., it won't be used by a coarser neighbor)
						auto isIntermediateOnEdge = [](size_t coord, size_t step) -> bool
						{
							return step > 1 && (coord % step) != 0;
						};

						// Check if outer vertices are intermediate on their respective edges
						// This determines whether we need to skip triangles for stitching
						const bool outerTL_isIntermediateNorth = touchesNorth && northLod > lod && isIntermediateOnEdge(i, northStep);
						const bool outerTR_isIntermediateNorth = touchesNorth && northLod > lod && isIntermediateOnEdge(i + 1, northStep);
						const bool outerTR_isIntermediateEast = touchesEast && eastLod > lod && isIntermediateOnEdge(j, eastStep);
						const bool outerBR_isIntermediateEast = touchesEast && eastLod > lod && isIntermediateOnEdge(j + 1, eastStep);
						const bool outerBL_isIntermediateSouth = touchesSouth && southLod > lod && isIntermediateOnEdge(i, southStep);
						const bool outerBR_isIntermediateSouth = touchesSouth && southLod > lod && isIntermediateOnEdge(i + 1, southStep);
						const bool outerTL_isIntermediateWest = touchesWest && westLod > lod && isIntermediateOnEdge(j, westStep);
						const bool outerBL_isIntermediateWest = touchesWest && westLod > lod && isIntermediateOnEdge(j + 1, westStep);

						// Skip triangles that use intermediate edge vertices
						const bool skipTopTri = outerTL_isIntermediateNorth || outerTR_isIntermediateNorth;
						const bool skipRightTri = outerTR_isIntermediateEast || outerBR_isIntermediateEast;
						const bool skipBottomTri = outerBL_isIntermediateSouth || outerBR_isIntermediateSouth;
						const bool skipLeftTri = outerTL_isIntermediateWest || outerBL_isIntermediateWest;

						// Top triangle (faces north edge)
						if (!skipTopTri)
						{
							indices.push_back(innerIdx);
							indices.push_back(outerTR);
							indices.push_back(outerTL);
						}

						// Right triangle (faces east edge)
						if (!skipRightTri)
						{
							indices.push_back(innerIdx);
							indices.push_back(outerBR);
							indices.push_back(outerTR);
						}

						// Bottom triangle (faces south edge)
						if (!skipBottomTri)
						{
							indices.push_back(innerIdx);
							indices.push_back(outerBL);
							indices.push_back(outerBR);
						}

						// Left triangle (faces west edge)
						if (!skipLeftTri)
						{
							indices.push_back(innerIdx);
							indices.push_back(outerTL);
							indices.push_back(outerBL);
						}
					}
				}
			}
			else
			{
				// For LOD > 0, use simple quad grid with the appropriate step size
				const size_t step = ourStep;

				for (size_t j = 0; j < maxIdx; j += step)
				{
					for (size_t i = 0; i < maxIdx; i += step)
					{
						// Check for holes - if any cell in the quad is a hole, skip the whole quad
						bool isHole = false;
						for (size_t hj = 0; hj < step && !isHole; ++hj)
						{
							for (size_t hi = 0; hi < step && !isHole; ++hi)
							{
								if (i + hi < constants::InnerVerticesPerTileSide && j + hj < constants::InnerVerticesPerTileSide)
								{
									const uint32 bitIndex = static_cast<uint32>((i + hi) + (j + hj) * constants::InnerVerticesPerTileSide);
									if ((holeMap & (1ULL << bitIndex)) != 0)
									{
										isHole = true;
									}
								}
							}
						}

						if (isHole)
						{
							continue;
						}

						// Check if this quad touches an edge with a coarser neighbor
						const bool touchesNorth = (j == 0);
						const bool touchesEast = (i + step >= maxIdx);
						const bool touchesSouth = (j + step >= maxIdx);
						const bool touchesWest = (i == 0);

						// Skip edge quads that will be handled by stitching
						const bool needsNorthStitch = touchesNorth && (northLod > lod);
						const bool needsEastStitch = touchesEast && (eastLod > lod);
						const bool needsSouthStitch = touchesSouth && (southLod > lod);
						const bool needsWestStitch = touchesWest && (westLod > lod);

						// If any stitching is needed for this quad, skip it - stitching will handle it
						if (needsNorthStitch || needsEastStitch || needsSouthStitch || needsWestStitch)
						{
							continue;
						}

						const size_t nextI = std::min(i + step, maxIdx);
						const size_t nextJ = std::min(j + step, maxIdx);

						const uint16 outerTL = GetOuterVertexIndex(i, j);
						const uint16 outerTR = GetOuterVertexIndex(nextI, j);
						const uint16 outerBL = GetOuterVertexIndex(i, nextJ);
						const uint16 outerBR = GetOuterVertexIndex(nextI, nextJ);

						// Triangle 1: TL, BL, BR
						indices.push_back(outerTL);
						indices.push_back(outerBL);
						indices.push_back(outerBR);

						// Triangle 2: TL, BR, TR
						indices.push_back(outerTL);
						indices.push_back(outerBR);
						indices.push_back(outerTR);
					}
				}
			}

			// Generate edge stitching triangles for edges with coarser neighbors
			GenerateEdgeStitching(indices, lod, northLod, eastLod, southLod, westLod);

			// If no indices were generated, mark tile as non-renderable and skip buffer creation
			if (indices.empty())
			{
				m_lodIndexCache[stitchKey].reset();
				if (lod == 0)
				{
					m_hasRenderableGeometry = false;
				}
			}
			else
			{
				m_lodIndexCache[stitchKey] = std::make_unique<IndexData>();
				m_lodIndexCache[stitchKey]->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(
					static_cast<uint32>(indices.size()),
					IndexBufferSize::Index_16,
					BufferUsage::StaticWriteOnly,
					indices.data());
				m_lodIndexCache[stitchKey]->indexCount = static_cast<uint32>(indices.size());
				m_lodIndexCache[stitchKey]->indexStart = 0;

				if (lod == 0)
				{
					m_hasRenderableGeometry = true;
				}
			}

			m_currentStitchKey = stitchKey;
		}

		void Tile::GenerateEdgeStitching(std::vector<uint16>& indices, uint32 lod, uint32 northLod, uint32 eastLod, uint32 southLod, uint32 westLod)
		{
			// Edge stitching creates triangles that connect this tile's edge to a coarser
			// neighbor without creating T-junctions.
			//
			// The key insight is that the coarser neighbor only has vertices at certain
			// positions (every neighborStep vertices), so we must fan from those coarse
			// vertices to our finer edge vertices.

			const size_t maxIdx = constants::OuterVerticesPerTileSide - 1;
			const size_t innerMax = constants::InnerVerticesPerTileSide - 1;

			// Calculate step sizes
			const size_t ourStep = (lod <= 1) ? 1 : (static_cast<size_t>(1) << (lod - 1));
			const size_t northStep = (northLod <= 1) ? 1 : (static_cast<size_t>(1) << (northLod - 1));
			const size_t eastStep = (eastLod <= 1) ? 1 : (static_cast<size_t>(1) << (eastLod - 1));
			const size_t southStep = (southLod <= 1) ? 1 : (static_cast<size_t>(1) << (southLod - 1));
			const size_t westStep = (westLod <= 1) ? 1 : (static_cast<size_t>(1) << (westLod - 1));

			// ===== NORTH EDGE STITCHING =====
			if (northLod > lod)
			{
				// For each segment between coarse neighbor vertices
				for (size_t segStart = 0; segStart < maxIdx; segStart += northStep)
				{
					const size_t segEnd = std::min(segStart + northStep, maxIdx);
					const uint16 coarseLeft = GetOuterVertexIndex(segStart, 0);
					const uint16 coarseRight = GetOuterVertexIndex(segEnd, 0);

					if (lod == 0)
					{
						// LOD 0 uses diamond pattern - need to connect through inner vertices
						// Fan from coarseLeft through all inner vertices in this segment to coarseRight
						for (size_t k = segStart; k < segEnd; ++k)
						{
							const uint16 innerVertex = GetInnerVertexIndex(k, 0);

							if (k == segStart)
							{
								// First: connect coarseLeft to first inner and the vertex below
								if (segEnd - segStart > 1)
								{
									const uint16 belowVertex = GetOuterVertexIndex(k + 1, 1);
									indices.push_back(coarseLeft);
									indices.push_back(innerVertex);
									indices.push_back(belowVertex);
								}
							}
							else
							{
								// Middle: connect through the "below" outer vertex
								const uint16 prevInner = GetInnerVertexIndex(k - 1, 0);
								const uint16 belowVertex = GetOuterVertexIndex(k, 1);

								indices.push_back(coarseLeft);
								indices.push_back(prevInner);
								indices.push_back(belowVertex);

								indices.push_back(coarseLeft);
								indices.push_back(belowVertex);
								indices.push_back(innerVertex);
							}
						}

						// Final triangle: coarseLeft -> lastInner -> coarseRight
						if (segEnd > segStart)
						{
							const uint16 lastInner = GetInnerVertexIndex(segEnd - 1, 0);
							indices.push_back(coarseLeft);
							indices.push_back(lastInner);
							indices.push_back(coarseRight);
						}
					}
					else
					{
						// LOD > 0 uses quad pattern
						// Collect all vertices along edge and interior
						std::vector<uint16> edgeVertices;
						std::vector<uint16> interiorVertices;
						for (size_t x = segStart; x <= segEnd; x += ourStep)
						{
							edgeVertices.push_back(GetOuterVertexIndex(x, 0));
							interiorVertices.push_back(GetOuterVertexIndex(x, ourStep));
						}

						// For each segment between our fine edge vertices
						for (size_t i = 0; i + 1 < edgeVertices.size(); ++i)
						{
							const uint16 edgeLeft = edgeVertices[i];
							const uint16 edgeRight = edgeVertices[i + 1];
							const uint16 intLeft = interiorVertices[i];
							const uint16 intRight = interiorVertices[i + 1];

							// Check if edge vertices are on the coarse grid
							const size_t edgeLeftX = segStart + i * ourStep;
							const size_t edgeRightX = segStart + (i + 1) * ourStep;
							const bool isCoarseLeft = (edgeLeftX % northStep) == 0;
							const bool isCoarseRight = (edgeRightX % northStep) == 0;

							if (isCoarseLeft && isCoarseRight)
							{
								// Both on coarse grid - simple quad
								indices.push_back(edgeLeft);
								indices.push_back(intLeft);
								indices.push_back(intRight);

								indices.push_back(edgeLeft);
								indices.push_back(intRight);
								indices.push_back(edgeRight);
							}
							else if (isCoarseLeft)
							{
								// Fan from left coarse vertex
								indices.push_back(edgeLeft);
								indices.push_back(intLeft);
								indices.push_back(intRight);

								indices.push_back(edgeLeft);
								indices.push_back(intRight);
								indices.push_back(edgeRight);
							}
							else if (isCoarseRight)
							{
								// Fan from right coarse vertex
								indices.push_back(edgeRight);
								indices.push_back(edgeLeft);
								indices.push_back(intLeft);

								indices.push_back(edgeRight);
								indices.push_back(intLeft);
								indices.push_back(intRight);
							}
							else
							{
								// Both intermediate - need 4 triangles to cover the quad and connect to coarse
								// First fill the interior quad
								indices.push_back(edgeLeft);
								indices.push_back(intLeft);
								indices.push_back(intRight);

								indices.push_back(edgeLeft);
								indices.push_back(intRight);
								indices.push_back(edgeRight);

								// Then connect edge vertices to the coarse vertex
								indices.push_back(coarseLeft);
								indices.push_back(edgeLeft);
								indices.push_back(intLeft);

								indices.push_back(coarseLeft);
								indices.push_back(intRight);
								indices.push_back(edgeRight);
							}
						}
					}
				}
			}

			// ===== EAST EDGE STITCHING =====
			if (eastLod > lod)
			{
				for (size_t segStart = 0; segStart < maxIdx; segStart += eastStep)
				{
					const size_t segEnd = std::min(segStart + eastStep, maxIdx);
					const uint16 coarseTop = GetOuterVertexIndex(maxIdx, segStart);
					const uint16 coarseBottom = GetOuterVertexIndex(maxIdx, segEnd);

					if (lod == 0)
					{
						for (size_t k = segStart; k < segEnd; ++k)
						{
							const uint16 innerVertex = GetInnerVertexIndex(innerMax, k);

							if (k == segStart)
							{
								if (segEnd - segStart > 1)
								{
									const uint16 leftVertex = GetOuterVertexIndex(maxIdx - 1, k + 1);
									indices.push_back(coarseTop);
									indices.push_back(innerVertex);
									indices.push_back(leftVertex);
								}
							}
							else
							{
								const uint16 prevInner = GetInnerVertexIndex(innerMax, k - 1);
								const uint16 leftVertex = GetOuterVertexIndex(maxIdx - 1, k);

								indices.push_back(coarseTop);
								indices.push_back(prevInner);
								indices.push_back(leftVertex);

								indices.push_back(coarseTop);
								indices.push_back(leftVertex);
								indices.push_back(innerVertex);
							}
						}

						if (segEnd > segStart)
						{
							const uint16 lastInner = GetInnerVertexIndex(innerMax, segEnd - 1);
							indices.push_back(coarseTop);
							indices.push_back(lastInner);
							indices.push_back(coarseBottom);
						}
					}
					else
					{
						std::vector<uint16> edgeVertices;
						std::vector<uint16> interiorVertices;
						for (size_t y = segStart; y <= segEnd; y += ourStep)
						{
							edgeVertices.push_back(GetOuterVertexIndex(maxIdx, y));
							interiorVertices.push_back(GetOuterVertexIndex(maxIdx - ourStep, y));
						}

						for (size_t i = 0; i + 1 < edgeVertices.size(); ++i)
						{
							const uint16 edgeTop = edgeVertices[i];
							const uint16 edgeBottom = edgeVertices[i + 1];
							const uint16 intTop = interiorVertices[i];
							const uint16 intBottom = interiorVertices[i + 1];

							const size_t edgeTopY = segStart + i * ourStep;
							const size_t edgeBottomY = segStart + (i + 1) * ourStep;
							const bool isCoarseTop = (edgeTopY % eastStep) == 0;
							const bool isCoarseBottom = (edgeBottomY % eastStep) == 0;

							if (isCoarseTop && isCoarseBottom)
							{
								indices.push_back(edgeTop);
								indices.push_back(intTop);
								indices.push_back(intBottom);

								indices.push_back(edgeTop);
								indices.push_back(intBottom);
								indices.push_back(edgeBottom);
							}
							else if (isCoarseTop)
							{
								indices.push_back(edgeTop);
								indices.push_back(intTop);
								indices.push_back(intBottom);

								indices.push_back(edgeTop);
								indices.push_back(intBottom);
								indices.push_back(edgeBottom);
							}
							else if (isCoarseBottom)
							{
								indices.push_back(edgeBottom);
								indices.push_back(edgeTop);
								indices.push_back(intTop);

								indices.push_back(edgeBottom);
								indices.push_back(intTop);
								indices.push_back(intBottom);
							}
							else
							{
								// Both intermediate - 4 triangles
								indices.push_back(edgeTop);
								indices.push_back(intTop);
								indices.push_back(intBottom);

								indices.push_back(edgeTop);
								indices.push_back(intBottom);
								indices.push_back(edgeBottom);

								indices.push_back(coarseTop);
								indices.push_back(edgeTop);
								indices.push_back(intTop);

								indices.push_back(coarseTop);
								indices.push_back(intBottom);
								indices.push_back(edgeBottom);
							}
						}
					}
				}
			}

			// ===== SOUTH EDGE STITCHING =====
			if (southLod > lod)
			{
				for (size_t segStart = 0; segStart < maxIdx; segStart += southStep)
				{
					const size_t segEnd = std::min(segStart + southStep, maxIdx);
					const uint16 coarseLeft = GetOuterVertexIndex(segStart, maxIdx);
					const uint16 coarseRight = GetOuterVertexIndex(segEnd, maxIdx);

					if (lod == 0)
					{
						for (size_t k = segEnd; k > segStart; --k)
						{
							const size_t idx = k - 1;
							const uint16 innerVertex = GetInnerVertexIndex(idx, innerMax);

							if (k == segEnd)
							{
								if (segEnd - segStart > 1)
								{
									const uint16 aboveVertex = GetOuterVertexIndex(k - 1, maxIdx - 1);
									indices.push_back(coarseRight);
									indices.push_back(innerVertex);
									indices.push_back(aboveVertex);
								}
							}
							else
							{
								const uint16 nextInner = GetInnerVertexIndex(k, innerMax);
								const uint16 aboveVertex = GetOuterVertexIndex(k, maxIdx - 1);

								indices.push_back(coarseRight);
								indices.push_back(nextInner);
								indices.push_back(aboveVertex);

								indices.push_back(coarseRight);
								indices.push_back(aboveVertex);
								indices.push_back(innerVertex);
							}
						}

						if (segEnd > segStart)
						{
							const uint16 firstInner = GetInnerVertexIndex(segStart, innerMax);
							indices.push_back(coarseRight);
							indices.push_back(firstInner);
							indices.push_back(coarseLeft);
						}
					}
					else
					{
						std::vector<uint16> edgeVertices;
						std::vector<uint16> interiorVertices;
						for (size_t x = segStart; x <= segEnd; x += ourStep)
						{
							edgeVertices.push_back(GetOuterVertexIndex(x, maxIdx));
							interiorVertices.push_back(GetOuterVertexIndex(x, maxIdx - ourStep));
						}

						for (size_t i = 0; i + 1 < edgeVertices.size(); ++i)
						{
							const uint16 edgeLeft = edgeVertices[i];
							const uint16 edgeRight = edgeVertices[i + 1];
							const uint16 intLeft = interiorVertices[i];
							const uint16 intRight = interiorVertices[i + 1];

							const size_t edgeLeftX = segStart + i * ourStep;
							const size_t edgeRightX = segStart + (i + 1) * ourStep;
							const bool isCoarseLeft = (edgeLeftX % southStep) == 0;
							const bool isCoarseRight = (edgeRightX % southStep) == 0;

							if (isCoarseLeft && isCoarseRight)
							{
								indices.push_back(edgeRight);
								indices.push_back(intRight);
								indices.push_back(intLeft);

								indices.push_back(edgeRight);
								indices.push_back(intLeft);
								indices.push_back(edgeLeft);
							}
							else if (isCoarseLeft)
							{
								indices.push_back(edgeLeft);
								indices.push_back(edgeRight);
								indices.push_back(intRight);

								indices.push_back(edgeLeft);
								indices.push_back(intRight);
								indices.push_back(intLeft);
							}
							else if (isCoarseRight)
							{
								indices.push_back(edgeRight);
								indices.push_back(intRight);
								indices.push_back(intLeft);

								indices.push_back(edgeRight);
								indices.push_back(intLeft);
								indices.push_back(edgeLeft);
							}
							else
							{
								// Both intermediate - 4 triangles
								indices.push_back(edgeRight);
								indices.push_back(intRight);
								indices.push_back(intLeft);

								indices.push_back(edgeRight);
								indices.push_back(intLeft);
								indices.push_back(edgeLeft);

								indices.push_back(coarseRight);
								indices.push_back(edgeRight);
								indices.push_back(intRight);

								indices.push_back(coarseRight);
								indices.push_back(intLeft);
								indices.push_back(edgeLeft);
							}
						}
					}
				}
			}

			// ===== WEST EDGE STITCHING =====
			if (westLod > lod)
			{
				for (size_t segStart = 0; segStart < maxIdx; segStart += westStep)
				{
					const size_t segEnd = std::min(segStart + westStep, maxIdx);
					const uint16 coarseTop = GetOuterVertexIndex(0, segStart);
					const uint16 coarseBottom = GetOuterVertexIndex(0, segEnd);

					if (lod == 0)
					{
						for (size_t k = segEnd; k > segStart; --k)
						{
							const size_t idx = k - 1;
							const uint16 innerVertex = GetInnerVertexIndex(0, idx);

							if (k == segEnd)
							{
								if (segEnd - segStart > 1)
								{
									const uint16 rightVertex = GetOuterVertexIndex(1, k - 1);
									indices.push_back(coarseBottom);
									indices.push_back(innerVertex);
									indices.push_back(rightVertex);
								}
							}
							else
							{
								const uint16 nextInner = GetInnerVertexIndex(0, k);
								const uint16 rightVertex = GetOuterVertexIndex(1, k);

								indices.push_back(coarseBottom);
								indices.push_back(nextInner);
								indices.push_back(rightVertex);

								indices.push_back(coarseBottom);
								indices.push_back(rightVertex);
								indices.push_back(innerVertex);
							}
						}

						if (segEnd > segStart)
						{
							const uint16 firstInner = GetInnerVertexIndex(0, segStart);
							indices.push_back(coarseBottom);
							indices.push_back(firstInner);
							indices.push_back(coarseTop);
						}
					}
					else
					{
						std::vector<uint16> edgeVertices;
						std::vector<uint16> interiorVertices;
						for (size_t y = segStart; y <= segEnd; y += ourStep)
						{
							edgeVertices.push_back(GetOuterVertexIndex(0, y));
							interiorVertices.push_back(GetOuterVertexIndex(ourStep, y));
						}

						for (size_t i = 0; i + 1 < edgeVertices.size(); ++i)
						{
							const uint16 edgeTop = edgeVertices[i];
							const uint16 edgeBottom = edgeVertices[i + 1];
							const uint16 intTop = interiorVertices[i];
							const uint16 intBottom = interiorVertices[i + 1];

							const size_t edgeTopY = segStart + i * ourStep;
							const size_t edgeBottomY = segStart + (i + 1) * ourStep;
							const bool isCoarseTop = (edgeTopY % westStep) == 0;
							const bool isCoarseBottom = (edgeBottomY % westStep) == 0;

							if (isCoarseTop && isCoarseBottom)
							{
								indices.push_back(edgeBottom);
								indices.push_back(intBottom);
								indices.push_back(intTop);

								indices.push_back(edgeBottom);
								indices.push_back(intTop);
								indices.push_back(edgeTop);
							}
							else if (isCoarseTop)
							{
								indices.push_back(edgeTop);
								indices.push_back(edgeBottom);
								indices.push_back(intBottom);

								indices.push_back(edgeTop);
								indices.push_back(intBottom);
								indices.push_back(intTop);
							}
							else if (isCoarseBottom)
							{
								indices.push_back(edgeBottom);
								indices.push_back(intBottom);
								indices.push_back(intTop);

								indices.push_back(edgeBottom);
								indices.push_back(intTop);
								indices.push_back(edgeTop);
							}
							else
							{
								// Both intermediate - 4 triangles
								indices.push_back(edgeBottom);
								indices.push_back(intBottom);
								indices.push_back(intTop);

								indices.push_back(edgeBottom);
								indices.push_back(intTop);
								indices.push_back(edgeTop);

								indices.push_back(coarseBottom);
								indices.push_back(edgeBottom);
								indices.push_back(intBottom);

								indices.push_back(coarseBottom);
								indices.push_back(intTop);
								indices.push_back(edgeTop);
							}
						}
					}
				}
			}
		}

		void Tile::UpdateLOD(const Camera& camera)
		{
			if (!m_hasRenderableGeometry)
			{
				return;
			}

			float distSq = GetSquaredViewDepth(camera);

			uint32 newLod = 0;
			if (distSq > constants::TileSize * constants::TileSize * 16.0f)
			{
				newLod = 3;
			}
			else if (distSq > constants::TileSize * constants::TileSize * 4.0f)
			{
				newLod = 2;
			}
			else if (distSq > constants::TileSize * constants::TileSize * 1.0f)
			{
				newLod = 1;
			}

			// Get neighbor tile LODs for edge stitching
			uint32 northLod = newLod;
			uint32 eastLod = newLod;
			uint32 southLod = newLod;
			uint32 westLod = newLod;

			// Query neighbor tiles from the page
			if (m_tileY > 0)
			{
				if (Tile* northTile = m_page.GetTile(static_cast<uint32>(m_tileX), static_cast<uint32>(m_tileY - 1)))
				{
					northLod = northTile->GetCurrentLOD();
				}
			}

			if (m_tileX < constants::TilesPerPage - 1)
			{
				if (Tile* eastTile = m_page.GetTile(static_cast<uint32>(m_tileX + 1), static_cast<uint32>(m_tileY)))
				{
					eastLod = eastTile->GetCurrentLOD();
				}
			}

			if (m_tileY < constants::TilesPerPage - 1)
			{
				if (Tile* southTile = m_page.GetTile(static_cast<uint32>(m_tileX), static_cast<uint32>(m_tileY + 1)))
				{
					southLod = southTile->GetCurrentLOD();
				}
			}

			if (m_tileX > 0)
			{
				if (Tile* westTile = m_page.GetTile(static_cast<uint32>(m_tileX - 1), static_cast<uint32>(m_tileY)))
				{
					westLod = westTile->GetCurrentLOD();
				}
			}

			// Calculate the stitch key for the current LOD configuration
			const uint32 stitchKey = newLod | (northLod << 4) | (eastLod << 8) | (southLod << 12) | (westLod << 16);

			// Only regenerate if the LOD or stitching configuration changed
			if (stitchKey != m_currentStitchKey)
			{
				m_currentLod = newLod;

				// Check if we already have this configuration cached
				if (m_lodIndexCache.find(stitchKey) == m_lodIndexCache.end())
				{
					CreateIndexData(newLod, northLod, eastLod, southLod, westLod);
				}
				else
				{
					m_currentStitchKey = stitchKey;
				}
			}
		}

		uint32 Tile::GetCurrentLOD() const
		{
			return m_currentLod;
		}

		bool Tile::HasRenderableGeometry() const
		{
			return m_hasRenderableGeometry;
		}

		bool Tile::TestCapsuleCollision(const Capsule &capsule, std::vector<CollisionResult> &results) const
		{
			// Transform capsule to tile's local space
			const Matrix4 worldTransform = GetParentNodeFullTransform();
			const Matrix4 localTransform = worldTransform.Inverse();

			const Vector3 localPointA = localTransform * capsule.GetPointA();
			const Vector3 localPointB = localTransform * capsule.GetPointB();
			const Capsule localCapsule(localPointA, localPointB, capsule.GetRadius());

			// Get the bounding box of the capsule in local space
			const AABB capsuleBounds = localCapsule.GetBounds();

			// Test against terrain triangles
			constexpr float scale = static_cast<float>(constants::TileSize / static_cast<double>(constants::OuterVerticesPerTileSide - 1));

			// Calculate the range of cells that could potentially intersect with the capsule
			const float invScale = 1.0f / scale;
			const int32 minI = std::max(0, static_cast<int32>(std::floor((capsuleBounds.min.x * invScale) - m_startX)));
			const int32 maxI = std::min(static_cast<int32>(constants::InnerVerticesPerTileSide), static_cast<int32>(std::ceil((capsuleBounds.max.x * invScale) - m_startX)));
			const int32 minJ = std::max(0, static_cast<int32>(std::floor((capsuleBounds.min.z * invScale) - m_startZ)));
			const int32 maxJ = std::min(static_cast<int32>(constants::InnerVerticesPerTileSide), static_cast<int32>(std::ceil((capsuleBounds.max.z * invScale) - m_startZ)));

			bool hasCollision = false;

			// Get the hole map for this tile
			const uint64 holeMap = m_page.GetTileHoleMap(m_tileX, m_tileY);

			// Extract rotation matrix for transforming normals (no translation, no scale)
			// For normals, we need to use the inverse transpose of the upper 3x3 matrix
			// But for orthogonal transforms (rotation only), this equals the rotation matrix
			Matrix3 normalTransform;
			normalTransform[0][0] = worldTransform[0][0]; normalTransform[0][1] = worldTransform[0][1]; normalTransform[0][2] = worldTransform[0][2];
			normalTransform[1][0] = worldTransform[1][0]; normalTransform[1][1] = worldTransform[1][1]; normalTransform[1][2] = worldTransform[1][2];
			normalTransform[2][0] = worldTransform[2][0]; normalTransform[2][1] = worldTransform[2][1]; normalTransform[2][2] = worldTransform[2][2];

			// Iterate through cells that could potentially intersect
			// Each cell corresponds to one inner vertex with 4 surrounding triangles
			for (int32 j = minJ; j < maxJ; ++j)
			{
				for (int32 i = minI; i < maxI; ++i)
				{
					// Check if this inner vertex is marked as a hole
					const uint32 bitIndex = static_cast<uint32>(i + j * constants::InnerVerticesPerTileSide);
					const bool isHole = (holeMap & (1ULL << bitIndex)) != 0;

					// Skip collision testing for holes
					if (isHole)
					{
						continue;
					}

					// Get positions of the 4 outer vertices surrounding this cell
					const size_t globalX = m_startX + i;
					const size_t globalZ = m_startZ + j;

					const float x1 = scale * globalX;
					const float z1 = scale * globalZ;
					const float x2 = scale * (globalX + 1);
					const float z2 = scale * (globalZ + 1);

					const float h00 = m_page.GetHeightAt(globalX, globalZ);
					const float h10 = m_page.GetHeightAt(globalX + 1, globalZ);
					const float h01 = m_page.GetHeightAt(globalX, globalZ + 1);
					const float h11 = m_page.GetHeightAt(globalX + 1, globalZ + 1);

					// Calculate inner vertex position (center of quad)
					const float centerX = (x1 + x2) * 0.5f;
					const float centerZ = (z1 + z2) * 0.5f;
					const float centerHeight = (h00 + h10 + h01 + h11) * 0.25f;

					// Create bounding box for this cell
					const float minHeight = std::min(std::min(h00, h10), std::min(h01, h11));
					const float maxHeight = std::max(std::max(h00, h10), std::max(h01, h11));
					const AABB cellBounds(Vector3(x1, minHeight, z1), Vector3(x2, maxHeight, z2));

					// Skip if capsule doesn't intersect with this cell's bounds
					if (!capsuleBounds.Intersects(cellBounds))
					{
						continue;
					}

					// Define the vertices (in local space)
					const Vector3 vTL(x1, h00, z1);						   // Top-left outer
					const Vector3 vTR(x2, h10, z1);						   // Top-right outer
					const Vector3 vBL(x1, h01, z2);						   // Bottom-left outer
					const Vector3 vBR(x2, h11, z2);						   // Bottom-right outer
					const Vector3 vCenter(centerX, centerHeight, centerZ); // Center inner

					// Test all 4 triangles formed by the inner vertex
					Vector3 contactPoint, contactNormal;
					float penetration, distance;

					// Top triangle: Center - TL - TR
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vTL, vTR, contactPoint, contactNormal, penetration, distance))
					{
						// Transform contact point and normal back to world space
						const Vector3 worldContactPoint = worldTransform * contactPoint;
						const Vector3 worldContactNormal = (normalTransform * contactNormal).NormalizedCopy();
						const Vector3 worldV0 = worldTransform * vCenter;
						const Vector3 worldV1 = worldTransform * vTL;
						const Vector3 worldV2 = worldTransform * vTR;
						results.emplace_back(true, worldContactPoint, worldContactNormal, worldV0, worldV1, worldV2, penetration, distance);
						hasCollision = true;
					}

					// Right triangle: Center - TR - BR
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vTR, vBR, contactPoint, contactNormal, penetration, distance))
					{
						// Transform contact point and normal back to world space
						const Vector3 worldContactPoint = worldTransform * contactPoint;
						const Vector3 worldContactNormal = (normalTransform * contactNormal).NormalizedCopy();
						const Vector3 worldV0 = worldTransform * vCenter;
						const Vector3 worldV1 = worldTransform * vTR;
						const Vector3 worldV2 = worldTransform * vBR;
						results.emplace_back(true, worldContactPoint, worldContactNormal, worldV0, worldV1, worldV2, penetration, distance);
						hasCollision = true;
					}

					// Bottom triangle: Center - BR - BL
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vBR, vBL, contactPoint, contactNormal, penetration, distance))
					{
						// Transform contact point and normal back to world space
						const Vector3 worldContactPoint = worldTransform * contactPoint;
						const Vector3 worldContactNormal = (normalTransform * contactNormal).NormalizedCopy();
						const Vector3 worldV0 = worldTransform * vCenter;
						const Vector3 worldV1 = worldTransform * vBR;
						const Vector3 worldV2 = worldTransform * vBL;
						results.emplace_back(true, worldContactPoint, worldContactNormal, worldV0, worldV1, worldV2, penetration, distance);
						hasCollision = true;
					}

					// Left triangle: Center - BL - TL
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vBL, vTL, contactPoint, contactNormal, penetration, distance))
					{
						// Transform contact point and normal back to world space
						const Vector3 worldContactPoint = worldTransform * contactPoint;
						const Vector3 worldContactNormal = (normalTransform * contactNormal).NormalizedCopy();
						const Vector3 worldV0 = worldTransform * vCenter;
						const Vector3 worldV1 = worldTransform * vBL;
						const Vector3 worldV2 = worldTransform * vTL;
						results.emplace_back(true, worldContactPoint, worldContactNormal, worldV0, worldV1, worldV2, penetration, distance);
						hasCollision = true;
					}
				}
			}

			return hasCollision;
		}

		bool Tile::TestRayCollision(const Ray &ray, CollisionResult &result) const
		{
			// Transform ray to tile's local space
			const Matrix4 worldTransform = GetParentNodeFullTransform();
			const Matrix4 localTransform = worldTransform.Inverse();

			const Vector3 localOrigin = localTransform * ray.origin;
			Vector3 localDirection = localTransform * (ray.origin + ray.GetDirection()) - localOrigin;
			localDirection.Normalize();

			// Calculate the ray length in local space
			const float rayLength = ray.GetLength();
			const Ray localRay(localOrigin, localDirection, rayLength);

			// Test against terrain triangles
			constexpr float scale = static_cast<float>(constants::TileSize / static_cast<double>(constants::OuterVerticesPerTileSide - 1));

			bool hasCollision = false;
			float closestDistance = std::numeric_limits<float>::max();
			Vector3 closestIntersectionPoint;

			// Get the hole map for this tile
			const uint64 holeMap = m_page.GetTileHoleMap(m_tileX, m_tileY);

			// Iterate through all cells (each cell has an inner vertex with 4 triangles)
			for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
				{
					// Check if this inner vertex is marked as a hole
					const uint32 bitIndex = static_cast<uint32>(i + j * constants::InnerVerticesPerTileSide);
					const bool isHole = (holeMap & (1ULL << bitIndex)) != 0;

					// Skip ray testing for holes
					if (isHole)
					{
						continue;
					}

					// Get the 4 outer vertices and center inner vertex
					const size_t globalX = m_startX + i;
					const size_t globalZ = m_startZ + j;

					const float x1 = scale * globalX;
					const float z1 = scale * globalZ;
					const float x2 = scale * (globalX + 1);
					const float z2 = scale * (globalZ + 1);

					const float h00 = m_page.GetHeightAt(globalX, globalZ);
					const float h10 = m_page.GetHeightAt(globalX + 1, globalZ);
					const float h01 = m_page.GetHeightAt(globalX, globalZ + 1);
					const float h11 = m_page.GetHeightAt(globalX + 1, globalZ + 1);

					// Calculate inner vertex position
					const float centerX = (x1 + x2) * 0.5f;
					const float centerZ = (z1 + z2) * 0.5f;
					const float centerHeight = (h00 + h10 + h01 + h11) * 0.25f;

					// Define vertices
					const Vector3 vTL(x1, h00, z1);
					const Vector3 vTR(x2, h10, z1);
					const Vector3 vBL(x1, h01, z2);
					const Vector3 vBR(x2, h11, z2);
					const Vector3 vCenter(centerX, centerHeight, centerZ);

					// Test all 4 triangles
					float t;
					Vector3 intersectionPoint;

					// Top triangle: Center - TR - TL
					if (Terrain::RayTriangleIntersection(localRay, vCenter, vTR, vTL, t, intersectionPoint))
					{
						if (t >= 0.0f && t < closestDistance)
						{
							closestDistance = t;
							closestIntersectionPoint = intersectionPoint;
							hasCollision = true;
						}
					}

					// Right triangle: Center - BR - TR
					if (Terrain::RayTriangleIntersection(localRay, vCenter, vBR, vTR, t, intersectionPoint))
					{
						if (t >= 0.0f && t < closestDistance)
						{
							closestDistance = t;
							closestIntersectionPoint = intersectionPoint;
							hasCollision = true;
						}
					}

					// Bottom triangle: Center - BL - BR
					if (Terrain::RayTriangleIntersection(localRay, vCenter, vBL, vBR, t, intersectionPoint))
					{
						if (t >= 0.0f && t < closestDistance)
						{
							closestDistance = t;
							closestIntersectionPoint = intersectionPoint;
							hasCollision = true;
						}
					}

					// Left triangle: Center - TL - BL
					if (Terrain::RayTriangleIntersection(localRay, vCenter, vTL, vBL, t, intersectionPoint))
					{
						if (t >= 0.0f && t < closestDistance)
						{
							closestDistance = t;
							closestIntersectionPoint = intersectionPoint;
							hasCollision = true;
						}
					}
				}
			}

			if (hasCollision)
			{
				// Transform intersection point back to world space
				result.hasCollision = true;
				result.contactPoint = worldTransform * closestIntersectionPoint;

				// Calculate normal at intersection point by interpolating vertex normals
				const float invScale = 1.0f / scale;
				const size_t gridX = static_cast<size_t>((closestIntersectionPoint.x * invScale) - m_startX + 0.5f);
				const size_t gridZ = static_cast<size_t>((closestIntersectionPoint.z * invScale) - m_startZ + 0.5f);

				const size_t clampedX = std::min(gridX, static_cast<size_t>(constants::OuterVerticesPerTileSide - 1));
				const size_t clampedZ = std::min(gridZ, static_cast<size_t>(constants::OuterVerticesPerTileSide - 1));

				const Vector3 localNormal = m_page.GetNormalAt(m_startX + clampedX, m_startZ + clampedZ);
				result.contactNormal = (worldTransform * Vector4(localNormal, 0.0f)).Ptr();
				result.contactNormal.Normalize();

				result.penetrationDepth = closestDistance;
			}

			return hasCollision;
		}
	}
}
