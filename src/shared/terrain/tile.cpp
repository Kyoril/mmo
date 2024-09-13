
#include "tile.h"

#include "page.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/scene_node.h"

static constexpr uint32 TilesPerPage = 16;
static constexpr uint32 VerticesPerPage = 256;
static constexpr uint32 VerticesPerTile = VerticesPerPage / TilesPerPage;
static constexpr uint32 VerticesPerTileSquared = VerticesPerTile * VerticesPerTile;

namespace mmo
{
	namespace terrain
	{
		Tile::Tile(const String& name, Page& page, size_t startX, size_t startZ)
			: MovableObject(name)
			, Renderable()
			, m_page(page)
			, m_startX(startX)
			, m_startZ(startZ)
		{
			SetRenderQueueGroup(WorldGeometry1);

			m_tileX = m_startX / (17 - 1);
			m_tileZ = m_startZ / (17 - 1);

			CreateVertexData(m_startX, m_startZ);
			CreateIndexData(0, 0);
		}

		Tile::~Tile() = default;

		void Tile::PrepareRenderOperation(RenderOperation& operation)
		{
			operation.vertexData = m_vertexData.get();
			operation.indexData = m_indexData.get();
			operation.topology = TopologyType::TriangleList;
			operation.material = GetMaterial();
		}

		const Matrix4& Tile::GetWorldTransform() const
		{
			return GetParentNodeFullTransform();
		}

		float Tile::GetSquaredViewDepth(const Camera& camera) const
		{
			return GetParentSceneNode()->GetSquaredViewDepth(camera);
		}

		MaterialPtr Tile::GetMaterial() const
		{
			return nullptr;
		}

		const String& Tile::GetMovableType() const
		{
			static const String type = "Tile";
			return type;
		}

		const AABB& Tile::GetBoundingBox() const
		{
			return m_bounds;
		}

		float Tile::GetBoundingRadius() const
		{
			return m_boundingRadius;
		}

		void Tile::VisitRenderables(Visitor& visitor, bool debugRenderables)
		{
			visitor.Visit(*this, 0, false);
		}

		void Tile::PopulateRenderQueue(RenderQueue& queue)
		{
			queue.AddRenderable(*this, m_renderQueueId);
		}

		void Tile::CreateVertexData(size_t startX, size_t startZ)
		{
			m_vertexData = std::make_unique<VertexData>();
			m_vertexData->vertexStart = 0;
			m_vertexData->vertexCount = 17*17;

			VertexDeclaration* decl = m_vertexData->vertexDeclaration;
			VertexBufferBinding* bind = m_vertexData->vertexBufferBinding;

			uint32 offset = 0;
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

			const size_t endX = startX + 17;
			const size_t endZ = startZ + 17;

			float minHeight = m_page.GetHeightAt(0, 0);
			float maxHeight = minHeight;

			const float scale = 2.0f;

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
			VertexStruct* vert = vertices.data();

			for (size_t j = startZ; j < endZ; ++j)
			{
				for (size_t i = startX; i < endX; ++i)
				{
					const float height = m_page.GetHeightAt(i, j);
					vert->position = Vector3(scale * i, height, scale * j);
					vert->normal = m_page.GetNormalAt(i, j);
					vert->tangent = m_page.GetTangentAt(i, j);
					// TODO: vert->binormal = m_page.GetTangentAt(i, j);
					vert->binormal = Vector3::UnitX;
					vert->color = 0xFFFFFFFF;
					vert->u = static_cast<float>(i) / static_cast<float>(256 + 1);
					vert->v = static_cast<float>(j) / static_cast<float>(256 + 1);

					if (height < minHeight) {
						minHeight = height;
					}

					if (height > maxHeight) {
						maxHeight = height;
					}

					vert++;
				}
			}

			m_mainBuffer = GraphicsDevice::Get().CreateVertexBuffer(m_vertexData->vertexCount, decl->GetVertexSize(0), BufferUsage::DynamicWriteOnly, vertices.data());
			bind->SetBinding(0, m_mainBuffer);

			if (minHeight == maxHeight) maxHeight = minHeight + 0.1f;

			m_bounds = AABB(
				Vector3(scale * startX, minHeight, scale * startZ),
				Vector3(scale * endX, maxHeight, scale * endZ));

			m_center = m_bounds.GetCenter();

			m_boundingRadius = (m_bounds.max - m_center).GetLength();
		}

