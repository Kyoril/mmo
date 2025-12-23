#include "tile.h"

#include "page.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/scene_node.h"
#include "terrain.h"
#include "graphics/material_instance.h"
#include "graphics/texture_mgr.h"
#include "math/capsule.h"
#include "math/collision.h"
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
			CreateIndexData(0, 0);

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
			operation.indexData = m_indexData.get();
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
			return GetParentSceneNode()->GetSquaredViewDepth(camera);
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
			queue.AddRenderable(*this, m_renderQueueId);

			if (m_page.GetTerrain().IsWireframeVisible())
			{
				queue.AddRenderable(*this, WireframeRenderGroupId);
			}
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

			// Update outer vertices (9x9 = 81 vertices)
			for (size_t j = 0; j < constants::OuterVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::OuterVerticesPerTileSide; ++i)
				{
					const size_t globalX = m_startX + i;
					const size_t globalZ = m_startZ + j;

					const float height = m_page.GetHeightAt(globalX, globalZ);

					if (height < minHeight)
					{
						minHeight = height;
					}
					if (height > maxHeight)
					{
						maxHeight = height;
					}

					vert->position = Vector3(outerScale * globalX, height, outerScale * globalZ);
					vert->normal = m_page.CalculateNormalAt(globalX, globalZ);

					const Vector3 arbitrary = std::abs(vert->normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					vert->tangent = (arbitrary - vert->normal * vert->normal.Dot(arbitrary)).NormalizedCopy();
					vert->binormal = vert->normal.Cross(vert->tangent).NormalizedCopy();

					vert->color = Color(m_page.GetColorAt(globalX, globalZ)).GetABGR();
					vert->u = static_cast<float>(i) / static_cast<float>(constants::OuterVerticesPerTileSide - 1);
					vert->v = static_cast<float>(j) / static_cast<float>(constants::OuterVerticesPerTileSide - 1);

					vert++;
				}
			}

			// Update inner vertices (8x8 = 64 vertices)
			for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
				{
					const size_t globalX = m_startX + i;
					const size_t globalZ = m_startZ + j;

					// Use stored inner height for full editor precision
					const size_t innerPageX = m_tileX * constants::InnerVerticesPerTileSide + i;
					const size_t innerPageZ = m_tileY * constants::InnerVerticesPerTileSide + j;
					const float height = m_page.GetInnerHeightAt(innerPageX, innerPageZ);

					if (height < minHeight)
					{
						minHeight = height;
					}
					if (height > maxHeight)
					{
						maxHeight = height;
					}

					const float worldX = outerScale * (globalX + 0.5f);
					const float worldZ = outerScale * (globalZ + 0.5f);

					vert->position = Vector3(worldX, height, worldZ);

					// Use stored inner normal (fallback: recompute from outer if needed)
					Vector3 innerNormal = m_page.GetInnerNormalAt(innerPageX, innerPageZ);
					if (innerNormal.IsZeroLength())
					{
						const Vector3 n00 = m_page.CalculateNormalAt(globalX, globalZ);
						const Vector3 n10 = m_page.CalculateNormalAt(globalX + 1, globalZ);
						const Vector3 n01 = m_page.CalculateNormalAt(globalX, globalZ + 1);
						const Vector3 n11 = m_page.CalculateNormalAt(globalX + 1, globalZ + 1);
						innerNormal = ((n00 + n10 + n01 + n11) * 0.25f).NormalizedCopy();
					}
					vert->normal = innerNormal;

					const Vector3 arbitrary = std::abs(vert->normal.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					vert->tangent = (arbitrary - vert->normal * vert->normal.Dot(arbitrary)).NormalizedCopy();
					vert->binormal = vert->normal.Cross(vert->tangent).NormalizedCopy();

					// Use stored inner color
					vert->color = Color(m_page.GetInnerColorAt(innerPageX, innerPageZ)).GetABGR();

					vert->u = (static_cast<float>(i) + 0.5f) / static_cast<float>(constants::InnerVerticesPerTileSide);
					vert->v = (static_cast<float>(j) + 0.5f) / static_cast<float>(constants::InnerVerticesPerTileSide);

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
		CreateIndexData(0, 0);
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
				Vector3(outerScale * startX, minHeight, outerScale * startZ),
				Vector3(outerScale * (startX + constants::OuterVerticesPerTileSide - 1), maxHeight, outerScale * (startZ + constants::OuterVerticesPerTileSide - 1)));

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

		void Tile::CreateIndexData(uint32 lod, uint32 neighborState)
		{
			// For the new structure, we'll create indices differently:
			// Each inner vertex connects to 4 outer vertices to form 4 triangles
			// Pattern for each quad (O = outer, I = inner):
			//   O---O
			//   |\I/|
			//   | X |
			//   |/I\|
			//   O---O

			// Estimate index count:
			// - Each of 64 inner vertices creates 4 triangles = 256 triangles = 768 indices
			// We'll allocate more for safety
			std::vector<uint16> indices;
			indices.reserve(800);

			// Get the hole map for this tile
			const uint64 holeMap = m_page.GetTileHoleMap(m_tileX, m_tileY);

			// For each inner vertex, create 4 triangles connecting to surrounding outer vertices
			for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
				{
					// Check if this inner vertex is marked as a hole
					const uint32 bitIndex = static_cast<uint32>(i + j * constants::InnerVerticesPerTileSide);
					const bool isHole = (holeMap & (1ULL << bitIndex)) != 0;

					// Skip triangles for holes
					if (isHole)
					{
						continue;
					}

					const uint16 innerIdx = GetInnerVertexIndex(i, j);

					// Get the 4 surrounding outer vertices
					const uint16 outerTL = GetOuterVertexIndex(i, j);		  // Top-left
					const uint16 outerTR = GetOuterVertexIndex(i + 1, j);	  // Top-right
					const uint16 outerBL = GetOuterVertexIndex(i, j + 1);	  // Bottom-left
					const uint16 outerBR = GetOuterVertexIndex(i + 1, j + 1); // Bottom-right

					// Create 4 triangles (counter-clockwise winding when viewed from above)
					// Top triangle
					indices.push_back(innerIdx);
					indices.push_back(outerTR);
					indices.push_back(outerTL);

					// Right triangle
					indices.push_back(innerIdx);
					indices.push_back(outerBR);
					indices.push_back(outerTR);

					// Bottom triangle
					indices.push_back(innerIdx);
					indices.push_back(outerBL);
					indices.push_back(outerBR);

					// Left triangle
					indices.push_back(innerIdx);
					indices.push_back(outerTL);
					indices.push_back(outerBL);
				}
			}

			m_indexData = std::make_unique<IndexData>();
			m_indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(
				static_cast<uint32>(indices.size()),
				IndexBufferSize::Index_16,
				BufferUsage::StaticWriteOnly,
				indices.data());
			m_indexData->indexCount = static_cast<uint32>(indices.size());
			m_indexData->indexStart = 0;
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

			// Iterate through cells that could potentially intersect
			// Each cell corresponds to one inner vertex with 4 surrounding triangles
			for (int32 j = minJ; j < maxJ; ++j)
			{
				for (int32 i = minI; i < maxI; ++i)
				{
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

					// Define the vertices
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
						results.emplace_back(true, contactPoint, contactNormal, vCenter, vTL, vTR, penetration, distance);
						hasCollision = true;
					}

					// Right triangle: Center - TR - BR
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vTR, vBR, contactPoint, contactNormal, penetration, distance))
					{
						results.emplace_back(true, contactPoint, contactNormal, vCenter, vTR, vBR, penetration, distance);
						hasCollision = true;
					}

					// Bottom triangle: Center - BR - BL
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vBR, vBL, contactPoint, contactNormal, penetration, distance))
					{
						results.emplace_back(true, contactPoint, contactNormal, vCenter, vBR, vBL, penetration, distance);
						hasCollision = true;
					}

					// Left triangle: Center - BL - TL
					if (CapsuleTriangleIntersection(localCapsule, vCenter, vBL, vTL, contactPoint, contactNormal, penetration, distance))
					{
						results.emplace_back(true, contactPoint, contactNormal, vCenter, vBL, vTL, penetration, distance);
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

			// Iterate through all cells (each cell has an inner vertex with 4 triangles)
			for (size_t j = 0; j < constants::InnerVerticesPerTileSide; ++j)
			{
				for (size_t i = 0; i < constants::InnerVerticesPerTileSide; ++i)
				{
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
