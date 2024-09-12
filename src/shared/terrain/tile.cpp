
#include "tile.h"

#include "page.h"

static constexpr uint32 TilesPerPage = 16;
static constexpr uint32 VerticesPerPage = 256;
static constexpr uint32 VerticesPerTile = VerticesPerPage / TilesPerPage;
static constexpr uint32 VerticesPerTileSquared = VerticesPerPage * VerticesPerPage;

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

			m_tileX = m_startX / (VerticesPerTile - 1);
			m_tileZ = m_startZ / (VerticesPerTile - 1);

		}

		Tile::~Tile() = default;

		void Tile::PrepareRenderOperation(RenderOperation& operation)
		{

		}

		const Matrix4& Tile::GetWorldTransform() const
		{
			return Matrix4::Identity;
		}

		float Tile::GetSquaredViewDepth(const Camera& camera) const
		{
			return 0.0f;
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
			return 0.0f;
		}

		void Tile::VisitRenderables(Visitor& visitor, bool debugRenderables)
		{
		}

		void Tile::PopulateRenderQueue(RenderQueue& queue)
		{
		}

		void Tile::CreateVertexData(size_t startX, size_t startZ)
		{
			m_vertexData = std::make_unique<VertexData>();
			m_vertexData->vertexStart = 0;
			m_vertexData->vertexCount = VerticesPerTileSquared;

			VertexDeclaration* decl = m_vertexData->vertexDeclaration;
			VertexBufferBinding* bind = m_vertexData->vertexBufferBinding;

			uint32 offset = 0;
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

			m_mainBuffer = GraphicsDevice::Get().CreateVertexBuffer(m_vertexData->vertexCount, decl->GetVertexSize(0), BufferUsage::DynamicWriteOnly, nullptr);
			bind->SetBinding(0, m_mainBuffer);

			const size_t endX = startX + VerticesPerTile;
			const size_t endZ = startZ + VerticesPerTile;

			float minHeight = 0.0f;
			float maxHeight = 0.0f;

			const VertexElement* posElem = decl->FindElementBySemantic(VertexElementSemantic::Position);
			const VertexElement* texElem0 = decl->FindElementBySemantic(VertexElementSemantic::TextureCoordinate, 0);
			const VertexElement* normElem = decl->FindElementBySemantic(VertexElementSemantic::Normal);
			const VertexElement* tangentElem = decl->FindElementBySemantic(VertexElementSemantic::Tangent);
			unsigned char* pBase = static_cast<unsigned char*>(m_mainBuffer->Map(LockOptions::Normal));

			minHeight = m_page.GetHeightAt(0, 0);
			maxHeight = minHeight;

			const float scale = 1.0f;

			for (size_t j = startZ; j < endZ; ++j)
			{
				for (size_t i = startX; i < endX; ++i)
				{
					float* pPos, * pTex0;
					float* pNorm, * pTangent;
					posElem->BaseVertexPointerToElement(pBase, &pPos);
					texElem0->BaseVertexPointerToElement(pBase, &pTex0);
					normElem->BaseVertexPointerToElement(pBase, &pNorm);
					tangentElem->BaseVertexPointerToElement(pBase, &pTangent);

					float height = m_page.GetHeightAt(i, j);

					*pPos++ = scale * i;
					*pPos++ = height;
					*pPos++ = scale * j;

					*pTex0++ = static_cast<float>(i) / static_cast<float>(VerticesPerPage + 1);
					*pTex0++ = static_cast<float>(j) / static_cast<float>(VerticesPerPage + 1);

					if (height < minHeight) {
						minHeight = height;
					}

					if (height > maxHeight) {
						maxHeight = height;
					}

					const Vector3& normal = m_page.GetNormalAt(i, j);
					const Vector3& tangent = m_page.GetTangentAt(i, j);

					pNorm[0] = normal.x; pNorm[1] = normal.y; pNorm[2] = normal.z;
					pTangent[0] = tangent.x; pTangent[1] = tangent.y; pTangent[2] = tangent.z;

					pBase += m_mainBuffer->GetVertexSize();
				}
			}

			m_mainBuffer->Unmap();

			if (minHeight == maxHeight) maxHeight = minHeight + 0.1f;

			m_bounds = AABB(
				Vector3(scale * startX, minHeight, scale * startZ),
				Vector3(scale * endX, maxHeight, scale * endZ));

			m_center = m_bounds.GetCenter();

			m_boundingRadius = (m_bounds.max - m_center).GetLength();
		}
	}
}