		void Tile::CreateIndexData(uint32 lod, uint32 neighborState)
		{
			const unsigned int step = 1 << lod;

			const unsigned int northLOD = neighborState >> 24;
			const unsigned int eastLOD = (neighborState >> 16) & 0xFF;
			const unsigned int southLOD = (neighborState >> 8) & 0xFF;
			const unsigned int westLOD = neighborState & 0xFF;
			const unsigned int north = northLOD ? step : 0;
			const unsigned int east = eastLOD ? step : 0;
			const unsigned int south = southLOD ? step : 0;
			const unsigned int west = westLOD ? step : 0;

			const uint32 tileSize = 17;

			const size_t newLength = (tileSize / step) * (tileSize / step) * 2 * 2 * 2;
			std::vector<uint16> indices(newLength);
			uint16* pIdx = indices.data();

			unsigned int numIndexes = 0;

			// go over all vertices and combine them to triangles in trilist format.
			// leave out the edges if we need to stitch those in case over lower LOD at the
			// neighbour.
			for (unsigned int j = north; j < tileSize - 1 - south; j += step)
			{
				for (unsigned int i = west; i < tileSize - 1 - east; i += step)
				{
					// triangles
					*pIdx++ = GetIndex(i, j);
					*pIdx++ = GetIndex(i, j + step);
					*pIdx++ = GetIndex(i + step, j);

					*pIdx++ = GetIndex(i, j + step);
					*pIdx++ = GetIndex(i + step, j + step);
					*pIdx++ = GetIndex(i + step, j);

					numIndexes += 6;
				}
			}

			// stitching edges to neighbours where needed
			if (northLOD) {
				numIndexes += StitchEdge(Direction::North, lod, northLOD, westLOD > 0, eastLOD > 0, &pIdx);
			}
			if (eastLOD) {
				numIndexes += StitchEdge(Direction::East, lod, eastLOD, northLOD > 0, southLOD > 0, &pIdx);
			}
			if (southLOD) {
				numIndexes += StitchEdge(Direction::South, lod, southLOD, eastLOD > 0, westLOD > 0, &pIdx);
			}
			if (westLOD) {
				numIndexes += StitchEdge(Direction::West, lod, westLOD, southLOD > 0, northLOD > 0, &pIdx);
			}

			m_indexData = std::make_unique<IndexData>();
			m_indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(numIndexes, IndexBufferSize::Index_16, BufferUsage::StaticWriteOnly, indices.data());
			m_indexData->indexCount = numIndexes;
			m_indexData->indexStart = 0;
		}

		uint16 Tile::GetIndex(size_t x, size_t y) const
		{
			return static_cast<uint16>(x + (y * 17));
		}

		uint32 Tile::StitchEdge(Direction direction, uint32 hiLOD, uint32 loLOD, bool omitFirstTri, bool omitLastTri, uint16** ppIdx)
		{
			ASSERT(loLOD > hiLOD);

			unsigned short* pIdx = *ppIdx;

			int step = 1 << hiLOD;
			int superstep = 1 << loLOD;
			int halfsuperstep = superstep >> 1;
			int rowstep = 0;
			size_t startx = 0, starty = 0, endx = 0;
			bool horizontal = false;

			switch (direction)
			{
			case Direction::North:
				startx = starty = 0;
				endx = 17 - 1;
				rowstep = step;
				horizontal = true;
				break;

			case Direction::South:
				startx = starty = 17 - 1;
				endx = 0;
				rowstep = -step;
				step = -step;
				superstep = -superstep;
				halfsuperstep = -halfsuperstep;
				horizontal = true;
				break;

			case Direction::East:
				startx = 0;
				endx = 17 - 1;
				starty = 17 - 1;
				rowstep = -step;
				horizontal = false;
				break;

			case Direction::West:
				startx = 17 - 1;
				endx = 0;
				starty = 0;
				rowstep = step;
				step = -step;
				superstep = -superstep;
				halfsuperstep = -halfsuperstep;
				horizontal = false;
				break;
			}

			unsigned int numIndexes = 0;

			for (size_t j = startx; j != endx; j += superstep)
			{
				int k;
				for (k = 0; k != halfsuperstep; k += step)
				{
					size_t jk = j + k;
					if (j != startx || k != 0 || !omitFirstTri)
					{
						if (horizontal)
						{
							*pIdx++ = GetIndex(j, starty);
							*pIdx++ = GetIndex(jk, starty + rowstep);
							*pIdx++ = GetIndex(jk + step, starty + rowstep);
						}
						else
						{
							*pIdx++ = GetIndex(starty, j);
							*pIdx++ = GetIndex(starty + rowstep, jk);
							*pIdx++ = GetIndex(starty + rowstep, jk + step);
						}
						numIndexes += 3;
					}
				}

				if (horizontal)
				{
					*pIdx++ = GetIndex(j, starty);
					*pIdx++ = GetIndex(j + halfsuperstep, starty + rowstep);
					*pIdx++ = GetIndex(j + superstep, starty);
				}
				else
				{
					*pIdx++ = GetIndex(starty, j);
					*pIdx++ = GetIndex(starty + rowstep, j + halfsuperstep);
					*pIdx++ = GetIndex(starty, j + superstep);
				}
				numIndexes += 3;

				for (k = halfsuperstep; k != superstep; k += step)
				{
					size_t jk = j + k;
					if (j != endx - superstep || k != superstep - step || !omitLastTri)
					{
						if (horizontal)
						{
							*pIdx++ = GetIndex(j + superstep, starty);
							*pIdx++ = GetIndex(jk, starty + rowstep);
							*pIdx++ = GetIndex(jk + step, starty + rowstep);
						}
						else
						{
							*pIdx++ = GetIndex(starty, j + superstep);
							*pIdx++ = GetIndex(starty + rowstep, jk);
							*pIdx++ = GetIndex(starty + rowstep, jk + step);
						}
						numIndexes += 3;
					}
				}
			}

			*ppIdx = pIdx;

			return numIndexes;
		}
	}
}
